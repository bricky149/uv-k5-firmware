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
#include "ARMCM0.h"
#include "app/dtmf.h"
#include "app/generic.h"
#include "app/menu.h"
#include "app/scanner.h"
#include "board.h"
#include "bsp/dp32g030/gpio.h"
#include "driver/backlight.h"
#include "driver/eeprom.h"
#include "driver/gpio.h"
#include "driver/keyboard.h"
#include "frequencies.h"
#include "helper/battery.h"
#include "misc.h"
#include "settings.h"
#include "ui/inputbox.h"
#include "ui/menu.h"
#include "ui/ui.h"

void MENU_StartCssScan(int8_t Direction)
{
	gCssScanMode = CSS_SCAN_MODE_SCANNING;
	gMenuScrollDirection = Direction;
	RADIO_SelectVfos();
	MENU_SelectNextCode();
	ScanPauseDelayIn10msec = 50;
	gScheduleScanListen = false;
}

void MENU_StopCssScan(void)
{
	gCssScanMode = CSS_SCAN_MODE_OFF;
	RADIO_SetupRegisters(true);
}

bool MENU_GetLimits(uint8_t Cursor, uint16_t *pMin, uint16_t *pMax)
{
	switch (Cursor) {
	case MENU_SQL:
		*pMin = 0;
		*pMax = 9;
		break;
	case MENU_STEP:
		if (gTxVfo->Band == BAND2_108MHz) {
			*pMin = 0;
			*pMax = 6;
			break;
		}
		// Fallthrough
	case MENU_ABR: case MENU_F_LOCK:
		*pMin = 0;
		*pMax = 5;
		break;
	case MENU_TXP: case MENU_SFT_D:
	case MENU_TDR: case MENU_WX:
	case MENU_SC_REV:
	case MENU_MDF:
	case MENU_ROGER:
		*pMin = 0;
		*pMax = 2;
		break;
	case MENU_R_DCS: case MENU_T_DCS:
		*pMin = 0;
		*pMax = 208;
		break;
	case MENU_R_CTCS: case MENU_T_CTCS:
		*pMin = 0;
		*pMax = 50;
		break;
	case MENU_W_N: case MENU_BCL:
	case MENU_AUTOLK:
	case MENU_S_ADD1: case MENU_S_ADD2:
	case MENU_STE:
	case MENU_D_ST: case MENU_D_DCD:
	case MENU_AM:
	case MENU_RESET: case MENU_350TX:
	case MENU_200TX: case MENU_500TX:
	case MENU_350EN:
		*pMin = 0;
		*pMax = 1;
		break;
	case MENU_TOT: case MENU_RP_STE:
		*pMin = 0;
		*pMax = 10;
		break;
	case MENU_MEM_CH: case MENU_1_CALL:
	case MENU_SLIST1: case MENU_SLIST2:
	case MENU_DEL_CH:
		*pMin = 0;
		*pMax = 199;
		break;
	case MENU_SAVE: case MENU_MIC:
		*pMin = 0;
		*pMax = 4;
		break;
	case MENU_S_LIST:
		*pMin = 1;
		*pMax = 2;
		break;
	case MENU_D_RSP: case MENU_PTT_ID:
		*pMin = 0;
		*pMax = 3;
		break;
	case MENU_D_HOLD:
		*pMin = 5;
		*pMax = 60;
		break;
	case MENU_D_PRE:
		*pMin = 3;
		*pMax = 99;
		break;
	case MENU_D_LIST:
		*pMin = 1;
		*pMax = 16;
		break;
	case MENU_BATCAL:
		*pMin = 1600;  // 0
		*pMax = 2200;  // 2300
		break;
	default:
		return false;
	}

	return true;
}

