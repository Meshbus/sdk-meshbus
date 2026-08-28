/*
 * Copyright (c) 2025 FoBE Projects
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MFD_TCA8418_H_
#define ZEPHYR_INCLUDE_DRIVERS_MFD_TCA8418_H_

#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/slist.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TCA8418 Register Addresses
 */
#define TCA8418_REG_CFG             0x01
#define TCA8418_REG_INT_STAT        0x02
#define TCA8418_REG_KEY_LCK_EC      0x03
#define TCA8418_REG_KEY_EVENT_A     0x04
#define TCA8418_REG_KEY_EVENT_B     0x05
#define TCA8418_REG_KEY_EVENT_C     0x06
#define TCA8418_REG_KEY_EVENT_D     0x07
#define TCA8418_REG_KEY_EVENT_E     0x08
#define TCA8418_REG_KEY_EVENT_F     0x09
#define TCA8418_REG_KEY_EVENT_G     0x0A
#define TCA8418_REG_KEY_EVENT_H     0x0B
#define TCA8418_REG_KEY_EVENT_I     0x0C
#define TCA8418_REG_KEY_EVENT_J     0x0D
#define TCA8418_REG_KP_LCK_TIMER    0x0E
#define TCA8418_REG_UNLOCK1         0x0F
#define TCA8418_REG_UNLOCK2         0x10
#define TCA8418_REG_GPIO_INT_STAT1  0x11
#define TCA8418_REG_GPIO_INT_STAT2  0x12
#define TCA8418_REG_GPIO_INT_STAT3  0x13
#define TCA8418_REG_GPIO_DAT_STAT1  0x14
#define TCA8418_REG_GPIO_DAT_STAT2  0x15
#define TCA8418_REG_GPIO_DAT_STAT3  0x16
#define TCA8418_REG_GPIO_DAT_OUT1   0x17
#define TCA8418_REG_GPIO_DAT_OUT2   0x18
#define TCA8418_REG_GPIO_DAT_OUT3   0x19
#define TCA8418_REG_GPIO_INT_EN1    0x1A
#define TCA8418_REG_GPIO_INT_EN2    0x1B
#define TCA8418_REG_GPIO_INT_EN3    0x1C
#define TCA8418_REG_KP_GPIO1        0x1D
#define TCA8418_REG_KP_GPIO2        0x1E
#define TCA8418_REG_KP_GPIO3        0x1F
#define TCA8418_REG_GPI_EM1         0x20
#define TCA8418_REG_GPI_EM2         0x21
#define TCA8418_REG_GPI_EM3         0x22
#define TCA8418_REG_GPIO_DIR1       0x23
#define TCA8418_REG_GPIO_DIR2       0x24
#define TCA8418_REG_GPIO_DIR3       0x25
#define TCA8418_REG_GPIO_INT_LVL1   0x26
#define TCA8418_REG_GPIO_INT_LVL2   0x27
#define TCA8418_REG_GPIO_INT_LVL3   0x28
#define TCA8418_REG_DEBOUNCE_DIS1   0x29
#define TCA8418_REG_DEBOUNCE_DIS2   0x2A
#define TCA8418_REG_DEBOUNCE_DIS3   0x2B
#define TCA8418_REG_GPIO_PULL1      0x2C
#define TCA8418_REG_GPIO_PULL2      0x2D
#define TCA8418_REG_GPIO_PULL3      0x2E

/*
 * CFG register bits
 */
#define TCA8418_CFG_AI              BIT(7)  /* Auto-increment */
#define TCA8418_CFG_GPI_E_CFG       BIT(6)  /* GPI event mode config */
#define TCA8418_CFG_OVR_FLOW_M      BIT(5)  /* Overflow mode */
#define TCA8418_CFG_INT_CFG         BIT(4)  /* Interrupt config (50us deassert) */
#define TCA8418_CFG_OVR_FLOW_IEN    BIT(3)  /* Overflow interrupt enable */
#define TCA8418_CFG_K_LCK_IEN       BIT(2)  /* Keypad lock interrupt enable */
#define TCA8418_CFG_GPI_IEN         BIT(1)  /* GPI interrupt enable */
#define TCA8418_CFG_KE_IEN          BIT(0)  /* Key event interrupt enable */

