// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#include <CayenneLPPPolyline.h>

CayenneLPPPolyline::CayenneLPPPolyline(uint32_t size) : m_maxSize(size) {}

std::vector<uint8_t> CayenneLPPPolyline::encode(const std::vector<Point> &coords,
						uint8_t factor,
						Simplification simplification)
{
	(void)coords;
	(void)factor;
	(void)simplification;
	return {};
}

std::vector<uint8_t> CayenneLPPPolyline::encode(const std::vector<Point> &coords,
						Precision precision,
						Simplification simplification)
{
	(void)coords;
	(void)precision;
	(void)simplification;
	return {};
}

std::vector<std::pair<double, double>>
CayenneLPPPolyline::decode(const std::vector<uint8_t> &buffer)
{
	(void)buffer;
	return {};
}

CayenneLPPPolyline::Stats CayenneLPPPolyline::getEncodeStats() const
{
	return {};
}
