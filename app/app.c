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
#include "app/dtmf.h"
#if defined(ENABLE_FMRADIO)
#include "app/fm.h"
#endif
#include "app/generic.h"
#include "app/main.h"
#include "app/menu.h"
#include "app/scanner.h"
#if defined(ENABLE_UART)
#include "app/uart.h"
#endif
#include "ARMCM0.h"
#include "board.h"
#include "bsp/dp32g030/gpio.h"
#include "driver/backlight.h"
#if defined(ENABLE_FMRADIO)
#include "driver/bk1080.h"
#endif
#include "driver/bk4819.h"
#include "driver/gpio.h"
#include "driver/keyboard.h"
#include "driver/st7565.h"
#include "driver/system.h"
#include "dtmf.h"
#include "frequencies.h"
#include "functions.h"
#include "helper/battery.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/battery.h"
#include "ui/inputbox.h"
#include "ui/menu.h"
#include "ui/rssi.h"
#include "ui/status.h"
#include "ui/ui.h"

static void APP_ProcessKey(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld);

static void APP_CheckForIncoming(void)
{
	if (gScanState == SCAN_OFF) {
		if (gCssScanMode != CSS_SCAN_MODE_OFF && gRxReceptionMode == RX_MODE_NONE) {
			ScanPauseDelayIn10msec = 100;
			gScheduleScanListen = false;
			gRxReceptionMode = RX_MODE_DETECTED;
		}
		if (gEeprom.DUAL_WATCH == DUAL_WATCH_OFF) {
			FUNCTION_Select(FUNCTION_INCOMING);
			return;
		}
		if (gRxReceptionMode != RX_MODE_NONE) {
			FUNCTION_Select(FUNCTION_INCOMING);
			return;
		}
		gDualWatchCountdown = 100;
		gScheduleDualWatch = false;
	} else {
		if (gRxReceptionMode != RX_MODE_NONE) {
			FUNCTION_Select(FUNCTION_INCOMING);
			return;
		}
		ScanPauseDelayIn10msec = 20;
		gScheduleScanListen = false;
	}
	gRxReceptionMode = RX_MODE_DETECTED;
	FUNCTION_Select(FUNCTION_INCOMING);
}

static void APP_EndReceive(uint8_t Mode)
{
#define END_OF_RX_MODE_SKIP 0
#define END_OF_RX_MODE_END  1
#define END_OF_RX_MODE_TTE  2

	switch (Mode) {
	case END_OF_RX_MODE_END:
		RADIO_SetupRegisters(true);
		gUpdateDisplay = true;
		if (gScanState != SCAN_OFF) {
			switch (gEeprom.SCAN_RESUME_MODE) {
			case SCAN_RESUME_CO:
				ScanPauseDelayIn10msec = 360;
				gScheduleScanListen = false;
				break;
			case SCAN_RESUME_SE:
				SCANNER_Stop();
				break;
			}
		}
		break;

	case END_OF_RX_MODE_TTE:
		if (gEeprom.TAIL_NOTE_ELIMINATION) {
			GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
			gEnableSpeaker = false;
			gTailNoteEliminationCountdown = 20;
			gFlagTteComplete = false;
			gEndOfRxDetectedMaybe = true;
		}
		break;
	}
}

static void APP_HandleReceive(void)
{
#define END_OF_RX_MODE_SKIP 0
#define END_OF_RX_MODE_END  1
#define END_OF_RX_MODE_TTE  2

	uint8_t Mode = END_OF_RX_MODE_SKIP;

	if (gFlagTteComplete) {
		Mode = END_OF_RX_MODE_END;
	} else if (gScanState != SCAN_OFF && IS_FREQ_CHANNEL(gNextMrChannel)) {
		if (g_SquelchLost) {
			return;
		}
		Mode = END_OF_RX_MODE_END;
	}
	switch (gCurrentCodeType) {
	case CODE_TYPE_CONTINUOUS_TONE:
	case CODE_TYPE_DIGITAL:
	case CODE_TYPE_REVERSE_DIGITAL:
		Mode = END_OF_RX_MODE_END;
		break;

	default:
		break;
	}

	if (Mode == END_OF_RX_MODE_END) {
		APP_EndReceive(Mode);
		return;
	}

	if (g_SquelchLost) {
		if (!gEndOfRxDetectedMaybe) {
			switch (gCurrentCodeType) {
			case CODE_TYPE_OFF:
				if (gEeprom.SQUELCH_LEVEL) {
					if (g_CxCSS_TAIL_Found) {
						Mode = END_OF_RX_MODE_TTE;
						g_CxCSS_TAIL_Found = false;
					}
				}
				break;

			case CODE_TYPE_CONTINUOUS_TONE:
				if (g_CTCSS_Lost) {
					gFoundCTCSS = false;
				} else if (!gFoundCTCSS) {
					gFoundCTCSS = true;
					gFoundCTCSSCountdown = 100;
				}
				if (g_CxCSS_TAIL_Found) {
					Mode = END_OF_RX_MODE_TTE;
					g_CxCSS_TAIL_Found = false;
				}
				break;

			case CODE_TYPE_DIGITAL:
			case CODE_TYPE_REVERSE_DIGITAL:
				if (g_CDCSS_Lost && gCDCSSCodeType == CDCSS_POSITIVE_CODE) {
					gFoundCDCSS = false;
				} else if (!gFoundCDCSS) {
					gFoundCDCSS = true;
					gFoundCDCSSCountdown = 100;
				}
				if (g_CxCSS_TAIL_Found) {
					if (BK4819_GetCTCType() == 1) {
						Mode = END_OF_RX_MODE_TTE;
					}
					g_CxCSS_TAIL_Found = false;
				}
				break;

			default:
				break;
			}
		}
	} else {
		Mode = END_OF_RX_MODE_END;
	}

	if (!gEndOfRxDetectedMaybe && Mode == END_OF_RX_MODE_SKIP &&
		gNextTimeslice40ms && gEeprom.TAIL_NOTE_ELIMINATION &&
		(gCurrentCodeType == CODE_TYPE_DIGITAL || gCurrentCodeType == CODE_TYPE_REVERSE_DIGITAL) &&
		BK4819_GetCTCType() == 1)
	{
		Mode = END_OF_RX_MODE_TTE;
	} else {
		gNextTimeslice40ms = false;
	}
	APP_EndReceive(Mode);
}

