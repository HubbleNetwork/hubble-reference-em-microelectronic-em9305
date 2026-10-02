/*
 * Copyright (c) 2026 Hubble Network, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _BEACON_H_
#define _BEACON_H_

#include <hubble/port/crypto.h>

/* Seconds between advertisement refreshes. */
#define HUBBLE_ADV_REFRESH_PERIOD_S 300U

/* Ensure we don't run out of EIDs based on refresh rate. */
#if (HUBBLE_ADV_REFRESH_PERIOD_S * (HUBBLE_MAX_SEQ_COUNTER + 1)) < \
	CONFIG_HUBBLE_EID_ROTATION_PERIOD_SEC
#error "HUBBLE_ADV_REFRESH_PERIOD_S runs out of sequence numbers within one EID rotation period"
#endif

/* Advertising interval in 0.625 ms units: 1600 = 1 s. */
#define HUBBLE_ADV_INTERVAL_0625MS 1600U

/* User payload bytes per advertisement. */
#define HUBBLE_USER_PAYLOAD_LEN 0U

void HubbleBeacon_TaskCreate(void);
void HubbleBeacon_TaskStart(void);

#endif /* _BEACON_H_ */
