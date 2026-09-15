/* SPDX-License-Identifier: Apache-2.0 */

#include <cstdio>
#include "environment.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <DeviceInfoProviderImpl.h>
#include <app/clusters/network-commissioning/network-commissioning.h>
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <data-model-providers/codegen/Instance.h>
#include <lib/support/CHIPMem.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/Zephyr/wifi/ZephyrWifiDriver.h>
#include <setup_payload/OnboardingCodesUtil.h>

LOG_MODULE_REGISTER(matter_temperature, LOG_LEVEL_INF);

namespace {
/* User-wired devkit_esp32c6 GP9 button, active low. */
const gpio_dt_spec pairing_button{DEVICE_DT_GET(DT_NODELABEL(gpio0)), 9,
	GPIO_ACTIVE_LOW | GPIO_PULL_UP};
K_SEM_DEFINE(server_ready, 0, 1);
CHIP_ERROR server_result = CHIP_ERROR_INCORRECT_STATE;
chip::CommonCaseDeviceServerInitParams server_params;
chip::app::Clusters::NetworkCommissioning::Instance network_commissioning(
	0, &chip::DeviceLayer::NetworkCommissioning::ZephyrWifiDriver::Instance());

void open_pairing_window(intptr_t)
{
	auto &window = chip::Server::GetInstance().GetCommissioningWindowManager();
	if (window.IsCommissioningWindowOpen()) {
		LOG_INF("Pairing window is already open");
		return;
	}
	CHIP_ERROR err = window.OpenBasicCommissioningWindow(chip::System::Clock::Seconds32(900));
	if (err != CHIP_NO_ERROR) {
		LOG_ERR("Cannot open pairing window: %" CHIP_ERROR_FORMAT, err.Format());
	} else {
		LOG_INF("Pairing window open for 15 minutes; code 34970112332");
	}
}

void init_server(intptr_t)
{
	using namespace chip;

	std::printf("MATTER: initializing server\n");
	Credentials::SetDeviceAttestationCredentialsProvider(
		Credentials::Examples::GetExampleDACProvider());
	server_result = server_params.InitializeStaticResourcesBeforeServerInit();
	if (server_result == CHIP_NO_ERROR) {
		auto &device_info = DeviceLayer::DeviceInfoProviderImpl::GetDefaultInstance();

		device_info.SetStorageDelegate(server_params.persistentStorageDelegate);
		DeviceLayer::SetDeviceInfoProvider(&device_info);
		server_params.dataModelProvider =
			app::CodegenDataModelProviderInstance(server_params.persistentStorageDelegate);
		server_result = Server::GetInstance().Init(server_params);
		std::printf("MATTER: Server::Init=%" CHIP_ERROR_FORMAT "\n", server_result.Format());
	}
	if (server_result == CHIP_NO_ERROR) {
		server_result = network_commissioning.Init();
		std::printf("MATTER: network commissioning=%" CHIP_ERROR_FORMAT "\n", server_result.Format());
	}
	if (server_result == CHIP_NO_ERROR) {
		server_result = environment_init();
	}
	if (server_result == CHIP_NO_ERROR) {
		std::printf("MATTER: environment clusters ready\n");
		PrintOnboardingCodes(RendezvousInformationFlags(RendezvousInformationFlag::kBLE));
		LOG_INF("Matter server ready; fabrics=%u", Server::GetInstance().GetFabricTable().FabricCount());
	}
	std::printf("MATTER: signaling main\n");
	k_sem_give(&server_ready);
}
} // namespace

int main(void)
{
	using namespace chip;

	std::printf("MATTER: starting platform\n");
	CHIP_ERROR err = Platform::MemoryInit();
	if (err == CHIP_NO_ERROR) {
		err = DeviceLayer::PlatformMgr().InitChipStack();
	}
	if (err == CHIP_NO_ERROR) {
		err = DeviceLayer::PlatformMgr().ScheduleWork(init_server);
	}
	if (err == CHIP_NO_ERROR) {
		err = DeviceLayer::PlatformMgr().StartEventLoopTask();
	}
	if (err != CHIP_NO_ERROR) {
		LOG_ERR("Matter startup failed: %" CHIP_ERROR_FORMAT, err.Format());
		return -EIO;
	}
	std::printf("MATTER: waiting for server\n");
	k_sem_take(&server_ready, K_FOREVER);
	std::printf("MATTER: server initialization returned\n");
	if (server_result != CHIP_NO_ERROR) {
		LOG_ERR("Matter server failed: %" CHIP_ERROR_FORMAT, server_result.Format());
		return -EIO;
	}

	bool button_ready = gpio_is_ready_dt(&pairing_button) &&
		gpio_pin_configure_dt(&pairing_button, GPIO_INPUT) == 0;
	if (!button_ready) {
		LOG_WRN("Pairing button unavailable");
	}
	int previous_button = 0;
	int64_t next_sample = 0;
	for (;;) {
		if (button_ready) {
			int pressed = gpio_pin_get_dt(&pairing_button);
			if (pressed == 1 && previous_button == 0) {
				k_msleep(30);
				if (gpio_pin_get_dt(&pairing_button) == 1) {
					err = DeviceLayer::PlatformMgr().ScheduleWork(open_pairing_window);
					if (err != CHIP_NO_ERROR) {
						LOG_ERR("Pairing request failed: %" CHIP_ERROR_FORMAT, err.Format());
					}
				}
			}
			if (pressed >= 0) {
				previous_button = pressed;
			}
		}
		if (k_uptime_get() >= next_sample) {
			environment_update();
			next_sample = k_uptime_get() + 5000;
		}
		k_sleep(K_MSEC(50));
	}
}