static void APP_HandleFunction(void)
{
	switch (gCurrentFunction) {
	case FUNCTION_FOREGROUND:
		if (g_SquelchLost) {
			APP_CheckForIncoming();
		}
		break;

	case FUNCTION_POWER_SAVE:
		if (!gRxIdleMode && g_SquelchLost) {
			APP_CheckForIncoming();
		}
		break;

	case FUNCTION_INCOMING:
		// APP_HandleIncoming();
		if (!g_SquelchLost) {
			FUNCTION_Select(FUNCTION_FOREGROUND);
			gUpdateDisplay = true;
			return;
		}
		bool bFlag = (gScanState == SCAN_OFF && gCurrentCodeType == CODE_TYPE_OFF);
		if (g_CTCSS_Lost && gCurrentCodeType == CODE_TYPE_CONTINUOUS_TONE) {
			bFlag = true;
			gFoundCTCSS = false;
		}
		if (g_CDCSS_Lost && gCDCSSCodeType == CDCSS_POSITIVE_CODE && (gCurrentCodeType == CODE_TYPE_DIGITAL || gCurrentCodeType == CODE_TYPE_REVERSE_DIGITAL)) {
			gFoundCDCSS = false;
		} else if (!bFlag) {
			return;
		}
		DTMF_HandleRequest();
		if (gScanState == SCAN_OFF && gCssScanMode == CSS_SCAN_MODE_OFF) {
			if (gRxVfo->DTMF_DECODING_ENABLE) {
				if (gDTMF_CallState == DTMF_CALL_STATE_NONE) {
					if (gRxReceptionMode == RX_MODE_DETECTED) {
						gDualWatchCountdown = 500;
						gScheduleDualWatch = false;
						gRxReceptionMode = RX_MODE_LISTENING;
					}
					return;
				}
			}
		}
		APP_StartListening(FUNCTION_RECEIVE);
		break;

	case FUNCTION_RECEIVE:
		APP_HandleReceive();
		break;
		
	default:
		break;
	}
}

void APP_StartListening(FUNCTION_Type_t Function)
{
#if defined(ENABLE_FMRADIO)
		if (gFmRadioMode) {
			BK1080_Sleep();
		}
#endif
	gVFO_RSSI_Level[!gEeprom.RX_VFO] = 0;
	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
	gEnableSpeaker = true;
	BACKLIGHT_TurnOn();

	if (gScanState != SCAN_OFF) {
		switch (gEeprom.SCAN_RESUME_MODE) {
		case SCAN_RESUME_TO:
			if (!gScanPauseMode) {
				ScanPauseDelayIn10msec = 500;
				gScheduleScanListen = false;
				gScanPauseMode = true;
			}
			break;
		case SCAN_RESUME_CO:
		case SCAN_RESUME_SE:
			ScanPauseDelayIn10msec = 0;
			gScheduleScanListen = false;
			break;
		}
		bScanKeepFrequency = true;
	}
	if (gCssScanMode != CSS_SCAN_MODE_OFF) {
		gCssScanMode = CSS_SCAN_MODE_FOUND;
	}
	if (gScanState == SCAN_OFF && gCssScanMode == CSS_SCAN_MODE_OFF && gEeprom.DUAL_WATCH != DUAL_WATCH_OFF) {
		gRxVfoIsActive = true;
		gDualWatchCountdown = 360;
		gScheduleDualWatch = false;
	}

	BK4819_SetModulation(gRxVfo->ModulationType);
	if (gRxVfo->ModulationType != MOD_FM) {
		BK4819_WriteRegister(BK4819_REG_48, 0xB3A8);
		BK4819_DisableAGC();
		BK4819_SetCompander(0);
		BK4819_SetAF(BK4819_AF_AM);
	} else {
		BK4819_WriteRegister(BK4819_REG_48, 0xB000
				| (gCalibration.VOLUME_GAIN << 4)
				| (gCalibration.DAC_GAIN << 0)
				);
		BK4819_EnableAGC();
		BK4819_SetCompander(gRxVfo->CompanderMode);
		BK4819_SetAF(BK4819_AF_OPEN);
	}

	FUNCTION_Select(Function);
	if (Function == FUNCTION_MONITOR
#if defined(ENABLE_FMRADIO)
		|| gFmRadioMode
#endif
		) {
		GUI_SelectNextDisplay(DISPLAY_MAIN);
		return;
	}
	gUpdateDisplay = true;
}