void MENU_AcceptSetting(void)
{
	uint16_t Min, Max;
	if (MENU_GetLimits(gMenuCursor, &Min, &Max)) {
		if (gSubMenuSelection < Min) {
			gSubMenuSelection = Min;
		} else if (gSubMenuSelection > Max) {
			gSubMenuSelection = Max;
		}
	}

	uint8_t Code;
	FREQ_Config_t *pConfig = &gTxVfo->ConfigRX;

	switch (gMenuCursor) {
	case MENU_SQL:
		gEeprom.SQUELCH_LEVEL = gSubMenuSelection;
		gRequestSaveSettings = true;
		gVfoConfigureMode = VFO_CONFIGURE;
		return;

	case MENU_STEP:
		if (IS_FREQ_CHANNEL(gTxVfo->CHANNEL_SAVE)) {
			gTxVfo->STEP_SETTING = gSubMenuSelection;
			gRequestSaveChannel = 1;
			return;
		}
		gSubMenuSelection = gTxVfo->STEP_SETTING;
		return;

	case MENU_TXP:
		gTxVfo->OUTPUT_POWER = gSubMenuSelection;
		gRequestSaveChannel = 1;
		return;

	case MENU_T_DCS:
		pConfig = &gTxVfo->ConfigTX;
		// Fallthrough
	case MENU_R_DCS:
		if (gSubMenuSelection == 0) {
			if (pConfig->CodeType != CODE_TYPE_DIGITAL && pConfig->CodeType != CODE_TYPE_REVERSE_DIGITAL) {
				gRequestSaveChannel = 1;
				return;
			}
			Code = 0;
			pConfig->CodeType = CODE_TYPE_OFF;
		} else if (gSubMenuSelection < 105) {
			pConfig->CodeType = CODE_TYPE_DIGITAL;
			Code = gSubMenuSelection - 1;
		} else {
			pConfig->CodeType = CODE_TYPE_REVERSE_DIGITAL;
			Code = gSubMenuSelection - 105;
		}
		pConfig->Code = Code;
		gRequestSaveChannel = 1;
		return;

	case MENU_T_CTCS:
		pConfig = &gTxVfo->ConfigTX;
		// Fallthrough
	case MENU_R_CTCS:
		if (gSubMenuSelection == 0) {
			if (pConfig->CodeType != CODE_TYPE_CONTINUOUS_TONE) {
				gRequestSaveChannel = 1;
				return;
			}
			Code = 0;
			pConfig->CodeType = CODE_TYPE_OFF;
		} else {
			pConfig->CodeType = CODE_TYPE_CONTINUOUS_TONE;
			Code = gSubMenuSelection - 1;
		}
		pConfig->Code = Code;
		gRequestSaveChannel = 1;
		return;

	case MENU_SFT_D:
		gTxVfo->FREQUENCY_DEVIATION_SETTING = gSubMenuSelection;
		gRequestSaveChannel = 1;
		return;

	case MENU_OFFSET:
		gTxVfo->FREQUENCY_OF_DEVIATION = gSubMenuSelection;
		gRequestSaveChannel = 1;
		return;

	case MENU_W_N:
		gTxVfo->CHANNEL_BANDWIDTH = gSubMenuSelection;
		gRequestSaveChannel = 1;
		return;

	case MENU_BCL:
		gTxVfo->BUSY_CHANNEL_LOCK = gSubMenuSelection;
		gRequestSaveChannel = 1;
		return;

	case MENU_MEM_CH:
		gTxVfo->CHANNEL_SAVE = gSubMenuSelection;
		gRequestSaveChannel = 2;
		gEeprom.MrChannel[0] = gSubMenuSelection;
		return;

	case MENU_SAVE:
		gEeprom.BATTERY_SAVE = gSubMenuSelection;
		break;

	case MENU_ABR:
		gEeprom.BACKLIGHT = gSubMenuSelection;
		if (gSubMenuSelection == 0) {
			GPIO_ClearBit(&GPIOB->DATA, GPIOB_PIN_BACKLIGHT);
		} else {
			BACKLIGHT_TurnOn();
		}
		break;

	case MENU_TDR:
		gEeprom.DUAL_WATCH = gSubMenuSelection;
		gFlagReconfigureVfos = true;
		gRequestSaveSettings = true;
		gUpdateStatus = true;
		return;

	case MENU_WX:
		gEeprom.CROSS_BAND_RX_TX = gSubMenuSelection;
		gFlagReconfigureVfos = true;
		gRequestSaveSettings = true;
		gUpdateStatus = true;
		return;

	case MENU_TOT:
		gEeprom.TX_TIMEOUT_TIMER = gSubMenuSelection;
		break;

	case MENU_SC_REV:
		gEeprom.SCAN_RESUME_MODE = gSubMenuSelection;
		break;

	case MENU_MDF:
		gEeprom.CHANNEL_DISPLAY_MODE = gSubMenuSelection;
		break;

	case MENU_AUTOLK:
		gEeprom.AUTO_KEYPAD_LOCK = gSubMenuSelection;
		gKeyLockCountdown = 0x1e;
		break;

	case MENU_S_ADD1:
		gTxVfo->SCANLIST1_PARTICIPATION = gSubMenuSelection;
		SETTINGS_UpdateChannel(gTxVfo->CHANNEL_SAVE, gTxVfo, true);
		gVfoConfigureMode = VFO_CONFIGURE;
		gFlagResetVfos = true;
		return;

	case MENU_S_ADD2:
		gTxVfo->SCANLIST2_PARTICIPATION = gSubMenuSelection;
		SETTINGS_UpdateChannel(gTxVfo->CHANNEL_SAVE, gTxVfo, true);
		gVfoConfigureMode = VFO_CONFIGURE;
		gFlagResetVfos = true;
		return;

	case MENU_STE:
		gEeprom.TAIL_NOTE_ELIMINATION = gSubMenuSelection;
		break;

	case MENU_RP_STE:
		gEeprom.REPEATER_TAIL_TONE_ELIMINATION = gSubMenuSelection;
		break;

	case MENU_MIC:
		gEeprom.MIC_SENSITIVITY = gSubMenuSelection;
		BOARD_EEPROM_LoadCalibration();
		gRequestSaveSettings = true;
		gFlagReconfigureVfos = true;
		return;

	case MENU_1_CALL:
		gEeprom.CHAN_1_CALL = gSubMenuSelection;
		break;

	case MENU_S_LIST:
		gEeprom.SCAN_LIST_DEFAULT = gSubMenuSelection - 1;
		break;

	case MENU_D_ST:
		gEeprom.DTMF_SIDE_TONE = gSubMenuSelection;
		break;

	case MENU_D_RSP:
		gEeprom.DTMF_DECODE_RESPONSE = gSubMenuSelection;
		break;

	case MENU_D_HOLD:
		gEeprom.DTMF_AUTO_RESET_TIME = gSubMenuSelection;
		break;

	case MENU_D_PRE:
		gEeprom.DTMF_PRELOAD_TIME = gSubMenuSelection * 10;
		break;

	case MENU_PTT_ID:
		gTxVfo->DTMF_PTT_ID_TX_MODE = gSubMenuSelection;
		gRequestSaveChannel = 1;
		return;

	case MENU_D_DCD:
		gTxVfo->DTMF_DECODING_ENABLE = gSubMenuSelection;
		gRequestSaveChannel = 1;
		return;

	case MENU_D_LIST:
		gDTMFChosenContact = gSubMenuSelection - 1;
		if (gIsDtmfContactValid) {
			GUI_SelectNextDisplay(DISPLAY_MAIN);
			gDTMF_InputMode = true;
			gDTMF_InputIndex = 3;
			memcpy(gDTMF_InputBox, gDTMF_ID, 4);
			gRequestDisplayScreen = DISPLAY_INVALID;
		}
		return;

	case MENU_ROGER:
		gEeprom.ROGER = gSubMenuSelection;
		break;

	case MENU_AM:
		gTxVfo->AM_CHANNEL_MODE = gSubMenuSelection;
		gRequestSaveChannel = 1;
		return;

	case MENU_DEL_CH:
		SETTINGS_UpdateChannel(gSubMenuSelection, NULL, false);
		gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
		gFlagResetVfos = true;
		return;

	case MENU_RESET:
		BOARD_FactoryReset(gSubMenuSelection);
		return;

	case MENU_350TX:
		gSetting_350TX = gSubMenuSelection;
		break;

	case MENU_F_LOCK:
		gSetting_F_LOCK = gSubMenuSelection;
		break;

	case MENU_200TX:
		gSetting_200TX = gSubMenuSelection;
		break;

	case MENU_500TX:
		gSetting_500TX = gSubMenuSelection;
		break;

	case MENU_350EN:
		gSetting_350EN = gSubMenuSelection;
		gRequestSaveSettings = true;
		gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
		gFlagResetVfos = true;
		return;

	case MENU_BATCAL:
		gBatteryCalibration[0] = (520ul * gSubMenuSelection) / 760;  // 5.20V empty, blinking above this value, reduced functionality below
		gBatteryCalibration[1] = (700ul * gSubMenuSelection) / 760;  // 7.00V,  ~5%, 1 bars above this value
		gBatteryCalibration[2] = (745ul * gSubMenuSelection) / 760;  // 7.45V, ~17%, 2 bars above this value
		gBatteryCalibration[3] =          gSubMenuSelection;         // 7.6V,  ~29%, 3 bars above this value
		gBatteryCalibration[4] = (788ul * gSubMenuSelection) / 760;  // 7.88V, ~65%, 4 bars above this value
		gBatteryCalibration[5] = 2300;
		EEPROM_WriteBuffer(0x1F40, gBatteryCalibration);
		break;

	default:
		return;
	}

	gRequestSaveSettings = true;
}

