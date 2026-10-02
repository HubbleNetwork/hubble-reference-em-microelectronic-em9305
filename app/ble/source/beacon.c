/*
 * Copyright (c) 2026 Hubble Network, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <dm_api.h>
#include <dm_handler.h>
#include <hci_api.h>
#include <hci_handler.h>
#include <ll_init_api.h>
#include <pal_bb.h>
#include <pal_cfg.h>
#include <pal_crypto.h>
#include <pal_sys.h>
#include <rtc.h>
#include <wsf_buf.h>
#include <wsf_heap.h>
#include <wsf_msg.h>
#include <wsf_os.h>
#include <wsf_timer.h>
#include <wsf_types.h>

#include <hubble/ble.h>
#include <hubble/hubble.h>
#include <hubble/port/crypto.h>
#include <hubble/port/sys.h>

#include <beacon.h>

/* Pull the Hubble key, and if relevant, the Unix time from `embed_key_time.py` output. */
#if defined(__has_include)
#if __has_include(<key.c>)
#include <key.c>
#endif
#if defined(CONFIG_HUBBLE_COUNTER_SOURCE_UNIX_TIME) && __has_include(<time.c>)
#include <time.c>
#endif
#endif

#if !defined(HUBBLE_KEY_SET)
#error "No device key. Run embed_key_time.py (see README)."
#endif
#if defined(CONFIG_HUBBLE_COUNTER_SOURCE_UNIX_TIME) && !defined(HUBBLE_UNIX_TIME_SET)
#error "No provisioned time. Run embed_key_time.py (see README)."
#endif

/* Bluetooth SIG AD types. */
#define AD_TYPE_COMPLETE_LIST_16_BIT_UUID 0x03U
#define AD_TYPE_SERVICE_16_BIT_DATA       0x16U

/* Legacy advertising carries at most 31 bytes of AD data. */
#define ADV_DATA_MAX_LEN 31U

/* (len + type + UUID) + (len + type) + Hubble payload. */
#define HUBBLE_ADV_TOTAL_LEN                                                   \
	(4U + 2U + HUBBLE_BLE_ADV_HEADER_SIZE + HUBBLE_USER_PAYLOAD_LEN)

_Static_assert(HUBBLE_USER_PAYLOAD_LEN <= HUBBLE_BLE_MAX_DATA_LEN,
	       "HUBBLE_USER_PAYLOAD_LEN exceeds HUBBLE_BLE_MAX_DATA_LEN");
_Static_assert(HUBBLE_ADV_TOTAL_LEN <= ADV_DATA_MAX_LEN,
	       "Hubble advertisement does not fit in 31 bytes of AD data");

/* A beacon never connects or scans. */
#define LL_RT_CFG_NUM_TX_BUFS     (2u)
#define LL_RT_CFG_NUM_RX_BUFS     (2u)
#define LL_RT_CFG_MAX_ADV_REPORTS (1u)

/* Sent by the refresh timer to _HubbleHandler(). Below DM_CBACK_START, so
 * it cannot collide with a DM event. */
#define HUBBLE_ADV_REFRESH_MSG 0x01U

/* DM and HCI command/event buffers; nothing larger is ever in flight. */
static const wsfBufPoolDesc_t gcBuffPoolDesc[] = {
	{ .len = 16, .num = 8 },
	{ .len = 32, .num = 4 },
	{ .len = 64, .num = 2 },
	{ .len = 128, .num = 2 },
};

/* The stack keeps pointers to these. */
static BbRtCfg_t gBbRtCfg;
static LlRtCfg_t gLlRtCfg;

static wsfHandlerId_t gHubbleHandlerId;

/* Rearmed each time it fires. */
static wsfTimer_t gRefreshTimer;

/* DmAdvSetData() keeps this pointer rather than copying. */
static uint8_t gAdvData[ADV_DATA_MAX_LEN];
static uint8_t gAdvDataLen;

/* The controller is up, and whether advertising is actually running. A
 * refresh that finds it stopped retries the start. */
static bool gDmReady;
static bool gAdvRunning;

/*
 * A fresh non-resolvable private address: 46 random bits, neither all zeros
 * nor all ones, with the top two bits clear. bdAddr_t is little-endian, so
 * the top bits are in addr[5].
 */
static void _NrpaGenerate(bdAddr_t addr)
{
	const uint8_t zeros[BDA_ADDR_LEN - 1] = { 0 };
	const uint8_t ones[BDA_ADDR_LEN - 1] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	uint8_t top;

	do {
		PalCryptoGenerateRandomNumber(addr, BDA_ADDR_LEN);
		DM_RAND_ADDR_SET(addr, DM_RAND_ADDR_NONRESOLV);
		top = addr[BDA_ADDR_LEN - 1];
	} while (((top == 0x00U) && (memcmp(addr, zeros, sizeof(zeros)) == 0)) ||
		 ((top == 0x3FU) && (memcmp(addr, ones, sizeof(ones)) == 0)));
}