void APP_SetFrequencyByStep(VFO_Info_t *pInfo, int8_t Step)
{
	uint32_t Frequency;

	Frequency = pInfo->ConfigRX.Frequency + (Step * pInfo->StepFrequency);
	if (Frequency > gUpperLimitFrequencyBandTable[pInfo->Band]) {
		pInfo->ConfigRX.Frequency = gLowerLimitFrequencyBandTable[pInfo->Band];
	} else if (Frequency < gLowerLimitFrequencyBandTable[pInfo->Band]) {
		pInfo->ConfigRX.Frequency = FREQUENCY_FloorToStep(gUpperLimitFrequencyBandTable[pInfo->Band], pInfo->StepFrequency, gLowerLimitFrequencyBandTable[pInfo->Band]);
	} else {
		pInfo->ConfigRX.Frequency = Frequency;
	}
}

static void FREQ_NextChannel(void)
{
	APP_SetFrequencyByStep(gRxVfo, gScanState);
	RADIO_ApplyOffset(gRxVfo);
	RADIO_ConfigureSquelchAndOutputPower(gRxVfo);
	RADIO_SetupRegisters(true);
	gUpdateDisplay = true;
	ScanPauseDelayIn10msec = 10;
	bScanKeepFrequency = false;
}

static void MR_NextChannel(void)
{
	const uint8_t Ch1 = gEeprom.SCANLIST_PRIORITY_CH1[gEeprom.SCAN_LIST_DEFAULT];
	const uint8_t Ch2 = gEeprom.SCANLIST_PRIORITY_CH2[gEeprom.SCAN_LIST_DEFAULT];
	uint8_t PreviousCh, Ch;
	bool bEnabled;

	PreviousCh = gNextMrChannel;
	bEnabled = gEeprom.SCAN_LIST_ENABLED[gEeprom.SCAN_LIST_DEFAULT];
	if (bEnabled) {
		if (gCurrentScanList == 0) {
			gPreviousMrChannel = gNextMrChannel;
			if (RADIO_CheckValidChannel(Ch1, false, 0)) {
				gNextMrChannel = Ch1;
			} else {
				gCurrentScanList = 1;
			}
		}
		if (gCurrentScanList == 1) {
			if (RADIO_CheckValidChannel(Ch2, false, 0)) {
				gNextMrChannel = Ch2;
			} else {
				gCurrentScanList = 2;
			}
		}
		if (gCurrentScanList == 2) {
			gNextMrChannel = gPreviousMrChannel;
			Ch = RADIO_FindNextChannel(gNextMrChannel + gScanState, gScanState, true, gEeprom.SCAN_LIST_DEFAULT);
			if (Ch == 0xFF) {
				return;
			}

			gNextMrChannel = Ch;
		}
	} else {
		Ch = RADIO_FindNextChannel(gNextMrChannel + gScanState, gScanState, true, gEeprom.SCAN_LIST_DEFAULT);
		if (Ch == 0xFF) {
			return;
		}

		gNextMrChannel = Ch;
	}

	if (PreviousCh != gNextMrChannel) {
		gEeprom.MrChannel[gEeprom.RX_VFO] = gNextMrChannel;
		gEeprom.ScreenChannel[gEeprom.RX_VFO] = gNextMrChannel;
		RADIO_ConfigureChannel(gEeprom.RX_VFO, VFO_CONFIGURE_RELOAD);
		RADIO_SetupRegisters(true);
		gUpdateDisplay = true;
	}
	ScanPauseDelayIn10msec = 20;
	bScanKeepFrequency = false;
	if (bEnabled) {
		gCurrentScanList++;
		if (gCurrentScanList > 2) {
			gCurrentScanList = 0;
		}
	}
}

static void DUALWATCH_Alternate(void)
{
	gEeprom.RX_VFO = !gEeprom.RX_VFO;
	gRxVfo = &gEeprom.VfoInfo[gEeprom.RX_VFO];

	RADIO_SetupRegisters(false);

	gDualWatchCountdown = 10;
}

void APP_CheckRadioInterrupts(void)
{
	while (BK4819_ReadRegister(BK4819_REG_0C) & 1U) {
		BK4819_WriteRegister(BK4819_REG_02, 0);
		uint16_t Mask = BK4819_ReadRegister(BK4819_REG_02);
		if (Mask & BK4819_REG_02_DTMF_5TONE_FOUND) {
			gDTMF_RequestPending = true;
			gDTMF_RecvTimeout = 5;
			if (gDTMF_WriteIndex > 15) {
				for (uint8_t i = 0; i < sizeof(gDTMF_Received) - 1; i++) {
					gDTMF_Received[i] = gDTMF_Received[i + 1];
				}
				gDTMF_WriteIndex = 15;
			}
			gDTMF_Received[gDTMF_WriteIndex++] = DTMF_GetCharacter(BK4819_GetDTMF_5TONE_Code());
			if (gCurrentFunction == FUNCTION_RECEIVE) {
				DTMF_HandleRequest();
			}
		}
		if (Mask & BK4819_REG_02_CxCSS_TAIL) {
			g_CxCSS_TAIL_Found = true;
		}
		if (Mask & BK4819_REG_02_CDCSS_LOST) {
			g_CDCSS_Lost = true;
			gCDCSSCodeType = BK4819_GetCDCSSCodeType();
		}
		if (Mask & BK4819_REG_02_CDCSS_FOUND) {
			g_CDCSS_Lost = false;
		}
		if (Mask & BK4819_REG_02_CTCSS_LOST) {
			g_CTCSS_Lost = true;
		}
		if (Mask & BK4819_REG_02_CTCSS_FOUND) {
			g_CTCSS_Lost = false;
		}
		if (Mask & BK4819_REG_02_SQUELCH_LOST) {
			g_SquelchLost = true;
			BK4819_SetGpioOut(BK4819_GPIO6_PIN2_GREEN);
		}
		if (Mask & BK4819_REG_02_SQUELCH_FOUND) {
			g_SquelchLost = false;
			BK4819_ClearGpioOut(BK4819_GPIO6_PIN2_GREEN);
		}
	}
}

