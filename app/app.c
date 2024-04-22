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
#if defined(ENABLE_MDC1200)
#include "mdc1200.h"
#endif
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/battery.h"
#include "ui/inputbox.h"
#include "ui/menu.h"
#include "ui/rssi.h"
#include "ui/status.h"
#include "ui/ui.h"

static uint16_t CurrentRSSI;
static bool bUpdateRSSI;

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
		if (gDTMF_RequestPending) {
			DTMF_HandleRequest();
		}
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

	BK4819_SetModulation(gRxVfo->MODULATION_MODE);
	if (gRxVfo->MODULATION_MODE != MOD_FM) {
		BK4819_WriteRegister(BK4819_REG_48, 0xB3A8);
		BK4819_SetCompander(0);
	} else {
		BK4819_WriteRegister(BK4819_REG_48, 0xB000
				| (gCalibration.VOLUME_GAIN << 4)
				| (gCalibration.DAC_GAIN << 0)
				);
		BK4819_SetCompander(gRxVfo->CompanderMode);
	}

	FUNCTION_Select(Function);
	if (Function == FUNCTION_MONITOR
#if defined(ENABLE_FMRADIO)
		|| gFmRadioMode
#endif
		) {
		gRequestDisplayScreen = DISPLAY_MAIN;
		return;
	}
	gUpdateDisplay = true;
}

void APP_SetFrequencyByStep(VFO_Info_t *pInfo, int8_t Step)
{
	uint32_t Frequency = pInfo->ConfigRX.Frequency + (Step * pInfo->StepFrequency);
	// DualTachyon
	if (pInfo->StepFrequency == 833) {
		const uint32_t Lower = LowerLimitFrequencyBandTable[pInfo->Band];
		const uint32_t Delta = Frequency - Lower;
		uint32_t Base = (Delta / 2500) * 2500;
		const uint32_t Index = ((Delta - Base) % 2500) / 833;
		if (Index == 2) {
			Base++;
		}
		Frequency = Lower + Base + (Index * 833);
	}
	if (Frequency > UpperLimitFrequencyBandTable[pInfo->Band]) {
		pInfo->ConfigRX.Frequency = LowerLimitFrequencyBandTable[pInfo->Band];
	} else if (Frequency < LowerLimitFrequencyBandTable[pInfo->Band]) {
		pInfo->ConfigRX.Frequency = FREQUENCY_FloorToStep(UpperLimitFrequencyBandTable[pInfo->Band], pInfo->StepFrequency, LowerLimitFrequencyBandTable[pInfo->Band]);
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
	gRxVfo = &gVFO.Info[gEeprom.RX_VFO];

	RADIO_SetupRegisters(false);
	gDualWatchCountdown = 10;
}

void APP_EndTransmission(void)
{
	RADIO_SendEndOfTransmission();
	RADIO_EnableCxCSS();
	RADIO_SetupRegisters(false);
}

void APP_Update(void)
{
#if defined(ENABLE_UART)
	if (UART_IsCommandAvailable()) {
		__disable_irq();
		UART_HandleCommand();
		__enable_irq();
	}
#endif

    gFlashLightBlinkCounter++;
	// Consider re-adding SOS
	if (gFlashLightState == FLASHLIGHT_BLINK && (gFlashLightBlinkCounter & 15U) == 0) {
		GPIO_FlipBit(&GPIOC->DATA, GPIOC_PIN_FLASHLIGHT);
	}

	if (gCurrentFunction == FUNCTION_TRANSMIT) {
		if (gTxTimeoutReached) {
			gTxTimeoutReached = false;
			gFlagEndTransmission = true;
			APP_EndTransmission();
			RADIO_SetVfoState(VFO_STATE_TIMEOUT);
			gUpdateDisplay = true;
		}
		if (gRTTECountdown == 0) {
			FUNCTION_Select(FUNCTION_FOREGROUND);
			gUpdateDisplay = true;
		}
	} else {
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
						gRequestDisplayScreen = DISPLAY_MAIN;
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
		if (gScanState != SCAN_OFF || gCssScanMode != CSS_SCAN_MODE_OFF
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
				bUpdateRSSI = false;
			}
			FUNCTION_Init();
			gBatterySave = 10;
			gRxIdleMode = false;
		} else if (gEeprom.DUAL_WATCH == DUAL_WATCH_OFF || gScanState != SCAN_OFF || gCssScanMode != CSS_SCAN_MODE_OFF || bUpdateRSSI) {
			CurrentRSSI = BK4819_GetRSSI();
			UI_UpdateRSSI(CurrentRSSI);
			gBatterySave = 40;
			gRxIdleMode = true;

			BK4819_Sleep();
			BK4819_ClearGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE);
			// Authentic device checks removed
		} else {
			DUALWATCH_Alternate();
			bUpdateRSSI = true;
			gBatterySave = 10;
		}
		gBatterySaveCountdownExpired = false;
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
		if (gScanState == SCAN_OFF &&
#if defined(ENABLE_FMRADIO)
			(gFM_ScanState == FM_SCAN_OFF || gAskToSave) &&
#endif
			gCssScanMode == CSS_SCAN_MODE_OFF) {
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
							gRequestDisplayScreen = DISPLAY_MAIN;
						}
#else
						gRequestDisplayScreen = DISPLAY_MAIN;
#endif
					}
				}
			}
		}
		CurrentRSSI = BK4819_GetRSSI();
		UI_UpdateRSSI(CurrentRSSI);
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
		UI_DisplayBattery(gLowBatteryBlink);
		if (gLowBatteryCountdown >= 30 && gCurrentFunction != FUNCTION_TRANSMIT) {
			gLowBatteryCountdown = 0;
			if (!gChargingWithTypeC) {
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
		if (gDTMF_AUTO_RESET_TIME > 0) {
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

	if (gDTMF_RecvTimeout > 0) {
		gDTMF_RecvTimeout--;
		if (gDTMF_RecvTimeout == 0) {
			gDTMF_WriteIndex = 0;
			memset(gDTMF_Received, 0, sizeof(gDTMF_Received));
		}
	}

#if defined(ENABLE_MDC1200)
	if (mdc1200_rx_ready_tick_500ms > 0) {
		mdc1200_rx_ready_tick_500ms--;
		if (mdc1200_rx_ready_tick_500ms == 0) {
			gUpdateDisplay = true;
		}
	}
#endif
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
