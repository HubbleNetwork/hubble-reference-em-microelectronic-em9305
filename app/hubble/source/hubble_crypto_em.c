/*
 * Copyright (c) 2026 Hubble Network, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * CTR and CMAC are both from the EM9305 SDK.
 * The crypto hardware only supports 128-bit AES.
 * Using 256-bit keys uses an internal software engine.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <AES.h>
#include <AES_CMAC.h>
#include <AES_CTR.h>

#include <hubble/port/crypto.h>

/* The AES hardware is 128-bit only; 256-bit keys use the software engine. */
#if CONFIG_HUBBLE_KEY_SIZE == 16
#define _AES_ENGINE   AES_HARDWARE
#define _AES_KEY_BITS AES_KEY_128
#elif CONFIG_HUBBLE_KEY_SIZE == 32
#define _AES_ENGINE   AES_SOFTWARE
#define _AES_KEY_BITS AES_KEY_256
#else
#error "CONFIG_HUBBLE_KEY_SIZE must be 16 or 32"
#endif

_Static_assert(HUBBLE_AES_BLOCK_SIZE == AES_BLOCK_SIZE_BYTE,
	       "Hubble and EM9305 disagree on the AES block size");

void hubble_crypto_zeroize(void *buf, size_t len)
{
	volatile uint8_t *p = (volatile uint8_t *)buf;

	while (len-- > 0U) {
		*p++ = 0U;
	}
}

int hubble_crypto_init(void)
{
	return 0;
}

int hubble_crypto_aes_ctr(const uint8_t key[CONFIG_HUBBLE_KEY_SIZE],
			  uint8_t nonce_counter[HUBBLE_NONCE_BUFFER_SIZE],
			  const uint8_t *data, size_t len, uint8_t *output)
{
	AES_CTR_CTX ctx;
	int ret = 0;

	/* No user payload: nothing to encrypt, and data may be NULL. */
	if (len == 0U) {
		return 0;
	}

	if (AES_CTR_InitCtx(&ctx, _AES_ENGINE, AES_CTR_KEY_VALUE,
			    _AES_KEY_BITS, nonce_counter,
			    (uint8_t *)key) != AES_CTR_SUCCESS) {
		ret = -1;
		goto out;
	}

	if (AES_CTR_Encrypt(&ctx, (uint8_t *)data, output, (uint32_t)len) !=
	    AES_CTR_SUCCESS) {
		ret = -1;
		goto out;
	}

out:
	/* The context holds a copy of the key and the last key stream block. */
	hubble_crypto_zeroize(&ctx, sizeof(ctx));

	return ret;
}

int hubble_crypto_cmac(const uint8_t key[CONFIG_HUBBLE_KEY_SIZE],
		       const uint8_t *data, size_t len,
		       uint8_t output[HUBBLE_AES_BLOCK_SIZE])
{
	AES_CMAC_CTX ctx;
	int ret = 0;

	/* An empty message (no user payload) is valid CMAC; Compute() is still
	 * called for it. */
	if (AES_CMAC_InitCtx(&ctx, _AES_ENGINE, AES_CMAC_KEY_VALUE,
			     _AES_KEY_BITS,
			     (uint8_t *)key) != AES_CMAC_SUCCESS) {
		/* A failed init may still have copied the key. */
		ret = -1;
		goto out;
	}

	if (AES_CMAC_Compute(&ctx, (uint8_t *)data, (uint32_t)len) !=
	    AES_CMAC_SUCCESS) {
		ret = -1;
		goto out;
	}

	if (AES_CMAC_GetMAC(&ctx, output, HUBBLE_AES_BLOCK_SIZE) !=
	    AES_CMAC_SUCCESS) {
		ret = -1;
		goto out;
	}

out:
	hubble_crypto_zeroize(&ctx, sizeof(ctx));

	return ret;
}