void APP_EndTransmission(void)
{
	RADIO_SendEndOfTransmission();
	RADIO_EnableCxCSS();
	RADIO_SetupRegisters(false);
}

void APP_Update(void)
{
	if (gReducedService) {
		return;
	}

	if (gCurrentFunction == FUNCTION_TRANSMIT && gTxTimeoutReached) {
		gTxTimeoutReached = false;
		gFlagEndTransmission = true;
		APP_EndTransmission();
		RADIO_SetVfoState(VFO_STATE_TIMEOUT);
		gUpdateDisplay = true;
	}
	if (gCurrentFunction != FUNCTION_TRANSMIT) {
		APP_HandleFunction();
	}

	if (gScreenToDisplay != DISPLAY_SCANNER && gScanState != SCAN_OFF && gScheduleScanListen && !gPttIsPressed) {
		if (IS_FREQ_CHANNEL(gNextMrChannel)) {
			if (gCurrentFunction == FUNCTION_INCOMING) {
				APP_StartListening(FUNCTION_RECEIVE);
			} else {
				FREQ_NextChannel();
			}
		} else {
			if (gCurrentCodeType == CODE_TYPE_OFF && gCurrentFunction == FUNCTION_INCOMING) {
				APP_StartListening(FUNCTION_RECEIVE);
			} else {
				MR_NextChannel();
			}
		}
		gScanPauseMode = false;
		gRxReceptionMode = RX_MODE_NONE;
		gScheduleScanListen = false;
	}

	if (gCssScanMode == CSS_SCAN_MODE_SCANNING && gScheduleScanListen) {
		MENU_SelectNextCode();
		gScheduleScanListen = false;
	}

	if (gScreenToDisplay != DISPLAY_SCANNER && gEeprom.DUAL_WATCH != DUAL_WATCH_OFF) {
		if (gScheduleDualWatch) {
			if (gScanState == SCAN_OFF && gCssScanMode == CSS_SCAN_MODE_OFF) {
				if (!gPttIsPressed
#if defined(ENABLE_FMRADIO)
						&& !gFmRadioMode
#endif
						&& gDTMF_CallState == DTMF_CALL_STATE_NONE
						&& gCurrentFunction != FUNCTION_POWER_SAVE) {
					DUALWATCH_Alternate();
					if (gRxVfoIsActive && gScreenToDisplay == DISPLAY_MAIN) {
						GUI_SelectNextDisplay(DISPLAY_MAIN);
					}
					gRxVfoIsActive = false;
					gScanPauseMode = false;
					gRxReceptionMode = RX_MODE_NONE;
					gScheduleDualWatch = false;
				}
			}
		}
	}

	if (gSchedulePowerSave) {
		if (gEeprom.BATTERY_SAVE == 0 || gScanState != SCAN_OFF || gCssScanMode != CSS_SCAN_MODE_OFF
#if defined(ENABLE_FMRADIO)
				|| gFmRadioMode
#endif
				|| gPttIsPressed || gScreenToDisplay != DISPLAY_MAIN || gKeyBeingHeld
				|| gDTMF_CallState != DTMF_CALL_STATE_NONE
				) {
			gBatterySaveCountdown = 1000;
		} else {
			FUNCTION_Select(FUNCTION_POWER_SAVE);
		}
		gSchedulePowerSave = false;
	}

	if (gBatterySaveCountdownExpired && gCurrentFunction == FUNCTION_POWER_SAVE) {
		if (gRxIdleMode) {
			BK4819_EnableRX();

			if (gEeprom.DUAL_WATCH != DUAL_WATCH_OFF && gScanState == SCAN_OFF && gCssScanMode == CSS_SCAN_MODE_OFF) {
				DUALWATCH_Alternate();
				gUpdateRSSI = false;
			}
			FUNCTION_Init();
			gBatterySave = 10;
			gRxIdleMode = false;
		} else if (gEeprom.DUAL_WATCH == DUAL_WATCH_OFF || gScanState != SCAN_OFF || gCssScanMode != CSS_SCAN_MODE_OFF || gUpdateRSSI) {
			gCurrentRSSI = BK4819_GetRSSI();
			UI_UpdateRSSI(gCurrentRSSI);
			gBatterySave = gEeprom.BATTERY_SAVE * 10;
			gRxIdleMode = true;

			BK4819_Sleep();
			BK4819_ClearGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE);
			// Authentic device checked removed
		} else {
			DUALWATCH_Alternate();
			gUpdateRSSI = true;
			gBatterySave = 10;
		}
		gBatterySaveCountdownExpired = false;
	}
}

