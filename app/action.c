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

#include "app/action.h"
#include "app/app.h"
#include "app/dtmf.h"
#if defined(ENABLE_FMRADIO)
#include "app/fm.h"
#endif
#include "app/scanner.h"
#include "bsp/dp32g030/gpio.h"
#include "driver/bk1080.h"
#include "driver/bk4819.h"
#include "driver/gpio.h"
#include "functions.h"
#include "misc.h"
#include "settings.h"
#include "ui/inputbox.h"
#include "ui/ui.h"

static void ACTION_FlashLight(void)
{
	switch (gFlashLightState) {
	case 0:
		gFlashLightState++;
		GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_FLASHLIGHT);
		break;
	case 1:
		gFlashLightState++;
		break;
	default:
		gFlashLightState = 0;
		GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_FLASHLIGHT);
	}
}

void ACTION_Power(void)
{
	if (++gTxVfo->OUTPUT_POWER > OUTPUT_POWER_HIGH) {
		gTxVfo->OUTPUT_POWER = OUTPUT_POWER_LOW;
	}

	gRequestSaveChannel = 1;
	gRequestDisplayScreen = gScreenToDisplay;
}

static void ACTION_Monitor(void)
{
	if (gCurrentFunction != FUNCTION_MONITOR) {
		RADIO_SelectVfos();
		RADIO_SetupRegisters(true);
		APP_StartListening(FUNCTION_MONITOR);
		return;
	}
	if (gScanState != SCAN_OFF) {
		ScanPauseDelayIn10msec = 500;
		gScheduleScanListen = false;
		gScanPauseMode = true;
	}

	RADIO_SetupRegisters(true);
#if defined(ENABLE_FMRADIO)
	if (gFmRadioMode) {
		FM_Start();
		gRequestDisplayScreen = DISPLAY_FM;
	} else {
#endif
		gRequestDisplayScreen = gScreenToDisplay;
#if defined(ENABLE_FMRADIO)
	}
#endif
}

#if defined(ENABLE_FMRADIO)
void ACTION_Scan(bool bRestart)
{
	if (!gFmRadioMode) {
		return;
	}
	if (gCurrentFunction != FUNCTION_RECEIVE && gCurrentFunction != FUNCTION_MONITOR && gCurrentFunction != FUNCTION_TRANSMIT) {
		uint16_t Frequency;
		GUI_SelectNextDisplay(DISPLAY_FM);
		
		if (gFM_ScanState != FM_SCAN_OFF) {
			FM_PlayAndUpdate();
		} else {
			if (bRestart) {
				gFM_AutoScan = true;
				gFM_ChannelPosition = 0;
				FM_EraseChannels();
				Frequency = 760;
			} else {
				gFM_AutoScan = false;
				gFM_ChannelPosition = 0;
				Frequency = gEeprom.FM_FrequencyPlaying;
			}
			BK1080_GetFrequencyDeviation(Frequency);
			FM_Tune(Frequency, 1, bRestart);
		}
	}
}
#else
void ACTION_Scan(void)
{
	if (gScreenToDisplay == DISPLAY_SCANNER) {
		return;
	}
	RADIO_SelectVfos();
	gRequestDisplayScreen = DISPLAY_MAIN;
	if (gScanState != SCAN_OFF) {
		SCANNER_Stop();
	} else {
		CHANNEL_Next(true, 1);
	}
}
#endif

#if defined(ENABLE_FMRADIO)
void ACTION_FM(void)
{
	if (gCurrentFunction != FUNCTION_TRANSMIT && gCurrentFunction != FUNCTION_MONITOR) {
		if (gFmRadioMode) {
			FM_TurnOff();
			gInputBoxIndex = 0;
			gFlagReconfigureVfos = true;
			gRequestDisplayScreen = DISPLAY_MAIN;
			return;
		}
		RADIO_SelectVfos();
		RADIO_SetupRegisters(true);
		FM_Start();
		gInputBoxIndex = 0;
		gRequestDisplayScreen = DISPLAY_FM;
	}
}
#endif

void ACTION_Handle(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
	if (gScreenToDisplay == DISPLAY_MAIN && gDTMF_InputMode) {
		if (Key == KEY_SIDE1 && !bKeyHeld && bKeyPressed) {
			if (gDTMF_InputIndex > 0) {
				gDTMF_InputIndex--;
				gDTMF_InputBox[gDTMF_InputIndex] = '-';
				if (gDTMF_InputIndex > 0) {
					gPttWasReleased = true;
					gRequestDisplayScreen = DISPLAY_MAIN;
					return;
				}
			}
			gRequestDisplayScreen = DISPLAY_MAIN;
			gDTMF_InputMode = false;
		}
		gPttWasReleased = true;
		return;
	}
	if (!bKeyHeld && bKeyPressed) {
		return;
	}

	uint8_t Short;
	uint8_t Long;
	if (Key == KEY_SIDE1) {
		Short = gEeprom.KEY_1_SHORT_PRESS_ACTION;
		Long = gEeprom.KEY_1_LONG_PRESS_ACTION;
	} else {
		Short = gEeprom.KEY_2_SHORT_PRESS_ACTION;
		Long = gEeprom.KEY_2_LONG_PRESS_ACTION;
	}

	if (bKeyHeld || bKeyPressed) {
		if (!bKeyHeld) {
			return;
		}
		Short = Long;
		if (!bKeyPressed) {
			return;
		}
	}

	switch (Short) {
	case 1:
		ACTION_FlashLight();
		break;
	case 2:
		ACTION_Power();
		break;
	case 3:
		ACTION_Monitor();
		break;
	case 4:
		// Work around bug where unlocking does
		// not work while scan is running
		if (!gEeprom.KEY_LOCK) {
#if defined(ENABLE_FMRADIO)
			ACTION_Scan(true);
#else
			ACTION_Scan();
#endif
		}
		break;
	case 5:
#if defined(ENABLE_FMRADIO)
		ACTION_FM();
#endif
		break;
	}
}