void MENU_SelectNextCode(void)
{
	uint8_t UpperLimit;

	if (gMenuCursor == MENU_R_DCS) {
		UpperLimit = 208;
	} else if (gMenuCursor == MENU_R_CTCS) {
		UpperLimit = 50;
	} else {
		return;
	}

	gSubMenuSelection = NUMBER_AddWithWraparound(gSubMenuSelection, gMenuScrollDirection, 1, UpperLimit);
	if (gMenuCursor == MENU_R_DCS) {
		if (gSubMenuSelection > 104) {
			gSelectedCodeType = CODE_TYPE_REVERSE_DIGITAL;
			gSelectedCode = gSubMenuSelection - 105;
		} else {
			gSelectedCodeType = CODE_TYPE_DIGITAL;
			gSelectedCode = gSubMenuSelection - 1;
		}

	} else {
		gSelectedCodeType = CODE_TYPE_CONTINUOUS_TONE;
		gSelectedCode = gSubMenuSelection - 1;
	}

	RADIO_SetupRegisters(true);

	if (gSelectedCodeType == CODE_TYPE_CONTINUOUS_TONE) {
		ScanPauseDelayIn10msec = 20;
	} else {
		ScanPauseDelayIn10msec = 30;
	}

	gUpdateDisplay = true;
}