void APP_CheckKeys(void)
{
	KEY_Code_t Key = KEYBOARD_Poll();

	if (gKeyReading0 != Key) {
		if (gKeyReading0 != KEY_INVALID && Key != KEY_INVALID) {
			APP_ProcessKey(gKeyReading1, false, gKeyBeingHeld);
		}
		gKeyReading0 = Key;
		gDebounceCounter = 0;
		return;
	}
	gDebounceCounter++;
	if (gDebounceCounter == 2) {
		if (Key == KEY_INVALID) {
			if (gKeyReading1 != KEY_INVALID) {
				APP_ProcessKey(gKeyReading1, false, gKeyBeingHeld);
				gKeyReading1 = KEY_INVALID;
			}
		} else {
			gKeyReading1 = Key;
			APP_ProcessKey(Key, true, false);
		}
		gKeyBeingHeld = false;
	} else if (gDebounceCounter == 128) {
		if (Key == KEY_STAR || Key == KEY_F || Key == KEY_SIDE2 || Key == KEY_SIDE1 || Key == KEY_UP || Key == KEY_DOWN) {
			gKeyBeingHeld = true;
			APP_ProcessKey(Key, true, true);
		}
	} else if (gDebounceCounter > 128) {
		if (Key == KEY_UP || Key == KEY_DOWN) {
			gKeyBeingHeld = true;
			if ((gDebounceCounter & 15) == 0) {
				APP_ProcessKey(Key, true, true);
			}
		}
		if (gDebounceCounter != 0xFFFF) {
			return;
		}
		gDebounceCounter = 128;
	}
}

void APP_TimeSlice10ms(void)
{

#if defined(ENABLE_UART)
	if (UART_IsCommandAvailable()) {
		__disable_irq();
		UART_HandleCommand();
		__enable_irq();
	}
#endif

	gFlashLightBlinkCounter++;
	if (gFlashLightState == FLASHLIGHT_BLINK && (gFlashLightBlinkCounter & 15U) == 0) {
		GPIO_FlipBit(&GPIOC->DATA, GPIOC_PIN_FLASHLIGHT);
	}

	if (gReducedService) {
		return;
	}

	if ((gCurrentFunction != FUNCTION_POWER_SAVE || !gRxIdleMode) && gScreenToDisplay != DISPLAY_SCANNER) {
		APP_CheckRadioInterrupts();
	}
	if (gCurrentFunction == FUNCTION_TRANSMIT) {
		if (gRTTECountdown > 0) {
			gRTTECountdown--;
			if (gRTTECountdown == 0) {
				FUNCTION_Select(FUNCTION_FOREGROUND);
				gUpdateDisplay = true;
			}
		}
	}

	// Skipping authentic device checks

	if (gCurrentFunction != FUNCTION_TRANSMIT) {
		if (gUpdateStatus) {
			UI_DisplayStatus();
			gUpdateStatus = false;
		}
		if (gUpdateDisplay) {
			GUI_DisplayScreen();
			gUpdateDisplay = false;
		}
	}

#if defined(ENABLE_FMRADIO)
	if (gFmRadioCountdown > 0) {
		return;
	}
	if (gFmRadioMode && gFM_RestoreCountdown > 0) {
		gFM_RestoreCountdown--;
		if (gFM_RestoreCountdown == 0) {
			FM_Start();
			GUI_SelectNextDisplay(DISPLAY_FM);
		}
	}
	if (gFM_ScanState != FM_SCAN_OFF && gScheduleFM && gCurrentFunction != FUNCTION_MONITOR && gCurrentFunction != FUNCTION_RECEIVE && gCurrentFunction != FUNCTION_TRANSMIT) {
		FM_Play();
		gScheduleFM = false;
	}
#endif

	if (gScanDelay > 0) {
		gScanDelay--;
		APP_CheckKeys();
		return;
	}
	if (gScannerEditState > 0 || gScreenToDisplay != DISPLAY_SCANNER) {
		APP_CheckKeys();
		return;
	}

	uint32_t Result;
	uint16_t CtcssFreq;
	BK4819_CssScanResult_t ScanResult;

	switch (gScanCssState) {
		case SCAN_CSS_STATE_OFF:
			if (!BK4819_GetFrequencyScanResult(&Result)) {
				break;
			}
			int32_t Delta = Result - gScanFrequency;
			gScanFrequency = Result;
			if (Delta < 0) {
				Delta = -Delta;
			}
			if (Delta < 100) {
				gScanHitCount++;
			} else {
				gScanHitCount = 0;
			}
			BK4819_DisableFrequencyScan();
			if (gScanHitCount < 2) {
				BK4819_EnableFrequencyScan();
			} else {
				BK4819_SetScanFrequency(gScanFrequency);
				gScanCssResultCode = 0xFF;
				gScanCssResultType = 0xFF;
				gScanHitCount = 0;
				gScanUseCssResult = false;
				gScanProgressIndicator = 0;
				gScanCssState = SCAN_CSS_STATE_SCANNING;
				GUI_SelectNextDisplay(DISPLAY_SCANNER);
			}
			gScanDelay = 21;
			break;

		case SCAN_CSS_STATE_SCANNING:
			ScanResult = BK4819_GetCxCSSScanResult(&Result, &CtcssFreq);
			if (ScanResult == BK4819_CSS_RESULT_NOT_FOUND) {
				break;
			}
			BK4819_Disable();
			if (ScanResult == BK4819_CSS_RESULT_CDCSS) {
				uint8_t Code = DCS_GetCdcssCode(Result);
				if (Code != 0xFF) {
					gScanCssResultCode = Code;
					gScanCssResultType = CODE_TYPE_DIGITAL;
					gScanCssState = SCAN_CSS_STATE_FOUND;
					gScanUseCssResult = true;
				}
			} else if (ScanResult == BK4819_CSS_RESULT_CTCSS) {
				uint8_t Code = DCS_GetCtcssCode(CtcssFreq);
				if (Code != 0xFF) {
					if (Code == gScanCssResultCode && gScanCssResultType == CODE_TYPE_CONTINUOUS_TONE) {
						gScanHitCount++;
						if (gScanHitCount >= 2) {
							gScanCssState = SCAN_CSS_STATE_FOUND;
							gScanUseCssResult = true;
						}
					} else {
						gScanHitCount = 0;
					}
					gScanCssResultType = CODE_TYPE_CONTINUOUS_TONE;
					gScanCssResultCode = Code;
				}
			}
			if (gScanCssState < SCAN_CSS_STATE_FOUND) {
				BK4819_SetScanFrequency(gScanFrequency);
				gScanDelay = 21;
				break;
			}
			GUI_SelectNextDisplay(DISPLAY_SCANNER);
			break;

		default:
			break;
	}
}

