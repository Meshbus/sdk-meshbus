#ifndef FOBE_TESTS_LIB_MESHCORE_COMMON_INCLUDE_ARDUINO_H_
#define FOBE_TESTS_LIB_MESHCORE_COMMON_INCLUDE_ARDUINO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t byte;
typedef bool boolean;

#ifdef __cplusplus
template <typename T>
static inline T constrain(T value, T low, T high)
{
	if (value < low) {
		return low;
	}
	if (value > high) {
		return high;
	}
	return value;
}
extern "C" {
#endif

static inline char *ltoa(long value, char *str, int base)
{
	char buf[65];
	char *out = str;
	unsigned long magnitude;
	size_t pos = 0U;

	if (str == NULL || base < 2 || base > 36) {
		return str;
	}

	if (value < 0 && base == 10) {
		*out++ = '-';
		magnitude = (unsigned long)(-value);
	} else {
		magnitude = (unsigned long)value;
	}

	do {
		unsigned long digit = magnitude % (unsigned long)base;

		buf[pos++] = (char)(digit < 10U ? ('0' + digit) : ('a' + (digit - 10U)));
		magnitude /= (unsigned long)base;
	} while (magnitude != 0U);

	while (pos > 0U) {
		*out++ = buf[--pos];
	}
	*out = '\0';
	return str;
}

#ifdef __cplusplus
}
#endif

#endif /* FOBE_TESTS_LIB_MESHCORE_COMMON_INCLUDE_ARDUINO_H_ */