void MENU_ShowCurrentSetting(void)
{
	switch (gMenuCursor) {
	case MENU_SQL:
		gSubMenuSelection = gEeprom.SQUELCH_LEVEL;
		break;

	case MENU_STEP:
		gSubMenuSelection = gTxVfo->STEP_SETTING;
		break;

	case MENU_TXP:
		gSubMenuSelection = gTxVfo->OUTPUT_POWER;
		break;

	case MENU_R_DCS:
		switch (gTxVfo->ConfigRX.CodeType) {
		case CODE_TYPE_DIGITAL:
			gSubMenuSelection = gTxVfo->ConfigRX.Code + 1;
			break;
		case CODE_TYPE_REVERSE_DIGITAL:
			gSubMenuSelection = gTxVfo->ConfigRX.Code + 105;
			break;
		default:
			gSubMenuSelection = 0;
			break;
		}
		break;

	case MENU_RESET:
		gSubMenuSelection = 0;
		break;

	case MENU_R_CTCS:
		if (gTxVfo->ConfigRX.CodeType == CODE_TYPE_CONTINUOUS_TONE) {
			gSubMenuSelection = gTxVfo->ConfigRX.Code + 1;
		} else {
			gSubMenuSelection = 0;
		}
		break;

	case MENU_T_DCS:
		switch (gTxVfo->ConfigTX.CodeType) {
		case CODE_TYPE_DIGITAL:
			gSubMenuSelection = gTxVfo->ConfigTX.Code + 1;
			break;
		case CODE_TYPE_REVERSE_DIGITAL:
			gSubMenuSelection = gTxVfo->ConfigTX.Code + 105;
			break;
		default:
			gSubMenuSelection = 0;
			break;
		}
		break;

	case MENU_T_CTCS:
		if (gTxVfo->ConfigTX.CodeType == CODE_TYPE_CONTINUOUS_TONE) {
			gSubMenuSelection = gTxVfo->ConfigTX.Code + 1;
		} else {
			gSubMenuSelection = 0;
		}
		break;

	case MENU_SFT_D:
		gSubMenuSelection = gTxVfo->FREQUENCY_DEVIATION_SETTING;
		break;

	case MENU_OFFSET:
		gSubMenuSelection = gTxVfo->FREQUENCY_OF_DEVIATION;
		break;

	case MENU_W_N:
		gSubMenuSelection = gTxVfo->CHANNEL_BANDWIDTH;
		break;

	case MENU_BCL:
		gSubMenuSelection = gTxVfo->BUSY_CHANNEL_LOCK;
		break;

	case MENU_MEM_CH:
		gSubMenuSelection = gEeprom.MrChannel[0];
		break;

	case MENU_SAVE:
		gSubMenuSelection = gEeprom.BATTERY_SAVE;
		break;

	case MENU_ABR:
		gSubMenuSelection = gEeprom.BACKLIGHT;
		break;

	case MENU_TDR:
		gSubMenuSelection = gEeprom.DUAL_WATCH;
		break;

	case MENU_WX:
		gSubMenuSelection = gEeprom.CROSS_BAND_RX_TX;
		break;

	case MENU_TOT:
		gSubMenuSelection = gEeprom.TX_TIMEOUT_TIMER;
		break;

	case MENU_SC_REV:
		gSubMenuSelection = gEeprom.SCAN_RESUME_MODE;
		break;

	case MENU_MDF:
		gSubMenuSelection = gEeprom.CHANNEL_DISPLAY_MODE;
		break;

	case MENU_AUTOLK:
		gSubMenuSelection = gEeprom.AUTO_KEYPAD_LOCK;
		break;

	case MENU_S_ADD1:
		gSubMenuSelection = gTxVfo->SCANLIST1_PARTICIPATION;
		break;

	case MENU_S_ADD2:
		gSubMenuSelection = gTxVfo->SCANLIST2_PARTICIPATION;
		break;

	case MENU_STE:
		gSubMenuSelection = gEeprom.TAIL_NOTE_ELIMINATION;
		break;

	case MENU_RP_STE:
		gSubMenuSelection = gEeprom.REPEATER_TAIL_TONE_ELIMINATION;
		break;

	case MENU_MIC:
		gSubMenuSelection = gEeprom.MIC_SENSITIVITY;
		break;

	case MENU_1_CALL:
		gSubMenuSelection = gEeprom.CHAN_1_CALL;
		break;

	case MENU_S_LIST:
		gSubMenuSelection = gEeprom.SCAN_LIST_DEFAULT + 1;
		break;

	case MENU_SLIST1:
		gSubMenuSelection = RADIO_FindNextChannel(0, 1, true, 0);
		break;

	case MENU_SLIST2:
		gSubMenuSelection = RADIO_FindNextChannel(0, 1, true, 1);
		break;

	case MENU_D_ST:
		gSubMenuSelection = gEeprom.DTMF_SIDE_TONE;
		break;

	case MENU_D_RSP:
		gSubMenuSelection = gEeprom.DTMF_DECODE_RESPONSE;
		break;

	case MENU_D_HOLD:
		gSubMenuSelection = gEeprom.DTMF_AUTO_RESET_TIME;
		break;

	case MENU_D_PRE:
		gSubMenuSelection = gEeprom.DTMF_PRELOAD_TIME / 10;
		break;

	case MENU_PTT_ID:
		gSubMenuSelection = gTxVfo->DTMF_PTT_ID_TX_MODE;
		break;

	case MENU_D_DCD:
		gSubMenuSelection = gTxVfo->DTMF_DECODING_ENABLE;
		break;

	case MENU_D_LIST:
		gSubMenuSelection = gDTMFChosenContact + 1;
		break;

	case MENU_ROGER:
		gSubMenuSelection = gEeprom.ROGER;
		break;

	case MENU_AM:
		gSubMenuSelection = gTxVfo->AM_CHANNEL_MODE;
		break;

	case MENU_DEL_CH:
		gSubMenuSelection = RADIO_FindNextChannel(gEeprom.MrChannel[0], 1, false, 1);
		break;

	case MENU_350TX:
		gSubMenuSelection = gSetting_350TX;
		break;

	case MENU_F_LOCK:
		gSubMenuSelection = gSetting_F_LOCK;
		break;

	case MENU_200TX:
		gSubMenuSelection = gSetting_200TX;
		break;

	case MENU_500TX:
		gSubMenuSelection = gSetting_500TX;
		break;

	case MENU_350EN:
		gSubMenuSelection = gSetting_350EN;
		break;

	case MENU_BATCAL:
		gSubMenuSelection = gBatteryCalibration[3];
		break;
	}
}