void APP_TimeSlice40ms(void) {
	if (gRxVfo->ModulationType == MOD_AM) {
		BK4819_NaiveAGC();
	}
}

void APP_TimeSlice500ms(void)
{
	// Skipped authentic device check

	if (gKeypadLocked > 0) {
		gKeypadLocked--;
		if (gKeypadLocked == 0) {
			gUpdateDisplay = true;
		}
	}

	// Skipped authentic device check

#if defined(ENABLE_FMRADIO)
	if (gFmRadioCountdown > 0) {
		gFmRadioCountdown--;
		return;
	}
#endif

	if (gReducedService) {
		BOARD_ADC_GetBatteryInfo(&gBatteryCurrentVoltage, &gBatteryCurrent);
		if (gBatteryCurrent > 500 || gBatteryCalibration[3] < gBatteryCurrentVoltage) {
			NVIC_SystemReset();
		}
		return;
	}

	gBatteryCheckCounter++;

	// Skipped authentic device check

	if (gCurrentFunction != FUNCTION_TRANSMIT) {
		if ((gBatteryCheckCounter & 1) == 0) {
			BOARD_ADC_GetBatteryInfo(&gBatteryVoltages[gBatteryVoltageIndex++], &gBatteryCurrent);
			if (gBatteryVoltageIndex > 3) {
				gBatteryVoltageIndex = 0;
			}
			BATTERY_GetReadings(true);
		}
		if (
#if defined(ENABLE_FMRADIO)
			(gFM_ScanState == FM_SCAN_OFF || gAskToSave) &&
#endif
			gScanState == SCAN_OFF && gCssScanMode == CSS_SCAN_MODE_OFF) {
			if (gBacklightCountdown > 0) {
				gBacklightCountdown--;
				if (gBacklightCountdown == 0) {
					GPIO_ClearBit(&GPIOB->DATA, GPIOB_PIN_BACKLIGHT);
				}
			}
			if (gScreenToDisplay != DISPLAY_SCANNER || (gScanCssState >= SCAN_CSS_STATE_FOUND)) {
				if (gEeprom.AUTO_KEYPAD_LOCK && gKeyLockCountdown > 0 && !gDTMF_InputMode) {
					gKeyLockCountdown--;
					if (gKeyLockCountdown == 0) {
						gEeprom.KEY_LOCK = true;
					}
					gUpdateStatus = true;
				}
				if (gVoltageMenuCountdown > 0) {
					gVoltageMenuCountdown--;
					if (gVoltageMenuCountdown == 0) {
						if (gScreenToDisplay == DISPLAY_SCANNER) {
							BK4819_DisableFrequencyScan();
							BK4819_Disable();
							RADIO_ConfigureChannel(0, VFO_CONFIGURE_RELOAD);
							RADIO_ConfigureChannel(1, VFO_CONFIGURE_RELOAD);
							RADIO_SetupRegisters(true);
						}
						gWasFKeyPressed = false;
						gUpdateStatus = true;
						gInputBoxIndex = 0;
						gDTMF_InputMode = false;
						gDTMF_InputIndex = 0;
						gAskToSave = false;
						gAskToDelete = false;
#if defined(ENABLE_FMRADIO)
						if (gFmRadioMode && gCurrentFunction != FUNCTION_RECEIVE && gCurrentFunction != FUNCTION_MONITOR && gCurrentFunction != FUNCTION_TRANSMIT) {
							GUI_SelectNextDisplay(DISPLAY_FM);
						} else {
							GUI_SelectNextDisplay(DISPLAY_MAIN);
						}
#else
						GUI_SelectNextDisplay(DISPLAY_MAIN);
#endif
					}
				}
			}
		}
		gCurrentRSSI = BK4819_GetRSSI();
		UI_UpdateRSSI(gCurrentRSSI);
	}

#if defined(ENABLE_FMRADIO)
	if (!gPttIsPressed && gFM_ResumeCountdown > 0) {
		gFM_ResumeCountdown--;
		if (gFM_ResumeCountdown == 0) {
			RADIO_SetVfoState(VFO_STATE_NORMAL);
			if (gCurrentFunction != FUNCTION_RECEIVE && gCurrentFunction != FUNCTION_TRANSMIT && gCurrentFunction != FUNCTION_MONITOR && gFmRadioMode) {
				FM_Start();
				GUI_SelectNextDisplay(DISPLAY_FM);
			}
		}
	}
#endif

	if (gLowBattery) {
		gLowBatteryBlink = ++gLowBatteryCountdown & 1;
		UI_DisplayBattery(gLowBatteryCountdown);
		if (gLowBatteryCountdown >= 30 && gCurrentFunction != FUNCTION_TRANSMIT) {
			gLowBatteryCountdown = 0;
			if (!gChargingWithTypeC && gBatteryDisplayLevel == 0) {
				gReducedService = true;
				FUNCTION_Select(FUNCTION_POWER_SAVE);
				ST7565_HardwareReset();
				GPIO_ClearBit(&GPIOB->DATA, GPIOB_PIN_BACKLIGHT);
			}
		}
	}

	if (gScreenToDisplay == DISPLAY_SCANNER && gScannerEditState == 0 && gScanCssState < SCAN_CSS_STATE_FOUND) {
		gScanProgressIndicator++;
		if (gScanProgressIndicator > 32) {
			if (gScanCssState == SCAN_CSS_STATE_SCANNING && !gScanSingleFrequency) {
				gScanCssState = SCAN_CSS_STATE_FOUND;
			} else {
				gScanCssState = SCAN_CSS_STATE_FAILED;
			}
		}
		gUpdateDisplay = true;
	}

	if (gDTMF_CallState != DTMF_CALL_STATE_NONE && gCurrentFunction != FUNCTION_TRANSMIT && gCurrentFunction != FUNCTION_RECEIVE) {
		if (gDTMF_AUTO_RESET_TIME) {
			gDTMF_AUTO_RESET_TIME--;
			if (gDTMF_AUTO_RESET_TIME == 0) {
				gDTMF_CallState = DTMF_CALL_STATE_NONE;
				gUpdateDisplay = true;
			}
		}
		if (gDTMF_DecodeRing && gDTMF_DecodeRingCountdown > 0) {
			gDTMF_DecodeRingCountdown--;
			if (gDTMF_DecodeRingCountdown == 0) {
				gDTMF_DecodeRing = false;
			}
		}
	}

	if (gDTMF_IsTx && gDTMF_TxStopCountdown > 0) {
		gDTMF_TxStopCountdown--;
		if (gDTMF_TxStopCountdown == 0) {
			gDTMF_IsTx = false;
			gUpdateDisplay = true;
		}
	}

	if (gDTMF_RecvTimeout) {
		gDTMF_RecvTimeout--;
		if (gDTMF_RecvTimeout == 0) {
			gDTMF_WriteIndex = 0;
			memset(gDTMF_Received, 0, sizeof(gDTMF_Received));
		}
	}
}

