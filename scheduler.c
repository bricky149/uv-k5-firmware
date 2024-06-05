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

#if defined(ENABLE_FMRADIO)
#include "app/fm.h"
#endif
#include "app/scanner.h"
#include "functions.h"
#include "helper/battery.h"
#include "misc.h"
#include "scheduler.h"
#include "settings.h"
#include "ui/ui.h"

#define DECREMENT_AND_TRIGGER(cnt, flag) \
	if (cnt > 0 && --cnt == 0) { \
		flag = true; \
	}
#define INCREMENT_AND_TRIGGER(cnt, flag) \
	if (cnt == 0 && ++cnt > 0) { \
		flag = false; \
	}
#define TRIGGER_CXCSS(cnt, flag0, flag1) \
	if (cnt > 0 && --cnt == 0) { \
		if (flag0) { \
			flag1 = false; \
			flag0 = false; \
		} \
	}

static uint16_t SCHEDULER_Tasks;

static void SetTask(uint16_t Task)
{
	SCHEDULER_Tasks |= Task;
}

bool SCHEDULER_CheckTask(uint16_t Task)
{
	return SCHEDULER_Tasks & Task;
}

void SCHEDULER_ClearTask(uint16_t Task)
{
	SCHEDULER_Tasks &= ~Task;
}

static volatile uint32_t gGlobalSysTickCounter;

void SystickHandler(void);

void SystickHandler(void)
{
	gGlobalSysTickCounter++;

	SetTask(TASK_CHECK_KEYS);
	SetTask(TASK_CHECK_RADIO_INTERRUPTS);
	if (gCurrentFunction != FUNCTION_TRANSMIT || gRequestDisplayScreen != DISPLAY_INVALID) {
		SetTask(TASK_UPDATE_SCREEN);
	}

	if ((gGlobalSysTickCounter & 3) == 0) {
		gNextTimeslice40ms = true;
	}
	if ((gGlobalSysTickCounter % 21) == 0) {
		SetTask(TASK_SCANNER);
	}
	if ((gGlobalSysTickCounter % 50) == 0) {
		gNextTimeslice500ms = true;
		DECREMENT_AND_TRIGGER(gTxTimerCountdown, gTxTimeoutReached);
		SetTask(TASK_FM_RADIO);
	}

	TRIGGER_CXCSS(gFoundCDCSSCountdown, gFoundCDCSS, gFoundCTCSS);
	TRIGGER_CXCSS(gFoundCTCSSCountdown, gFoundCTCSS, gFoundCDCSS);

	switch (gCurrentFunction) {
	case FUNCTION_FOREGROUND:
		DECREMENT_AND_TRIGGER(gBatterySaveCountdown, gSchedulePowerSave);
		break;
	case FUNCTION_POWER_SAVE:
		DECREMENT_AND_TRIGGER(gBatterySave, gBatterySaveCountdownExpired);
		break;
	default:
		break;
	}
	if (gScanState == SCAN_OFF && gCssScanMode == CSS_SCAN_MODE_OFF && gEeprom.DUAL_WATCH != DUAL_WATCH_OFF) {
		switch (gCurrentFunction) {
		case FUNCTION_FOREGROUND:
		case FUNCTION_INCOMING:
		case FUNCTION_POWER_SAVE:
			DECREMENT_AND_TRIGGER(gDualWatchCountdown, gScheduleDualWatch);
			break;
		default:
			break;
		}
	}
	if (gScanState != SCAN_OFF || gCssScanMode == CSS_SCAN_MODE_SCANNING) {
		switch (gCurrentFunction) {
		case FUNCTION_FOREGROUND:
		case FUNCTION_INCOMING:
		case FUNCTION_RECEIVE:
		case FUNCTION_POWER_SAVE:
			DECREMENT_AND_TRIGGER(ScanPauseDelayIn10msec, gScheduleScanListen);
			break;
		default:
			break;
		}
	}

	DECREMENT_AND_TRIGGER(gTailNoteEliminationCountdown, gFlagTteComplete);
}