//

static void MENU_Key_DIGITS(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
	if (bKeyHeld || !bKeyPressed) {
		return;
	}

	INPUTBOX_Append(Key);
	uint16_t Value = 0;
	gRequestDisplayScreen = DISPLAY_MENU;

	if (!gIsInSubMenu) {
		switch (gInputBoxIndex) {
		case 1:
			Value = gInputBox[0];
			if (Value && Value <= gMenuListCount) {
				gMenuCursor = Value - 1;
				gFlagRefreshSetting = true;
				return;
			}
			break;

		case 2:
			gInputBoxIndex = 0;
			Value = (gInputBox[0] * 10) + gInputBox[1];
			if (Value && Value <= gMenuListCount) {
				gMenuCursor = Value - 1;
				gFlagRefreshSetting = true;
				return;
			}
			break;
		}
		gInputBoxIndex = 0;
	} else {
		switch (gMenuCursor) {
		case MENU_OFFSET:
			if (gInputBoxIndex < 6) {
				return;
			}
			gInputBoxIndex = 0;
			uint32_t Frequency;
			NUMBER_Get(gInputBox, &Frequency);
			Frequency += 75;
			gSubMenuSelection = FREQUENCY_FloorToStep(Frequency, gTxVfo->StepFrequency, 0);
			break;

		case MENU_MEM_CH:
		case MENU_DEL_CH:
		case MENU_1_CALL:
			if (gInputBoxIndex < 3) {
				gRequestDisplayScreen = DISPLAY_MENU;
				return;
			}
			gInputBoxIndex = 0;
			Value = ((gInputBox[0] * 100) + (gInputBox[1] * 10) + gInputBox[2]) - 1;
			if (IS_MR_CHANNEL(Value)) {
				gSubMenuSelection = Value;
			}
			break;

		case MENU_BATCAL:
			gSubMenuSelection = INPUTBOX_GetValue();
			// not yet enough characters
			if (gInputBoxIndex < 4) {
				return;
			}
			gInputBoxIndex = 0;
			break;

		default:
			uint16_t Min, Max;
			if (!MENU_GetLimits(gMenuCursor, &Min, &Max)) {
				gInputBoxIndex = 0;
				return;
			}
			uint8_t Offset = 2;
			if (Max < 10) {
				Offset = 1;
			}
			if (Max >= 100) {
				Offset = 3;
			}
			switch (gInputBoxIndex) {
			case 1:
				Value = gInputBox[0];
				break;

			case 2:
				Value = (gInputBox[0] * 10) + gInputBox[1];
				break;

			case 3:
				Value = (gInputBox[0] * 100) + (gInputBox[1] * 10) + gInputBox[2];
				break;
			}
			if (Offset == gInputBoxIndex) {
				gInputBoxIndex = 0;
			}
			if (Value <= Max) {
				gSubMenuSelection = Value;
			}
			break;
		}
	}
}

