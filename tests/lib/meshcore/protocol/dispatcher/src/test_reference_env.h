// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#ifndef FOBE_TESTS_LIB_MESHCORE_MODULE_DISPATCHER_SRC_REFERENCE_ENV_H_
#define FOBE_TESTS_LIB_MESHCORE_MODULE_DISPATCHER_SRC_REFERENCE_ENV_H_

#include "test_support.h"

extern "C" {
#include <zephyr/ztest.h>
}

namespace meshcore_dispatcher_tdd {

class CommonMillisecondClock : public mesh::MillisecondClock {
public:
	unsigned long getMillis() override;
};

class CommonFakeRadio : public mesh::Radio {
public:
	void begin() override;
	int recvRaw(uint8_t *bytes, int sz) override;
	uint32_t getEstAirtimeFor(int len_bytes) override;
	float packetScore(float snr, int packet_len) override;
	bool startSendRaw(const uint8_t *bytes, int len) override;
	bool isSendComplete() override;
	void onSendFinished() override;
	void loop() override;
	void triggerNoiseFloorCalibrate(int threshold) override;
	void resetAGC() override;
	bool isInRecvMode() const override;
	bool isReceiving() override;
	float getLastRSSI() const override;
	float getLastSNR() const override;
};

class ReferenceHarness : public mesh::Dispatcher {
public:
	ReferenceHarness(mesh::Radio &radio, mesh::MillisecondClock &clock,
			 mesh::PacketManager &manager,
			 DispatcherInputScript *script);

	uint16_t getErrFlags() const;

protected:
	mesh::DispatcherAction onRecvPacket(mesh::Packet *packet) override;
	void logRxRaw(float, float, const uint8_t[], int) override;
	void logRx(mesh::Packet *, int, float) override;
	void logTx(mesh::Packet *, int) override;
	void logTxFail(mesh::Packet *, int) override;
	const char *getLogDateTime() override;
	float getAirtimeBudgetFactor() const override;
	int calcRxDelay(float, uint32_t) const override;
	uint32_t getCADFailRetryDelay() const override;
	uint32_t getCADFailMaxDuration() const override;
	int getInterferenceThreshold() const override;
	int getAGCResetInterval() const override;
	unsigned long getDutyCycleWindowMs() const override;

private:
	DispatcherInputScript *script_;
};

struct ReferenceEnv {
	DispatcherInputScript script;
	CommonFakeRadio radio;
	CommonMillisecondClock clock;
	StaticPoolPacketManager manager;
	ReferenceHarness dispatcher;

	ReferenceEnv();
};

void *reference_env_suite_setup(void);
void reference_env_before_each(void *fixture);
ReferenceEnv &reference_env_get(void);
DispatcherSnapshot capture_reference_snapshot(ReferenceEnv &env);

} // namespace meshcore_dispatcher_tdd

#endif /* FOBE_TESTS_LIB_MESHCORE_MODULE_DISPATCHER_SRC_REFERENCE_ENV_H_ */
