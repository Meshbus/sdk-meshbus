// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include "RNG.h"

#include <zephyr/sys/util.h>

/*
 * Minimal RNG linkage shim for the upstream Crypto sources.
 *
 * The oracle-based identity tests drive randomness via:
 * - FixedRNG in main.cpp for the C++ MeshCore path
 * - meshcore_hal_test_rng_*() for the C path
 *
 * The upstream Crypto/Ed25519 sources still reference the global RNG symbol
 * declared in Crypto/RNG.h, so this file provides the smallest non-Arduino
 * implementation needed to satisfy that dependency during linking.
 */
RNGClass RNG;

RNGClass::RNGClass()
{
}

RNGClass::~RNGClass()
{
}

void RNGClass::begin(const char *tag)
{
	ARG_UNUSED(tag);
}

void RNGClass::addNoiseSource(NoiseSource &source)
{
	ARG_UNUSED(source);
}

void RNGClass::setAutoSaveTime(uint16_t minutes)
{
	ARG_UNUSED(minutes);
}

void RNGClass::rand(uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		data[i] = 0U;
	}
}

bool RNGClass::available(size_t len) const
{
	ARG_UNUSED(len);
	return true;
}

void RNGClass::stir(const uint8_t *data, size_t len, unsigned int credit)
{
	ARG_UNUSED(data);
	ARG_UNUSED(len);
	ARG_UNUSED(credit);
}

void RNGClass::save()
{
}

void RNGClass::loop()
{
}

void RNGClass::destroy()
{
}

void RNGClass::rekey()
{
}

void RNGClass::mixTRNG()
{
}