static void MENU_Key_EXIT(bool bKeyPressed, bool bKeyHeld)
{
	if (!bKeyHeld && bKeyPressed) {
		if (gCssScanMode == CSS_SCAN_MODE_OFF) {
			if (gIsInSubMenu) {
				if (gInputBoxIndex == 0 || gMenuCursor != MENU_OFFSET) {
					gIsInSubMenu = false;
					gAskForConfirmation = 0;
					gInputBoxIndex = 0;
					gFlagRefreshSetting = true;
				} else {
					gInputBoxIndex--;
					gInputBox[gInputBoxIndex] = 10;
				}
				gRequestDisplayScreen = DISPLAY_MENU;
				return;
			}
			gRequestDisplayScreen = DISPLAY_MAIN;
		} else {
			MENU_StopCssScan();
			gRequestDisplayScreen = DISPLAY_MENU;
		}
		gPttWasReleased = true;
	}
}

static void MENU_Key_MENU(bool bKeyPressed, bool bKeyHeld)
{
	if (!bKeyHeld && bKeyPressed) {
		gRequestDisplayScreen = DISPLAY_MENU;
		if (!gIsInSubMenu) {
			gAskForConfirmation = 0;
			gIsInSubMenu = true;
		} else {
			if (gMenuCursor == MENU_RESET || gMenuCursor == MENU_MEM_CH || gMenuCursor == MENU_DEL_CH) {
				switch (gAskForConfirmation) {
				case 0:
					gAskForConfirmation = 1;
					break;
				case 1:
					gAskForConfirmation = 2;
					UI_DisplayMenu(); // Needed for "WAIT!" string
					if (gMenuCursor == MENU_RESET) {
						MENU_AcceptSetting();
						NVIC_SystemReset();
					}
					gFlagAcceptSetting = true;
					gIsInSubMenu = false;
					gAskForConfirmation = 0;
				}
			} else {
				gFlagAcceptSetting = true;
				gIsInSubMenu = false;
			}
			gCssScanMode = CSS_SCAN_MODE_OFF;
		}
		gInputBoxIndex = 0;
	}
}

