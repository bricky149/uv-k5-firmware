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

#include "app/generic.h"
#include "app/scanner.h"
#include "driver/bk4819.h"
#include "frequencies.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/inputbox.h"
#include "ui/ui.h"

DCS_CodeType_t gScanCssResultType;
uint8_t gScanCssResultCode;
bool gFlagStartScan;
bool gFlagStopScan;
bool gScanSingleFrequency;
uint8_t gScannerEditState;
uint8_t gScanChannel;
uint32_t gScanFrequency;
bool gScanPauseMode;
SCAN_CssState_t gScanCssState;
volatile bool gScheduleScanListen = true;
volatile uint16_t ScanPauseDelayIn10msec;
uint8_t gScanProgressIndicator;
uint8_t gScanHitCount;
bool gScanUseCssResult;
int8_t gScanState;
bool bScanKeepFrequency;

static void SCANNER_Key_EXIT(void)
{
	switch (gScannerEditState) {
	case 0:
		gRequestDisplayScreen = DISPLAY_MAIN;
		gEeprom.CROSS_BAND_RX_TX = gBackupCROSS_BAND_RX_TX;
		gUpdateStatus = true;
		gFlagStopScan = true;
		gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
		gFlagResetVfos = true;
		break;

	case 1:
		if (gInputBoxIndex > 0) {
			gInputBoxIndex--;
			gInputBox[gInputBoxIndex] = 10;
			gRequestDisplayScreen = DISPLAY_SCANNER;
			break;
		}
		// Fallthrough

	case 2:
		gScannerEditState = 0;
		gRequestDisplayScreen = DISPLAY_SCANNER;
		break;
	}
}

static void SCANNER_Key_MENU(void)
{
	uint8_t Channel;

	if (gScanCssState == SCAN_CSS_STATE_OFF && !gScanSingleFrequency) {
		return;
	}
	if (gScanCssState == SCAN_CSS_STATE_SCANNING && gScanSingleFrequency) {
		return;
	}
	if (gScanCssState == SCAN_CSS_STATE_FAILED) {
		return;
	}

	switch (gScannerEditState) {
	case 0:
		if (!gScanSingleFrequency) {
			uint32_t Freq250;
			uint32_t Freq625;
			int16_t Delta250;
			int16_t Delta625;

			Freq250 = FREQUENCY_FloorToStep(gScanFrequency, 250, 0);
			Freq625 = FREQUENCY_FloorToStep(gScanFrequency, 625, 0);
			Delta250 = (short)gScanFrequency - (short)Freq250;
			if (125 < Delta250) {
				Delta250 = 250 - Delta250;
				Freq250 += 250;
			}
			Delta625 = (short)gScanFrequency - (short)Freq625;
			if (312 < Delta625) {
				Delta625 = 625 - Delta625;
				Freq625 += 625;
			}
			if (Delta625 < Delta250) {
				gStepSetting = STEP_6_25kHz;
				gScanFrequency = Freq625;
			} else {
				gStepSetting = STEP_2_5kHz;
				gScanFrequency = Freq250;
			}
		}
		if (IS_MR_CHANNEL(gTxVfo->CHANNEL_SAVE)) {
			gScannerEditState = 1;
			gScanChannel = gTxVfo->CHANNEL_SAVE;
			gShowChPrefix = RADIO_CheckValidChannel(gTxVfo->CHANNEL_SAVE, false, 0);
		} else {
			gScannerEditState = 2;
		}
		gScanCssState = SCAN_CSS_STATE_FOUND;
		gRequestDisplayScreen = DISPLAY_SCANNER;
		break;

	case 1:
		if (gInputBoxIndex == 0) {
			gRequestDisplayScreen = DISPLAY_SCANNER;
			gScannerEditState = 2;
		}
		break;

	case 2:
		if (!gScanSingleFrequency) {
			RADIO_InitInfo(gTxVfo, gTxVfo->CHANNEL_SAVE, FREQUENCY_GetBand(gScanFrequency), gScanFrequency);
			if (gScanUseCssResult) {
				gTxVfo->ConfigRX.CodeType = gScanCssResultType;
				gTxVfo->ConfigRX.Code = gScanCssResultCode;
			}
			gTxVfo->ConfigTX = gTxVfo->ConfigRX;
			gTxVfo->STEP_SETTING = gStepSetting;
		} else {
			RADIO_ConfigureChannel(0, VFO_CONFIGURE_RELOAD);
			RADIO_ConfigureChannel(1, VFO_CONFIGURE_RELOAD);
			gTxVfo->ConfigRX.CodeType = gScanCssResultType;
			gTxVfo->ConfigRX.Code = gScanCssResultCode;
			gTxVfo->ConfigTX.CodeType = gScanCssResultType;
			gTxVfo->ConfigTX.Code = gScanCssResultCode;
		}

		if (IS_MR_CHANNEL(gTxVfo->CHANNEL_SAVE)) {
			Channel = gScanChannel;
			gEeprom.MrChannel[gEeprom.TX_VFO] = Channel;
		} else {
			Channel = gTxVfo->Band + FREQ_CHANNEL_FIRST;
			gEeprom.FreqChannel[gEeprom.TX_VFO] = Channel;
		}
		gTxVfo->CHANNEL_SAVE = Channel;
		gEeprom.ScreenChannel[gEeprom.TX_VFO] = Channel;
		gRequestDisplayScreen = DISPLAY_SCANNER;
		gRequestSaveChannel = 2;
		gScannerEditState = 0;
		break;

	default:
		break;
	}
}