/*
 * INT_STAT register bits
 */
#define TCA8418_INT_CAD_INT         BIT(4)  /* Ctrl-Alt-Del interrupt */
#define TCA8418_INT_OVR_FLOW_INT    BIT(3)  /* Overflow interrupt */
#define TCA8418_INT_K_LCK_INT       BIT(2)  /* Keypad lock interrupt */
#define TCA8418_INT_GPI_INT         BIT(1)  /* GPI interrupt */
#define TCA8418_INT_K_INT           BIT(0)  /* Key event interrupt */

/*
 * KEY_LCK_EC register bits
 */
#define TCA8418_KEY_LCK_EC_K_LCK_EN BIT(6)  /* Key lock enable */
#define TCA8418_KEY_LCK_EC_KEC_MASK GENMASK(3, 0)  /* Key event count mask */

/*
 * Key event encoding
 */
#define TCA8418_KEY_EVENT_PRESS     BIT(7)  /* 1 = press, 0 = release */
#define TCA8418_KEY_EVENT_CODE_MASK GENMASK(6, 0)  /* Key code mask */

/*
 * Maximum values
 */
#define TCA8418_MAX_ROWS            8
#define TCA8418_MAX_COLS            10
#define TCA8418_FIFO_SIZE           10

/**
 * @brief TCA8418 MFD interrupt callback handler type
 */
typedef void (*tca8418_callback_handler_t)(const struct device *dev);

/**
 * @brief TCA8418 MFD power event type
 */
enum tca8418_mfd_power_event {
	TCA8418_MFD_POWER_EVENT_TURN_ON,
	TCA8418_MFD_POWER_EVENT_TURN_OFF,
};

/**
 * @brief TCA8418 MFD power callback handler type
 */
typedef void (*tca8418_power_callback_handler_t)(const struct device *dev,
						  enum tca8418_mfd_power_event event);

/**
 * @brief TCA8418 MFD callback structure
 */
struct tca8418_mfd_callback {
	sys_snode_t node;
	tca8418_callback_handler_t cb;
	const struct device *dev;
};

/**
 * @brief TCA8418 MFD power callback structure
 */
struct tca8418_mfd_power_callback {
	sys_snode_t node;
	tca8418_power_callback_handler_t cb;
	const struct device *dev;
};

/**
 * @brief Register an interrupt callback with the TCA8418 MFD
 *
 * The callback storage must remain valid for the lifetime of the child device.
 * This API is intended for child-device initialization and has no unregister
 * counterpart.
 *
 * @param mfd The MFD device
 * @param callback The callback structure to register
 */
void mfd_tca8418_register_interrupt_callback(const struct device *mfd,
					     struct tca8418_mfd_callback *callback);

/**
 * @brief Register a power event callback with the TCA8418 MFD
 *
 * The callback storage must remain valid for the lifetime of the child device.
 * This API is intended for child-device initialization and has no unregister
 * counterpart.
 *
 * @param mfd The MFD device
 * @param callback The callback structure to register
 */
void mfd_tca8418_register_power_callback(const struct device *mfd,
					 struct tca8418_mfd_power_callback *callback);

/**
 * @brief Get the I2C device spec from the MFD
 *
 * @param mfd The MFD device
 * @return Pointer to the I2C device spec
 */
const struct i2c_dt_spec *mfd_tca8418_get_i2c_spec(const struct device *mfd);

/**
 * @brief Return whether the MFD parent has an interrupt GPIO.
 *
 * @param mfd The MFD device
 * @return true when interrupt mode is available
 */
bool mfd_tca8418_has_interrupt(const struct device *mfd);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_MFD_TCA8418_H_ */
