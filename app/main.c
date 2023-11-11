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
#include "app/action.h"
#include "app/app.h"
#if defined(ENABLE_FMRADIO)
#include "app/fm.h"
#endif
#include "app/generic.h"
#include "app/main.h"
#include "app/scanner.h"
#include "dtmf.h"
#include "frequencies.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/inputbox.h"
#include "ui/ui.h"

static void MAIN_Key_DIGITS(KEY_Code_t Key)
{
	uint8_t Vfo = gEeprom.TX_VFO;

	if (!gWasFKeyPressed) {
		INPUTBOX_Append(Key);
		gRequestDisplayScreen = DISPLAY_MAIN;
		if (IS_MR_CHANNEL(gTxVfo->CHANNEL_SAVE)) {
			uint16_t Channel;

			if (gInputBoxIndex != 3) {
				gRequestDisplayScreen = DISPLAY_MAIN;
				return;
			}
			gInputBoxIndex = 0;
			Channel = ((gInputBox[0] * 100) + (gInputBox[1] * 10) + gInputBox[2]) - 1;
			if (!RADIO_CheckValidChannel(Channel, false, 0)) {
				return;
			}
			gEeprom.MrChannel[Vfo] = (uint8_t)Channel;
			gEeprom.ScreenChannel[Vfo] = (uint8_t)Channel;
			gRequestSaveVFO = true;
			gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
			return;
		}
		if (gInputBoxIndex < 6) {
			return;
		}
		gInputBoxIndex = 0;
		uint32_t Frequency;
		NUMBER_Get(gInputBox, &Frequency);

		if (Frequency < 35000000 || Frequency > 39999990) {
			uint8_t i;

			for (i = 0; i < 7; i++) {
				if (Frequency <= UpperLimitFrequencyBandTable[i] && (LowerLimitFrequencyBandTable[i] <= Frequency)) {
					if (gTxVfo->Band != i) {
						gTxVfo->Band = i;
						gEeprom.ScreenChannel[Vfo] = i + FREQ_CHANNEL_FIRST;
						gEeprom.FreqChannel[Vfo] = i + FREQ_CHANNEL_FIRST;
						SETTINGS_SaveVfoIndices();
						RADIO_ConfigureChannel(Vfo, 2);
					}
					Frequency += 75;
					gTxVfo->ConfigRX.Frequency = FREQUENCY_FloorToStep(
							Frequency,
							gTxVfo->StepFrequency,
							LowerLimitFrequencyBandTable[gTxVfo->Band]
							);
					gRequestSaveChannel = 1;
					return;
				}
			}
		}
		gRequestDisplayScreen = DISPLAY_MAIN;
		return;
	}
	gWasFKeyPressed = false;
	gUpdateStatus = true;
	switch (Key) {
	case KEY_0:
#if defined(ENABLE_FMRADIO)
		ACTION_FM();
#endif
		break;

	case KEY_1:
		if (!IS_FREQ_CHANNEL(gTxVfo->CHANNEL_SAVE)) {
			gWasFKeyPressed = false;
			gUpdateStatus = true;
			return;
		}
		uint8_t Band = gTxVfo->Band + 1;
		if (Band != BAND5_350MHz) {
			if (BAND7_470MHz < Band) {
				Band = BAND1_18MHz;
			}
		} else {
			Band = BAND6_400MHz;
		}
		gTxVfo->Band = Band;
		gEeprom.ScreenChannel[Vfo] = FREQ_CHANNEL_FIRST + Band;
		gEeprom.FreqChannel[Vfo] = FREQ_CHANNEL_FIRST + Band;
		gRequestSaveVFO = true;
		gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
		gRequestDisplayScreen = DISPLAY_MAIN;
		break;

	case KEY_2:
		if (gEeprom.CROSS_BAND_RX_TX == CROSS_BAND_CHAN_A) {
			gEeprom.CROSS_BAND_RX_TX = CROSS_BAND_CHAN_B;
		} else if (gEeprom.CROSS_BAND_RX_TX == CROSS_BAND_CHAN_B) {
			gEeprom.CROSS_BAND_RX_TX = CROSS_BAND_CHAN_A;
		} else if (gEeprom.DUAL_WATCH == DUAL_WATCH_CHAN_A) {
			gEeprom.DUAL_WATCH = DUAL_WATCH_CHAN_B;
		} else if (gEeprom.DUAL_WATCH == DUAL_WATCH_CHAN_B) {
			gEeprom.DUAL_WATCH = DUAL_WATCH_CHAN_A;
		} else {
			gEeprom.TX_VFO = !Vfo;
		}
		gRequestSaveSettings = 1;
		gFlagReconfigureVfos = true;
		gRequestDisplayScreen = DISPLAY_MAIN;
		break;

	case KEY_3:
		if (gEeprom.VFO_OPEN) {
			uint8_t Channel;

			if (IS_MR_CHANNEL(gTxVfo->CHANNEL_SAVE)) {
				gEeprom.ScreenChannel[Vfo] = gEeprom.FreqChannel[gEeprom.TX_VFO];
				gRequestSaveVFO = true;
				gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
				break;
			}
			Channel = RADIO_FindNextChannel(gEeprom.MrChannel[gEeprom.TX_VFO], 1, false, 0);
			if (Channel != 0xFF) {
				gEeprom.ScreenChannel[Vfo] = Channel;
				gRequestSaveVFO = true;
				gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
				break;
			}
		}
		break;

	case KEY_4:
		gWasFKeyPressed = false;
		gUpdateStatus = true;
		gFlagStartScan = true;
		gScanSingleFrequency = false;
		gBackupCROSS_BAND_RX_TX = gEeprom.CROSS_BAND_RX_TX;
		gEeprom.CROSS_BAND_RX_TX = CROSS_BAND_OFF;
		break;

	case KEY_6:
		ACTION_Power();
		break;

	case KEY_8:
		gTxVfo->FrequencyReverse = !gTxVfo->FrequencyReverse;
		gRequestSaveChannel = 1;
		break;

	case KEY_9:
		if (RADIO_CheckValidChannel(gEeprom.CHAN_1_CALL, false, 0)) {
			gEeprom.MrChannel[Vfo] = gEeprom.CHAN_1_CALL;
			gEeprom.ScreenChannel[Vfo] = gEeprom.CHAN_1_CALL;
			gRequestSaveVFO = true;
			gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
			break;
		}
		break;

	default:
		gUpdateStatus = true;
		gWasFKeyPressed = false;
		break;
	}
}

