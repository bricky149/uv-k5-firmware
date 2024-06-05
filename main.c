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
#include "driver/systick.h"
#include "scheduler.h"
#if defined(ENABLE_UART)
#include "driver/uart.h"
#endif
#include "helper/battery.h"
#include "helper/boot.h"
#if defined(ENABLE_MDC1200)
#include "mdc1200.h"
#endif
#include "misc.h"
#include "radio.h"
#include "settings.h"
//#include "task/battery.h"
#if defined(ENABLE_FMRADIO)
#include "task/fm.h"
#endif
#include "task/keys.h"
#include "task/radio.h"
#include "task/scanner.h"
#include "task/screen.h"
#include "ui/lock.h"

#if defined(ENABLE_UART)
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
	BK4819_Init();

#if defined(ENABLE_MDC1200)
	MDC1200_Init();
#endif

#if defined(ENABLE_UART)
	UART_Init();
	const char UART_Version[] = "UV-K5 Firmware, Open Edition, B149-"GIT_HASH"\n";
	UART_Send(UART_Version, sizeof(UART_Version));
#endif

	// Not implementing authentic device checks

	memset(gDTMF_String, '-', sizeof(gDTMF_String));
	gDTMF_String[14] = 0;

	memset(&gEeprom, 0, sizeof(gEeprom));
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

	if (!gChargingWithTypeC && gBatteryDisplayLevel == 1) {
		FUNCTION_Select(FUNCTION_POWER_SAVE);
		GPIO_ClearBit(&GPIOB->DATA, GPIOB_PIN_BACKLIGHT);
	} else {
		BACKLIGHT_TurnOn();
		if (gEeprom.POWER_ON_PASSWORD < 1000000) {
			bIsInLockScreen = true;
			UI_DisplayLock();
			bIsInLockScreen = false;
		}
		gMenuListCount = 45; // Does not include hidden items
		BOOT_Mode_t BootMode = BOOT_GetMode();
		BOOT_ProcessMode(BootMode);
		gUpdateStatus = true;
	}

	// Everything is initialised, set SLEEP* bits to save power
	SYSCON_REGISTER |= SYSCON_REGISTER_SLEEPONEXIT_BITS_ENABLE;
	SYSCON_REGISTER |= SYSCON_REGISTER_SLEEPDEEP_BITS_ENABLE;

	while (1) {
		if (SCHEDULER_CheckTask(TASK_SLEEP)) {
			continue;
		}

		APP_Update(); // Does not rely on sub-10ms timings

		// 10ms
		TASK_CheckKeys();
		TASK_CheckRadioInterrupts();
		TASK_UpdateScreen();

		// 500ms
#if defined(ENABLE_FMRADIO)
		TASK_FM_Radio();
#endif
		TASK_Scanner();

		if (gNextTimeslice500ms) {
			APP_TimeSlice500ms();
			gNextTimeslice500ms = false;
		}
	}
}

