#include "app/dtmf.h"
#include "app/scanner.h"
#include "driver/bk4819.h"
#include "misc.h"
#include "scheduler.h"
#include "ui/ui.h"

void TASK_Scanner(void) {
	if (!SCHEDULER_CheckTask(TASK_SCANNER)) {
		return;
	}
	SCHEDULER_ClearTask(TASK_SCANNER);

	if (gScannerEditState > 0 || gScreenToDisplay != DISPLAY_SCANNER) {
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
			if (gScanHitCount < 3) {
				BK4819_EnableFrequencyScan();
			} else {
				BK4819_SetScanFrequency(gScanFrequency);
				gScanCssResultCode = 0xFF;
				gScanCssResultType = 0xFF;
				gScanHitCount = 0;
				gScanUseCssResult = false;
				gScanProgressIndicator = 0;
				gScanCssState = SCAN_CSS_STATE_SCANNING;
				gRequestDisplayScreen = DISPLAY_SCANNER;
			}
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
						if (gScanHitCount >= 3) {
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
				break;
			}
			gRequestDisplayScreen = DISPLAY_SCANNER;
			break;

		default:
			break;
	}
}