static void MAIN_Key_EXIT(void)
{
#if defined(ENABLE_FMRADIO)
	if (gFmRadioMode) {
		ACTION_FM();
		return;
	}
#endif
	if (gScanState == SCAN_OFF) {
		if (gInputBoxIndex == 0) {
			return;
		}
		gInputBoxIndex--;
		gInputBox[gInputBoxIndex] = 10;
	} else {
		SCANNER_Stop();
	}
	gRequestDisplayScreen = DISPLAY_MAIN;
}

static void MAIN_Key_MENU(void)
{
	bool bFlag = gInputBoxIndex == 0;
	gInputBoxIndex = 0;
	if (bFlag) {
		gFlagRefreshSetting = true;
		gRequestDisplayScreen = DISPLAY_MENU;
	} else {
		gRequestDisplayScreen = DISPLAY_MAIN;
	}
}

static void MAIN_Key_STAR(bool bKeyPressed, bool bKeyHeld)
{
	if (bKeyHeld || !bKeyPressed) {
		if (bKeyHeld || bKeyPressed) {
			if (!bKeyHeld || !bKeyPressed) {
				return;
			}
			ACTION_Scan(false);
			return;
		}
		if (gScanState == SCAN_OFF) {
			gDTMF_InputMode = true;
			memcpy(gDTMF_InputBox, gDTMF_String, 15);
			gDTMF_InputIndex = 0;
			gRequestDisplayScreen = DISPLAY_MAIN;
			return;
		}
	} else {
		if (!gWasFKeyPressed) {
			return;
		}
		gWasFKeyPressed = false;
		gUpdateStatus = true;
		gFlagStartScan = true;
		gScanSingleFrequency = true;
		gBackupCROSS_BAND_RX_TX = gEeprom.CROSS_BAND_RX_TX;
		gEeprom.CROSS_BAND_RX_TX = CROSS_BAND_OFF;
		gPttWasReleased = true;
	}
}

static void MAIN_Key_UP_DOWN(bool bKeyPressed, int8_t Direction)
{
	if (!bKeyPressed || gInputBoxIndex > 0) {
		return;
	}
	uint8_t Channel = gEeprom.ScreenChannel[gEeprom.TX_VFO];
	if (gScanState == SCAN_OFF) {
		if (IS_FREQ_CHANNEL(Channel)) {
			APP_SetFrequencyByStep(gTxVfo, Direction);
			gRequestSaveChannel = 1;
			return;
		}
		uint8_t Next = RADIO_FindNextChannel(Channel + Direction, Direction, false, 0);
		if (Next == 0xFF || Channel == Next) {
			return;
		}
		gEeprom.MrChannel[gEeprom.TX_VFO] = Next;
		gEeprom.ScreenChannel[gEeprom.TX_VFO] = Next;
		gRequestSaveVFO = true;
		gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
		return;
	}
	CHANNEL_Next(false, Direction);
	gPttWasReleased = true;
}

void MAIN_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
#if defined(ENABLE_FMRADIO)
	if (gFmRadioMode && Key != KEY_PTT && Key != KEY_EXIT) {
		return;
	}
#endif
	if (gDTMF_InputMode && !bKeyHeld && bKeyPressed) {
		char Character = DTMF_GetCharacter(Key);
		if (Character != 0xFF) {
			DTMF_Append(Character);
			gRequestDisplayScreen = DISPLAY_MAIN;
			gPttWasReleased = true;
			return;
		}
	}

	switch (Key) {
	case KEY_0: case KEY_1: case KEY_2: case KEY_3:
	case KEY_4: case KEY_5: case KEY_6: case KEY_7:
	case KEY_8: case KEY_9:
		if (!bKeyHeld && bKeyPressed) {
			MAIN_Key_DIGITS(Key);
		}
		break;
	case KEY_MENU:
		if (!bKeyHeld && bKeyPressed) {
			MAIN_Key_MENU();
		}
		break;
	case KEY_UP:
		MAIN_Key_UP_DOWN(bKeyPressed, 1);
		break;
	case KEY_DOWN:
		MAIN_Key_UP_DOWN(bKeyPressed, -1);
		break;
	case KEY_EXIT:
		if (!bKeyHeld && bKeyPressed) {
			MAIN_Key_EXIT();
		}
		break;
	case KEY_STAR:
		if (gInputBoxIndex == 0) {
			MAIN_Key_STAR(bKeyPressed, bKeyHeld);
		}
		break;
	case KEY_F:
		GENERIC_Key_F(bKeyPressed, bKeyHeld);
		break;
	case KEY_PTT:
		GENERIC_Key_PTT(bKeyPressed);
		break;
	default:
		break;
	}
}

