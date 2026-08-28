// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright (c) 2026 FoBE Studio
 */

#ifndef FOBE_TESTS_LIB_MESHCORE_MODULE_DISPATCHER_MESH_SRC_TEST_REFERENCE_ENV_H_
#define FOBE_TESTS_LIB_MESHCORE_MODULE_DISPATCHER_MESH_SRC_TEST_REFERENCE_ENV_H_

#include "test_support.h"

namespace meshcore_dispatcher_mesh_tdd {

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

class CommonRng : public mesh::RNG {
public:
	void random(uint8_t *dest, size_t sz) override;
};

class CommonRtcClock : public mesh::RTCClock {
public:
	uint32_t getCurrentTime() override;
	void setCurrentTime(uint32_t time) override;
};

class ReferenceHarness : public mesh::Mesh {
public:
	ReferenceHarness(mesh::Radio &radio, mesh::MillisecondClock &clock,
			 mesh::RNG &rng, mesh::RTCClock &rtc,
			 mesh::PacketManager &manager, mesh::MeshTables &tables,
			 MeshScript *script);

	uint16_t getErrFlags() const;

protected:
	bool filterRecvFloodPacket(mesh::Packet *packet) override;
	bool allowPacketForward(const mesh::Packet *packet) override;
	uint32_t getRetransmitDelay(const mesh::Packet *packet) override;
	uint32_t getDirectRetransmitDelay(const mesh::Packet *packet) override;
	uint8_t getExtraAckTransmitCount() const override;
	uint32_t getCADFailRetryDelay() const override;
	int searchPeersByHash(const uint8_t *hash) override;
	void getPeerSharedSecret(uint8_t *dest_secret, int peer_idx) override;
	void onPeerDataRecv(mesh::Packet *packet, uint8_t type, int sender_idx,
			    const uint8_t *secret, uint8_t *data,
			    size_t len) override;
	void onTraceRecv(mesh::Packet *packet, uint32_t tag, uint32_t auth_code,
			 uint8_t flags, const uint8_t *path_snrs,
			 const uint8_t *path_hashes, uint8_t path_len) override;
	bool onPeerPathRecv(mesh::Packet *packet, int sender_idx,
			    const uint8_t *secret, uint8_t *path,
			    uint8_t path_len, uint8_t extra_type, uint8_t *extra,
			    uint8_t extra_len) override;
	void onAdvertRecv(mesh::Packet *packet, const mesh::Identity &id,
			  uint32_t timestamp, const uint8_t *app_data,
			  size_t app_data_len) override;
	void onAnonDataRecv(mesh::Packet *packet, const uint8_t *secret,
			    const mesh::Identity &sender, uint8_t *data,
			    size_t len) override;
	void onControlDataRecv(mesh::Packet *packet) override;
	void onRawDataRecv(mesh::Packet *packet) override;
	int searchChannelsByHash(const uint8_t *hash, mesh::GroupChannel channels[],
				 int max_matches) override;
	void onGroupDataRecv(mesh::Packet *packet, uint8_t type,
			     const mesh::GroupChannel &channel, uint8_t *data,
			     size_t len) override;
	void onAckRecv(mesh::Packet *packet, uint32_t ack_crc) override;

private:
	MeshScript *script_;
};

struct ReferenceEnv {
	MeshScript script;
	CommonFakeRadio radio;
	CommonMillisecondClock clock;
	CommonRng rng;
	CommonRtcClock rtc;
	StaticPoolPacketManager manager;
	SimpleMeshTables tables;
	ReferenceHarness mesh;

	ReferenceEnv();
};

void *reference_env_suite_setup(void);
void reference_env_before_each(void *fixture);
ReferenceEnv &reference_env_get(void);

JointSnapshot capture_reference_snapshot(ReferenceEnv &env);

} // namespace meshcore_dispatcher_mesh_tdd

#endif /* FOBE_TESTS_LIB_MESHCORE_MODULE_DISPATCHER_MESH_SRC_TEST_REFERENCE_ENV_H_ */
