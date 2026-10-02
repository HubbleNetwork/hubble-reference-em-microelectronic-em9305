/*
 * Copyright (c) 2026 Hubble Network, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Hubble Device SDK configuration, in place of Kconfig. app/CMakeLists.txt
 * applies it to every translation unit with -imacros.
 */

#ifndef _HUBBLE_CONFIG_H_
#define _HUBBLE_CONFIG_H_

#define CONFIG_HUBBLE_BLE_NETWORK 1

/* 128-bit uses the AES hardware; 256-bit also works, in software. */
#define CONFIG_HUBBLE_NETWORK_KEY_128 1

/* Implemented by hubble/source/hubble_crypto_em.c. */
#define CONFIG_HUBBLE_NETWORK_CRYPTO_CUSTOM 1

#define CONFIG_HUBBLE_COUNTER_SOURCE_UNIX_TIME 1

#define CONFIG_HUBBLE_EID_ROTATION_PERIOD_SEC 86400

#define CONFIG_HUBBLE_NETWORK_SECURITY_ENFORCE_NONCE_CHECK 1

/* Define to compile out all logging. */
/* #define CONFIG_HUBBLE_LOG_LEVEL_OFF 1 */

/* Lowest level logged: 0 debug, 1 info, 2 warning, 3 error. */
#ifndef HUBBLE_LOG_MIN_LEVEL
#define HUBBLE_LOG_MIN_LEVEL 0
#endif

#endif /* _HUBBLE_CONFIG_H_ */
