#include "app/fm.h"
#include "functions.h"
#include "misc.h"
#include "scheduler.h"
#include "ui/ui.h"

void TASK_FM_Radio(void) {
	if (!SCHEDULER_CheckTask(TASK_FM_RADIO)) {
		return;
	}
	SCHEDULER_ClearTask(TASK_FM_RADIO);

	if (gFmRadioCountdown > 0) {
		gFmRadioCountdown--;
		return;
	}

	if (gFmRadioMode && gFM_RestoreCountdown > 0) {
		gFM_RestoreCountdown--;
		if (gFM_RestoreCountdown == 0) {
			FM_Start();
			gRequestDisplayScreen = DISPLAY_FM;
		}
	}
	if (gFM_ScanState != FM_SCAN_OFF && gCurrentFunction != FUNCTION_MONITOR && gCurrentFunction != FUNCTION_RECEIVE && gCurrentFunction != FUNCTION_TRANSMIT) {
		FM_Play();
	}
}

