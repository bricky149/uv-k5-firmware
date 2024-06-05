#include "functions.h"
#include "misc.h"
#include "scheduler.h"
#include "ui/status.h"
#include "ui/ui.h"

void TASK_UpdateScreen(void) {
	if (!SCHEDULER_CheckTask(TASK_UPDATE_SCREEN)) {
		return;
	}
	SCHEDULER_ClearTask(TASK_UPDATE_SCREEN);

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

	if (gRequestDisplayScreen != DISPLAY_INVALID) {
		GUI_SelectNextDisplay(gRequestDisplayScreen);
		gRequestDisplayScreen = DISPLAY_INVALID;
	}
}
