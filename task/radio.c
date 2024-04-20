#include "app/dtmf.h"
#include "driver/bk4819.h"
#include "functions.h"
#if defined(ENABLE_MDC1200)
#include "mdc1200.h"
#endif
#include "misc.h"
#include "ui/ui.h"

void TASK_CheckRadioInterrupts(void) {
    if ((gCurrentFunction != FUNCTION_POWER_SAVE || !gRxIdleMode) && gScreenToDisplay != DISPLAY_SCANNER) {
		//APP_CheckRadioInterrupts();
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
	#if defined(ENABLE_MDC1200)
			MDC1200_process_rx(Mask);
	#endif
		}
	}
}