/* Build a fresh Hubble payload wrapped in the two AD structures. */
static int _AdvDataBuild(void)
{
	uint8_t out[HUBBLE_BLE_ADV_HEADER_SIZE + HUBBLE_BLE_MAX_DATA_LEN];
	size_t payload_len = sizeof(out);
	uint8_t *p = gAdvData;
	int ret;

#if HUBBLE_USER_PAYLOAD_LEN > 0
	uint8_t user_payload[HUBBLE_USER_PAYLOAD_LEN] = { 0 };

	/* Application data goes in user_payload[] here. */

	ret = hubble_ble_advertise_get(user_payload, sizeof(user_payload), out,
				       &payload_len);
#else
	ret = hubble_ble_advertise_get(NULL, 0U, out, &payload_len);
#endif
	if (ret != 0) {
		HUBBLE_LOG_ERROR("advertise_get failed: %d", ret);
		return ret;
	}

	/* Complete list of 16-bit service UUIDs, holding 0xFCA6. */
	*p++ = 3U;
	*p++ = AD_TYPE_COMPLETE_LIST_16_BIT_UUID;
	*p++ = (uint8_t)(HUBBLE_BLE_UUID & 0xFFU);
	*p++ = (uint8_t)((HUBBLE_BLE_UUID >> 8) & 0xFFU);

	/* Service Data. The SDK's output already starts with the UUID. */
	*p++ = (uint8_t)(payload_len + 1U);
	*p++ = AD_TYPE_SERVICE_16_BIT_DATA;
	memcpy(p, out, payload_len);
	p += payload_len;

	gAdvDataLen = (uint8_t)(p - gAdvData);

	return 0;
}

/* Install the advertisement and advertise until stopped. */
static void _AdvStart(void)
{
	uint8_t advHandle = DM_ADV_HANDLE_DEFAULT;
	uint16_t advDuration = 0U;
	uint8_t maxEaEvents = 0U;
	bdAddr_t nrpa_addr;

	if (_AdvDataBuild() != 0) {
		return;
	}

	/* Host must generate its own non-resolvable private addresses. */
	_NrpaGenerate(nrpa_addr);
	DmDevSetRandAddr(nrpa_addr);

	DmAdvSetData(DM_ADV_HANDLE_DEFAULT, HCI_ADV_DATA_OP_COMP_FRAG,
		     DM_DATA_LOC_ADV, gAdvDataLen, gAdvData);

	/* The result arrives as DM_ADV_START_IND. */
	DmAdvStart(1U, &advHandle, &advDuration, &maxEaEvents);
}

static void _AdvRefresh(void)
{
	uint8_t advHandle = DM_ADV_HANDLE_DEFAULT;

	if (!gAdvRunning) {
		/* Never started, or stopped: try again. */
		if (gDmReady) {
			_AdvStart();
		}
		return;
	}

	gAdvRunning = false;
	DmAdvStop(1U, &advHandle);
}

/* Forward DM events to _HubbleHandler(). */
static void _DmCback(dmEvt_t *pDmEvt)
{
	dmEvt_t *pMsg;
	uint16_t len = DmSizeOfEvt(pDmEvt);

	if ((pMsg = WsfMsgAlloc(len)) == NULL) {
		HUBBLE_LOG_ERROR("no buffer for DM event %u; dropped",
				 (unsigned int)pDmEvt->hdr.event);
		return;
	}

	memcpy(pMsg, pDmEvt, len);
	WsfMsgSend(gHubbleHandlerId, pMsg);
}

static void _HubbleHandler(wsfEventMask_t event, wsfMsgHdr_t *pMsg)
{
	(void)event;

	if (pMsg == NULL) {
		return;
	}

	switch (pMsg->event) {
	case HUBBLE_ADV_REFRESH_MSG:
		WsfTimerStartSec(&gRefreshTimer, HUBBLE_ADV_REFRESH_PERIOD_S);
		_AdvRefresh();
		break;

	case DM_RESET_CMPL_IND:
		gDmReady = true;
		DmAdvSetAddrType(DM_ADDR_RANDOM);
		/* DM_ADDR_PUBLIC here is the peer address type; ignored by undirected adv. */
		DmAdvConfig(DM_ADV_HANDLE_DEFAULT, DM_ADV_NONCONN_UNDIRECT,
			    DM_ADDR_PUBLIC, NULL);
		DmAdvSetInterval(DM_ADV_HANDLE_DEFAULT,
				 HUBBLE_ADV_INTERVAL_0625MS,
				 HUBBLE_ADV_INTERVAL_0625MS);
		_AdvStart();
		break;

	case DM_ADV_START_IND:
		if (pMsg->status != HCI_SUCCESS) {
			/* The next refresh retries. */
			HUBBLE_LOG_ERROR("advertising start failed (0x%02x)",
					 (unsigned int)pMsg->status);
			break;
		}

		gAdvRunning = true;
		HUBBLE_LOG_INFO("Hubble beaconing started");
		break;

	case DM_ADV_STOP_IND:
		gAdvRunning = false;
		_AdvStart();
		break;

	default:
		break;
	}
}