static void MENU_Key_STAR(bool bKeyPressed, bool bKeyHeld)
{
	if (!bKeyHeld && bKeyPressed) {
		RADIO_SelectVfos();
		if (!gRxVfo->IsAM) {
			if (gMenuCursor == MENU_R_CTCS || gMenuCursor == MENU_R_DCS) {
				if (gCssScanMode == CSS_SCAN_MODE_OFF) {
					MENU_StartCssScan(1);
					gRequestDisplayScreen = DISPLAY_MENU;
				} else {
					MENU_StopCssScan();
					gRequestDisplayScreen = DISPLAY_MENU;
				}
			}
			gPttWasReleased = true;
			return;
		}
	}
}

static void MENU_Key_UP_DOWN(bool bKeyPressed, bool bKeyHeld, int8_t Direction)
{
	if (!bKeyPressed) {
		return;
	}
	if (!bKeyHeld) {
		gInputBoxIndex = 0;
	}
	if (gCssScanMode != CSS_SCAN_MODE_OFF) {
		MENU_StartCssScan(Direction);
		gPttWasReleased = true;
		gRequestDisplayScreen = DISPLAY_MENU;
		return;
	}
	if (!gIsInSubMenu) {
		gMenuCursor = NUMBER_AddWithWraparound(gMenuCursor, -Direction, 0, gMenuListCount - 1);
		gFlagRefreshSetting = true;
		gRequestDisplayScreen = DISPLAY_MENU;
		return;
	}

	bool bCheckScanList;
	uint8_t VFO = 0;

	switch (gMenuCursor) {
	case MENU_OFFSET:
		int32_t Offset = (Direction * gTxVfo->StepFrequency) + gSubMenuSelection;
		if (Offset < 0) {
			Offset = 99999990;
		} else if (Offset > 99999990) {
			Offset = 0;
		}
		gSubMenuSelection = FREQUENCY_FloorToStep(Offset, gTxVfo->StepFrequency, 0);
		gRequestDisplayScreen = DISPLAY_MENU;
		return;

	case MENU_DEL_CH:
	case MENU_1_CALL:
		bCheckScanList = false;
		break;

	case MENU_SLIST2:
		VFO = 1;
		// Fallthrough
	case MENU_SLIST1:
		bCheckScanList = true;
		break;

	default:
		//MENU_ClampSelection(Direction) called once
		uint16_t Min, Max;
		if (MENU_GetLimits(gMenuCursor, &Min, &Max)) {
			uint16_t Selection = gSubMenuSelection;
			if (Selection < Min) {
				Selection = Min;
			}
			if (Selection > Max) {
				Selection = Max;
			}
			gSubMenuSelection = NUMBER_AddWithWraparound(Selection, Direction, Min, Max);
		}
		gRequestDisplayScreen = DISPLAY_MENU;
		return;
	}

	uint8_t Channel = RADIO_FindNextChannel(gSubMenuSelection + Direction, Direction, bCheckScanList, VFO);
	if (Channel != 0xFF) {
		gSubMenuSelection = Channel;
	}
	gRequestDisplayScreen = DISPLAY_MENU;
}

void MENU_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
	switch (Key) {
	case KEY_0: case KEY_1: case KEY_2: case KEY_3:
	case KEY_4: case KEY_5: case KEY_6: case KEY_7:
	case KEY_8: case KEY_9:
		MENU_Key_DIGITS(Key, bKeyPressed, bKeyHeld);
		break;
	case KEY_MENU:
		MENU_Key_MENU(bKeyPressed, bKeyHeld);
		break;
	case KEY_UP:
		MENU_Key_UP_DOWN(bKeyPressed, bKeyHeld, 1);
		break;
	case KEY_DOWN:
		MENU_Key_UP_DOWN(bKeyPressed, bKeyHeld, -1);
		break;
	case KEY_EXIT:
		MENU_Key_EXIT(bKeyPressed, bKeyHeld);
		break;
	case KEY_STAR:
		MENU_Key_STAR(bKeyPressed, bKeyHeld);
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
	if (gScreenToDisplay == DISPLAY_MENU && gMenuCursor == MENU_BATCAL) {
		gVoltageMenuCountdown = 0x20;
	}
}

