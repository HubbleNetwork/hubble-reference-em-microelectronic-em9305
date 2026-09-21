/*
 * Copyright (c) 2026 Hubble Network, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Hubble port for EM9305. */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include <printf.h>
#include <printfbleu.h>
#include <rtc.h>

#include <hubble/port/sys.h>

/* Message text, before the timestamp and level. */
#define HUBBLE_LOG_LINE_MAX 96

static int32_t _rtc_ms_get(uint64_t *ms)
{
	RTC_RefTimeCounter_t now;
	int32_t ret = RTC_GetTime(&now);

	if (ret != 0) {
		return ret;
	}

	/* Time stored as 32.8 fixed point - 32 bits for seconds, 8 for subSeconds */
	*ms = ((uint64_t)now.time.seconds * 1000U) +
	      (((uint64_t)now.time.subSeconds * 1000U) >> 8);

	return 0;
}

uint64_t hubble_uptime_get(void)
{
	uint64_t ms = 0U;
	int32_t ret = _rtc_ms_get(&ms);

	if (ret != 0) {
		HUBBLE_LOG_ERROR("RTC_GetTime failed (err=%d); uptime reads as 0",
				 (int)ret);
	}

	return ms;
}

int hubble_log(enum hubble_log_level level, const char *format, ...)
{
	static const char *const tag[HUBBLE_LOG_COUNT] = { "dbg", "inf", "wrn",
							   "err" };
	char buf[HUBBLE_LOG_LINE_MAX];
	va_list args;
	uint64_t ms;
	bool ms_valid;
	int written;

	if ((unsigned int)level >= (unsigned int)HUBBLE_LOG_COUNT) {
		return -1;
	}

	if ((unsigned int)level < (unsigned int)HUBBLE_LOG_MIN_LEVEL) {
		return 0;
	}

	/* Don't call hubble_uptime_get() - it could recurse as it logs on failure.*/
	ms_valid = (_rtc_ms_get(&ms) == 0);

	va_start(args, format);
	written = vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	if (written <= 0) {
		return written;
	}

	/* Appears on the DVK's USB serial port; `hubble_builder.py log` shows it. */
	if (ms_valid) {
		PrintfBleuMessage(PRINTFBLEU_DEBUG_LEVEL_NONE, "[%u] <%s> %s",
				  (uint32_t)ms, tag[level], buf);
	} else {
		PrintfBleuMessage(PRINTFBLEU_DEBUG_LEVEL_NONE,
				  "[UPTIME UNKNOWN] <%s> %s", tag[level], buf);
	}

	return written;
}

/*
 * No-ops: every Hubble SDK call runs on the single WSF task. If one is ever
 * made from an ISR, this needs a real (recursive, blocking) mutex.
 */
int hubble_lock_init(void)
{
	return 0;
}

void hubble_lock(void)
{
}

void hubble_unlock(void)
{
}