/* Link layer: platform defaults, narrowed to what a beacon uses. */
static void _InitLl(void)
{
	bdAddr_t bdAddr;
	uint32_t memAvail = WsfHeapCountAvailable();
	uint32_t memUsed;

	PalBbLoadCfg((PalBbCfg_t *)&gBbRtCfg);
	LlGetDefaultRunTimeCfg(&gLlRtCfg);

	gLlRtCfg.maxConn = 0U;
	gLlRtCfg.numTxBufs = LL_RT_CFG_NUM_TX_BUFS;
	gLlRtCfg.numRxBufs = LL_RT_CFG_NUM_RX_BUFS;
	gLlRtCfg.maxAdvReports = LL_RT_CFG_MAX_ADV_REPORTS;

	LlInitRtCfg_t llCfg = {
		.pBbRtCfg = &gBbRtCfg,
		.wlSizeCfg = 4,
		.rlSizeCfg = 4,
		.plSizeCfg = 4,
		.pLlRtCfg = &gLlRtCfg,
		.pFreeMem = WsfHeapGetFreeStartAddress(),
		.freeMemAvail = memAvail
	};

	memUsed = LlInit(&llCfg);
	if (memUsed > memAvail) {
		HUBBLE_LOG_ERROR("link layer needs %u B of heap, %u available",
				 (unsigned int)memUsed, (unsigned int)memAvail);
		return;
	}
	WsfHeapAlloc(memUsed);

	/* The public address, from the part's NVM information page. The
	 * beacon advertises from an NRPA instead, so this is never on the
	 * air; without it the controller keeps its default address. */
	if (!PalCfgLoadData((uint8_t)PAL_CFG_ID_BD_ADDR, bdAddr,
			    sizeof(bdAddr_t))) {
		HUBBLE_LOG_ERROR("no BD address in NVM info page");
	} else {
		LlSetBdAddr((uint8_t *)bdAddr);
	}
}

/* Runs on the WSF task, via WsfOsRegisterTaskInitCback(). */
static void _BeaconInit(void)
{
	wsfHandlerId_t handlerId;
	uint32_t memUsed;
	int ret;

#if defined(CONFIG_HUBBLE_COUNTER_SOURCE_UNIX_TIME)
	uint64_t initial_time = unix_time;
#else
	uint64_t initial_time = 0U;
#endif

	PalSysInit();

	_InitLl();

	memUsed = WsfBufInit(
		(uint8_t)(sizeof(gcBuffPoolDesc) / sizeof(gcBuffPoolDesc[0])),
		(wsfBufPoolDesc_t *)&gcBuffPoolDesc[0]);
	WsfHeapAlloc(memUsed);

	WsfTimerInit();

	/* Before DM, so gHubbleHandlerId is valid when it calls back. */
	gHubbleHandlerId = WsfOsSetNextHandler(_HubbleHandler);

	gRefreshTimer.handlerId = gHubbleHandlerId;
	gRefreshTimer.msg.event = HUBBLE_ADV_REFRESH_MSG;
	WsfTimerStartSec(&gRefreshTimer, HUBBLE_ADV_REFRESH_PERIOD_S);

	RTC_Enable();

	handlerId = WsfOsSetNextHandler(HciHandler);
	HciHandlerInit(handlerId);

	/* Advertising only: no connections, security or privacy. */
	handlerId = WsfOsSetNextHandler(DmHandler);
	DmDevVsInit(0);
	DmAdvInit();
	DmHandlerInit(handlerId);
	DmRegister(_DmCback);

	ret = hubble_init(initial_time, master_key);
	if (ret != 0) {
		HUBBLE_LOG_ERROR("hubble_init failed: %d", ret);
		return;
	}

	/* Advertising starts on DM_RESET_CMPL_IND. */
	DmDevReset();
}

void HubbleBeacon_TaskCreate(void)
{
	WsfOsInitOnly();
}

void HubbleBeacon_TaskStart(void)
{
	WsfOsRegisterTaskInitCback(_BeaconInit);

	WsfOsStartOnly();
}
