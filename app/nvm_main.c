/*
 * Copyright (c) 2026 Hubble Network, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Entry points. The boot ROM calls NVM_ConfigModules() before any hardware
 * is up, then NVM_ApplicationEntry(), which never returns.
 */

#include <bsp.h>
#include <em_terminal_cmd.h>
#include <em_transport_manager.h>
#include <gpio.h>
#include <interrupts.h>
#include <nvm_entry.h> /* Emits the firmware header. */
#include <pml.h>
#include <radio.h>
#include <rtc.h>
#include <spi_slave.h>
#include <types.h>
#include <wsf_os.h>

#include <beacon.h>

/* Buffers in the terminal handler that carries hubble_log(). */
#define LOG_BUF_SIZE 128U
#define LOG_BUF_NUM  4U

/*
 * Required: WSF's QP/C port allocates an event per WsfSetEvent() and
 * WsfMsgSend(). Not retained across sleep, so it is rebuilt on every boot.
 */
static SECTION_NP_NOINIT uint8_t
	gQpcEventPool[NUM_WSF_TASK_EVENTS * sizeof(QEventParams)];

void NVM_ConfigModules(void)
{
	RF_RegisterModule();

	/* Module configuration is consumed during module init, which happens
	 * before the application runs, so it must be set here. */
	RTC_RegisterModule();
	gRTC_Config.sleepTimerChannel = SLEEP_TIMER_CHANNEL_3;

	/* hubble_log() goes to the DVK over the SPI slave (GPIO 0-5), the same
	 * link BLEngine flashes through. */
	gConfig.pTransport = &gSPIS_Transport;
	SPIS_RegisterModule();

	if (PML_DidBootFromSleep()) {
		/* QP/C is still initialised; let the pool be re-registered. */
		extern uint_fast8_t QF_maxPool_;
		QF_maxPool_ = (uint_fast8_t)0;
	} else {
		GPIO_LoadDefaultConfigSPI();
		QF_init();
	}

	QF_bzero(&gQpcEventPool[0], sizeof(gQpcEventPool));
	QF_poolInit(&gQpcEventPool[0], sizeof(gQpcEventPool),
		    sizeof(QEventParams));
}

NO_RETURN void NVM_ApplicationEntry(void)
{
	BSP_Init();
	IRQ_EnableInterrupts();

	if (PML_DidBootFromSleep()) {
		EM_TerminalHandlerResume();

		/* Resume rather than re-init, which would reset the Hubble
		 * sequence counter and reuse nonces. */
		(void)QF_resume();
	} else {
		EM_TerminalHandler_Init_With_Buffer_Sizes(
			NULL, LOG_BUF_SIZE, LOG_BUF_NUM, LOG_BUF_SIZE,
			LOG_BUF_NUM);

		HubbleBeacon_TaskCreate();
		HubbleBeacon_TaskStart();

		(void)QF_run();
	}

	while (true) {
		HaltCPU();
	}
}

/* QP/C idle hook: hand queued log messages to the transport. */
void QK_onIdleExt(void)
{
	uint32_t irq = IRQ_DisableAndSaveInterrupts();

	irq = EMTransportManager_Process(irq);
	IRQ_RestoreInterrupts(irq);
}