static void SCANNER_Key_UP_DOWN(bool bKeyPressed, bool bKeyHeld, int8_t Direction)
{
	if (!bKeyPressed) {
		return;
	}
	if (!bKeyHeld) {
		gInputBoxIndex = 0;
	}
	if (gScannerEditState == 1) {
		gScanChannel = NUMBER_AddWithWraparound(gScanChannel, Direction, 0, 199);
		gShowChPrefix = RADIO_CheckValidChannel(gScanChannel, false, 0);
		gRequestDisplayScreen = DISPLAY_SCANNER;
	}
}

void SCANNER_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
	switch (Key) {
	case KEY_MENU:
		if (!bKeyHeld && bKeyPressed) {
			SCANNER_Key_MENU();
		}
		break;
	case KEY_UP:
		SCANNER_Key_UP_DOWN(bKeyPressed, bKeyHeld, 1);
		break;
	case KEY_DOWN:
		SCANNER_Key_UP_DOWN(bKeyPressed, bKeyHeld, -1);
		break;
	case KEY_EXIT:
		if (!bKeyHeld && bKeyPressed) {
			SCANNER_Key_EXIT();
		}
		break;
	case KEY_F:
		GENERIC_Key_F(bKeyPressed, bKeyHeld);
		break;
	default:
		break;
	}
}

void SCANNER_Start(void)
{
	uint8_t BackupStep;
	uint16_t BackupFrequency;

	BK4819_DisableFrequencyScan();
	BK4819_Disable();
	RADIO_SelectVfos();

	BackupStep = gRxVfo->STEP_SETTING;
	BackupFrequency = gRxVfo->StepFrequency;

	RADIO_InitInfo(gRxVfo, gRxVfo->CHANNEL_SAVE, gRxVfo->Band, gRxVfo->pRX->Frequency);

	gRxVfo->STEP_SETTING = BackupStep;
	gRxVfo->StepFrequency = BackupFrequency;

	RADIO_SetupRegisters(true);

	if (gScanSingleFrequency) {
		gScanCssState = SCAN_CSS_STATE_SCANNING;
		gScanFrequency = gRxVfo->pRX->Frequency;
		gStepSetting = gRxVfo->STEP_SETTING;
		BK4819_SelectFilter(gScanFrequency);
		BK4819_SetScanFrequency(gScanFrequency);
	} else {
		gScanCssState = SCAN_CSS_STATE_OFF;
		gScanFrequency = 0xFFFFFFFF;
		BK4819_SelectFilter(0xFFFFFFFF);
		BK4819_EnableFrequencyScan();
	}
	gScanDelay = 21;
	gScanCssResultCode = 0xFF;
	gScanCssResultType = 0xFF;
	gScanHitCount = 0;
	gScanUseCssResult = false;
	gDTMF_RequestPending = false;
	g_CxCSS_TAIL_Found = false;
	g_CDCSS_Lost = false;
	gCDCSSCodeType = 0;
	g_CTCSS_Lost = false;
	g_SquelchLost = false;
	gScannerEditState = 0;
	gScanProgressIndicator = 0;
}

void SCANNER_Stop(void)
{
	uint8_t Previous;

	Previous = gRestoreMrChannel;
	gScanState = SCAN_OFF;

	if (!bScanKeepFrequency) {
		if (IS_MR_CHANNEL(gNextMrChannel)) {
			gEeprom.MrChannel[gEeprom.RX_VFO] = gRestoreMrChannel;
			gEeprom.ScreenChannel[gEeprom.RX_VFO] = Previous;
			RADIO_ConfigureChannel(gEeprom.RX_VFO, VFO_CONFIGURE_RELOAD);
		} else {
			gRxVfo->ConfigRX.Frequency = gRestoreFrequency;
			RADIO_ApplyOffset(gRxVfo);
			RADIO_ConfigureSquelchAndOutputPower(gRxVfo);
		}
		RADIO_SetupRegisters(true);
		gUpdateDisplay = true;
		return;
	}

	if (!IS_MR_CHANNEL(gRxVfo->CHANNEL_SAVE)) {
		RADIO_ApplyOffset(gRxVfo);
		RADIO_ConfigureSquelchAndOutputPower(gRxVfo);
		SETTINGS_SaveChannel(gRxVfo->CHANNEL_SAVE, gEeprom.RX_VFO, gRxVfo, 1);
		return;
	}

	SETTINGS_SaveVfoIndices();
}

