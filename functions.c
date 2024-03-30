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
#include "app/dtmf.h"
#if defined(ENABLE_FMRADIO)
#include "app/fm.h"
#endif
#include "bsp/dp32g030/gpio.h"
#include "dcs.h"
#if defined(ENABLE_FMRADIO)
#include "driver/bk1080.h"
#endif
#include "driver/bk4819.h"
#include "driver/gpio.h"
#include "driver/system.h"
#include "functions.h"
#include "helper/battery.h"
#if defined(ENABLE_MDC1200)
#include "mdc1200.h"
#endif
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/status.h"
#include "ui/ui.h"

FUNCTION_Type_t gCurrentFunction;

void FUNCTION_Init(void)
{
	gCurrentCodeType = gSelectedCodeType;
	if (gCssScanMode == CSS_SCAN_MODE_OFF) {
		if (gRxVfo->MODULATION_MODE != MOD_FM) {
			gCurrentCodeType = CODE_TYPE_OFF;
		} else {
			gCurrentCodeType = gRxVfo->pRX->CodeType;
		}
	}
	gDTMF_RequestPending = false;
	gDTMF_WriteIndex = 0;
	memset(gDTMF_Received, 0, sizeof(gDTMF_Received));
	g_CxCSS_TAIL_Found = false;
	g_CDCSS_Lost = false;
	g_CTCSS_Lost = false;
	g_SquelchLost = false;
	gTailNoteEliminationCountdown = 0;
	gFlagTteComplete = false;
	gFoundCTCSS = false;
	gFoundCDCSS = false;
	gFoundCTCSSCountdown = 0;
	gFoundCDCSSCountdown = 0;
	gEndOfRxDetectedMaybe = false;
}

void FUNCTION_Select(FUNCTION_Type_t Function)
{
	FUNCTION_Type_t PreviousFunction;
	bool bWasPowerSave;

	PreviousFunction = gCurrentFunction;
	bWasPowerSave = (PreviousFunction == FUNCTION_POWER_SAVE);
	gCurrentFunction = Function;

	if (bWasPowerSave) {
		if (Function != FUNCTION_POWER_SAVE) {
			BK4819_EnableRX();
			gRxIdleMode = false;
			UI_DisplayStatus();
		}
	}

	switch (Function) {
	case FUNCTION_FOREGROUND:
		if (gDTMF_ReplyState != DTMF_REPLY_NONE) {
			RADIO_PrepareCssTX();
		}
		if (PreviousFunction == FUNCTION_TRANSMIT) {
			gVFO_RSSI_Level[0] = 0;
			gVFO_RSSI_Level[1] = 0;
		} else if (PreviousFunction != FUNCTION_RECEIVE) {
			break;
		}
#if defined(ENABLE_FMRADIO)
		if (gFmRadioMode) {
			gFM_RestoreCountdown = 500;
		}
#endif
		if (gDTMF_CallState == DTMF_CALL_STATE_CALL_OUT || gDTMF_CallState == DTMF_CALL_STATE_RECEIVED) {
			gDTMF_AUTO_RESET_TIME = 1 + (gEeprom.DTMF_AUTO_RESET_TIME * 2);
		}
		return;

	case FUNCTION_MONITOR:
	case FUNCTION_INCOMING:
	case FUNCTION_RECEIVE:
		break;

	case FUNCTION_POWER_SAVE:
		gBatterySave = 40;
		gRxIdleMode = true;

		BK4819_Sleep();
		BK4819_ClearGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE);
		gBatterySaveCountdownExpired = false;
		gUpdateStatus = true;
		GUI_SelectNextDisplay(DISPLAY_MAIN);
		return;

	case FUNCTION_TRANSMIT:
#if defined(ENABLE_FMRADIO)
		if (gFmRadioMode) {
			BK1080_Sleep();
		}
#endif
		GUI_DisplayScreen();
		RADIO_SetTxParameters();
		BK4819_SetGpioOut(BK4819_GPIO5_PIN1_RED);
		DTMF_Reply();
#if defined(ENABLE_MDC1200)
		BK4819_DisableMDC1200Rx();	
		if (gCurrentVfo->MDC1200_MODE == MDC1200_MODE_BOT ||
			gCurrentVfo->MDC1200_MODE == MDC1200_MODE_BOTH)
		{
			SYSTEM_DelayMs(30);
			BK4819_SendMDC1200(MDC1200_OP_CODE_PTT_ID, 0x80, gEeprom.MDC1200_ID, true, gCurrentVfo->CHANNEL_BANDWIDTH);
		}
#endif
		break;
	}

	gBatterySaveCountdown = 1000;
	gSchedulePowerSave = false;
#if defined(ENABLE_FMRADIO)
	gFM_RestoreCountdown = 0;
#endif
}

