/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include <string.h>
#include "app/app.h"
#include "app/dtmf.h"
#include "bsp/dp32g030/gpio.h"
#include "bsp/dp32g030/syscon.h"
#include "board.h"
#include "driver/backlight.h"
#include "driver/bk4819.h"
#include "driver/gpio.h"
#include "driver/system.h"
#include "driver/systick.h"
#if defined(ENABLE_UART)
#include "driver/uart.h"
#endif
#include "helper/battery.h"
#include "helper/boot.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/lock.h"

#if defined(ENABLE_UART)
static const char Version[] = "UV-K5 Firmware, Open Edition, OEFW-"GIT_HASH"\r\n";

void _putchar(char c)
{
	UART_Send((uint8_t *)&c, 1);
}
#endif

void Main(void)
{
	// Enable clock gating of blocks we need.
	SYSCON_DEV_CLK_GATE = 0
		| SYSCON_DEV_CLK_GATE_GPIOA_BITS_ENABLE
		| SYSCON_DEV_CLK_GATE_GPIOB_BITS_ENABLE
		| SYSCON_DEV_CLK_GATE_GPIOC_BITS_ENABLE
		| SYSCON_DEV_CLK_GATE_UART1_BITS_ENABLE
		| SYSCON_DEV_CLK_GATE_SPI0_BITS_ENABLE
		| SYSCON_DEV_CLK_GATE_SARADC_BITS_ENABLE
		| SYSCON_DEV_CLK_GATE_CRC_BITS_ENABLE
		| SYSCON_DEV_CLK_GATE_AES_BITS_ENABLE
		;

	SYSTICK_Init();
	BOARD_Init();

#if defined(ENABLE_UART)
	UART_Init();
	UART_Send(Version, sizeof(Version));
#endif

	// Not implementing authentic device checks

	memset(&gEeprom, 0, sizeof(gEeprom));
	memset(gDTMF_String, '-', sizeof(gDTMF_String));
	gDTMF_String[14] = 0;

	BK4819_Init();
	BOARD_EEPROM_Init();
	BOARD_EEPROM_LoadCalibration();

	RADIO_ConfigureChannel(0, 2);
	RADIO_ConfigureChannel(1, 2);
	RADIO_SelectVfos();
	RADIO_SetupRegisters(true);

	BOARD_ADC_GetBatteryInfo(&gBatteryCurrentVoltage, &gBatteryCurrent);
	for (uint8_t i = 0; i < 4; i++) {
		BOARD_ADC_GetBatteryInfo(&gBatteryVoltages[i], &gBatteryCurrent);
	}
	BATTERY_GetReadings(false);

	if (!gChargingWithTypeC && gBatteryDisplayLevel == 0) {
		FUNCTION_Select(FUNCTION_POWER_SAVE);
		GPIO_ClearBit(&GPIOB->DATA, GPIOB_PIN_BACKLIGHT);
		gReducedService = true;
	} else {
		BACKLIGHT_TurnOn();
		gMenuListCount = 49; // Does not include hidden items
		BOOT_Mode_t BootMode = BOOT_GetMode();
		if (gEeprom.POWER_ON_PASSWORD < 1000000) {
			bIsInLockScreen = true;
			UI_DisplayLock();
			bIsInLockScreen = false;
		}
		BOOT_ProcessMode(BootMode);
		gUpdateStatus = true;
	}

	// Everything is initialised, set SLEEP* bits to save power
	SYSCON_REGISTER |= SYSCON_REGISTER_SLEEPONEXIT_BITS_ENABLE;
	SYSCON_REGISTER |= SYSCON_REGISTER_SLEEPDEEP_BITS_ENABLE;

	while (1) {
		APP_Update();
		if (gNextTimeslice) {
			APP_TimeSlice10ms();
			gNextTimeslice = false;
		}
		if (gNextTimeslice500ms) {
			APP_TimeSlice500ms();
			gNextTimeslice500ms = false;
		}
	}
}