void CHANNEL_Next(bool bBackup, int8_t Direction)
{
	RADIO_SelectVfos();
	gNextMrChannel = gRxVfo->CHANNEL_SAVE;
	gCurrentScanList = 0;
	gScanState = Direction;
	if (IS_MR_CHANNEL(gNextMrChannel)) {
		if (bBackup) {
			gRestoreMrChannel = gNextMrChannel;
		}
		MR_NextChannel();
	} else {
		if (bBackup) {
			gRestoreFrequency = gRxVfo->ConfigRX.Frequency;
		}
		FREQ_NextChannel();
	}
	ScanPauseDelayIn10msec = 50;
	gScheduleScanListen = false;
	gRxReceptionMode = RX_MODE_NONE;
	gScanPauseMode = false;
	bScanKeepFrequency = false;
}

static void APP_ProcessKey(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
	if (gCurrentFunction == FUNCTION_POWER_SAVE) {
		FUNCTION_Select(FUNCTION_FOREGROUND);
	}
	gBatterySaveCountdown = 1000;
	if (gEeprom.AUTO_KEYPAD_LOCK) {
		gKeyLockCountdown = 30;
	}

	if (bKeyPressed) {
		if (Key != KEY_PTT) {
			gVoltageMenuCountdown = 0x10;
		}
		BACKLIGHT_TurnOn();
		if (gDTMF_DecodeRing) {
			gDTMF_DecodeRing = false;
			if (Key != KEY_PTT) {
				gPttWasReleased = true;
				return;
			}
		}
	}

	if (gEeprom.KEY_LOCK && gCurrentFunction != FUNCTION_TRANSMIT && Key != KEY_PTT) {
		if (Key == KEY_F) {
			if (!bKeyHeld) {
				if (!bKeyPressed) {
					return;
				}
				gKeypadLocked = 4;
				gUpdateDisplay = true;
				return;
			}
			if (!bKeyPressed) {
				return;
			}
		} else if (Key != KEY_SIDE1 && Key != KEY_SIDE2) {
			if (bKeyHeld || !bKeyPressed) {
				return;
			}
			gKeypadLocked = 4;
			gUpdateDisplay = true;
			return;
		}
	}

	if ((gScanState != SCAN_OFF && Key != KEY_PTT && Key != KEY_UP && Key != KEY_DOWN && Key != KEY_EXIT && Key != KEY_STAR) ||
	    (gCssScanMode != CSS_SCAN_MODE_OFF && Key != KEY_PTT && Key != KEY_UP && Key != KEY_DOWN && Key != KEY_EXIT && Key != KEY_STAR && Key != KEY_MENU)) {

		return;
	}

	if (gWasFKeyPressed && Key > KEY_9 && Key != KEY_F && Key != KEY_STAR) {
		gWasFKeyPressed = false;
		gUpdateStatus = true;
	}
	if (gF_LOCK) {
		if (Key == KEY_PTT || Key == KEY_SIDE2 || Key == KEY_SIDE1) {
			return;
		}
	}

	bool bIgnore = false;

	if (gPttWasPressed && Key == KEY_PTT) {
		bIgnore = bKeyHeld;
		if (!bKeyPressed) {
			bIgnore = true;
			gPttWasPressed = false;
		}
	}
	if (gPttWasReleased && Key != KEY_PTT) {
		bIgnore = bKeyHeld;
		if (!bKeyPressed) {
			bIgnore = true;
			gPttWasReleased = false;
		}
	}
	if (!bIgnore) {
		if (gCurrentFunction == FUNCTION_TRANSMIT) {
			if (Key == KEY_PTT) {
				GENERIC_Key_PTT(bKeyPressed);
			} else {
				uint8_t Code;

				if (Key == KEY_SIDE2) {
					Code = 0xFE;
				} else {
					Code = (uint8_t)DTMF_GetCharacter(Key);
					if (Code == 0xFF) {
						return;
					}
				}
				if (bKeyHeld || !bKeyPressed) {
					if (!bKeyPressed) {
						GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
						gEnableSpeaker = false;
						BK4819_ExitDTMF_TX(false);
					}
				} else {
					if (gEeprom.DTMF_SIDE_TONE) {
						GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
						gEnableSpeaker = true;
					}
					if (Code == 0xFE) {
						BK4819_TransmitTone(gEeprom.DTMF_SIDE_TONE, 1750);
					} else {
						BK4819_PlayDTMFEx(gEeprom.DTMF_SIDE_TONE, Code);
					}
				}
			}
		} else if (Key != KEY_SIDE1 && Key != KEY_SIDE2) {
			switch (gScreenToDisplay) {
			case DISPLAY_MAIN:
				MAIN_ProcessKeys(Key, bKeyPressed, bKeyHeld);
				break;
#if defined(ENABLE_FMRADIO)
			case DISPLAY_FM:
				FM_ProcessKeys(Key, bKeyPressed, bKeyHeld);
				break;
#endif
			case DISPLAY_MENU:
				MENU_ProcessKeys(Key, bKeyPressed, bKeyHeld);
				break;
			case DISPLAY_SCANNER:
				SCANNER_ProcessKeys(Key, bKeyPressed, bKeyHeld);
				break;
			default:
				break;
			}
		} else if (gScreenToDisplay != DISPLAY_SCANNER) {
			ACTION_Handle(Key, bKeyPressed, bKeyHeld);
		}
	}

	if (gFlagAcceptSetting) {
		MENU_AcceptSetting();
		gFlagRefreshSetting = true;
		gFlagAcceptSetting = false;
	}
	if (gFlagStopScan) {
		BK4819_DisableFrequencyScan();
		BK4819_Disable();
		gFlagStopScan = false;
	}

	if (gRequestSaveSettings) {
		if (!bKeyHeld && bKeyPressed) {
			SETTINGS_SaveSettings();
		}
		gRequestSaveSettings = false;
		gUpdateStatus = true;
	}
#if defined(ENABLE_FMRADIO)
	if (gRequestSaveFM) {
		if (!bKeyHeld && bKeyPressed) {
			SETTINGS_SaveFM();
		}
		gRequestSaveFM = false;
	}
#endif
	if (gRequestSaveVFO) {
		if (!bKeyHeld && bKeyPressed) {
			SETTINGS_SaveVfoIndices();
		}
		gRequestSaveVFO = false;
	}
	if (gRequestSaveChannel > 0) {
		if (!bKeyHeld && bKeyPressed) {
			SETTINGS_SaveChannel(
				gTxVfo->CHANNEL_SAVE,
				gEeprom.TX_VFO,
				gTxVfo,
				gRequestSaveChannel);
			if (gScreenToDisplay != DISPLAY_SCANNER) {
				gVfoConfigureMode = VFO_CONFIGURE;
			}
		} else {
			if (gRequestDisplayScreen == DISPLAY_INVALID) {
				gRequestDisplayScreen = DISPLAY_MAIN;
			}
		}
		gRequestSaveChannel = 0;
	}

	if (gVfoConfigureMode != VFO_CONFIGURE_NONE) {
		if (gFlagResetVfos) {
			RADIO_ConfigureChannel(0, gVfoConfigureMode);
			RADIO_ConfigureChannel(1, gVfoConfigureMode);
		} else {
			RADIO_ConfigureChannel(gEeprom.TX_VFO, gVfoConfigureMode);
		}
		if (gRequestDisplayScreen == DISPLAY_INVALID) {
			gRequestDisplayScreen = DISPLAY_MAIN;
		}
		gFlagReconfigureVfos = true;
		gVfoConfigureMode = VFO_CONFIGURE_NONE;
		gFlagResetVfos = false;
	}

	if (gFlagReconfigureVfos) {
		RADIO_SelectVfos();
		RADIO_SetupRegisters(true);

		gDTMF_AUTO_RESET_TIME = 0;
		gDTMF_CallState = DTMF_CALL_STATE_NONE;
		gDTMF_TxStopCountdown = 0;
		gDTMF_IsTx = false;
		gVFO_RSSI_Level[0] = 0;
		gVFO_RSSI_Level[1] = 0;
		gFlagReconfigureVfos = false;
	}

	if (gFlagRefreshSetting) {
		MENU_ShowCurrentSetting();
		gFlagRefreshSetting = false;
	}
	if (gFlagStartScan) {
		SCANNER_Start();
		gRequestDisplayScreen = DISPLAY_SCANNER;
		gFlagStartScan = false;
	}
	if (gFlagPrepareTX) {
		RADIO_PrepareTX();
		gFlagPrepareTX = false;
	}
	GUI_SelectNextDisplay(gRequestDisplayScreen);
	gRequestDisplayScreen = DISPLAY_INVALID;
}

