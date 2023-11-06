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
#include "bitmaps.h"
#include "dcs.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "frequencies.h"
#include "helper/battery.h"
#include "misc.h"
#include "settings.h"
#include "ui/helper.h"
#include "ui/inputbox.h"
#include "ui/menu.h"
#include "ui/ui.h"

static const MENU_Item_t MenuList[56] = {
	{ "SQL", MENU_SQL },
	{ "STEP", MENU_STEP },
	{ "TXP", MENU_TXP },
	{ "R_DCS", MENU_R_DCS },
	{ "R_CTCS", MENU_R_CTCS },
	{ "T_DCS", MENU_T_DCS },
	{ "T_CTCS", MENU_T_CTCS },
	{ "SFT-D", MENU_SFT_D },
	{ "OFFSET", MENU_OFFSET },
	{ "W/N", MENU_W_N },
	{ "SCR", MENU_SCR },
	{ "BCL", MENU_BCL },
	{ "MEM-CH", MENU_MEM_CH },
	{ "SAVE", MENU_SAVE },
	{ "VOX", MENU_VOX },
	{ "ABR", MENU_ABR },
	{ "TDR", MENU_TDR },
	{ "WX", MENU_WX },
	{ "BEEP", MENU_BEEP },
	{ "TOT", MENU_TOT },
	{ "VOICE", MENU_VOICE },
	{ "SC-REV", MENU_SC_REV },
	{ "MDF", MENU_MDF },
	{ "AUTOLK", MENU_AUTOLK },
	{ "S-ADD1", MENU_S_ADD1 },
	{ "S-ADD2", MENU_S_ADD2 },
	{ "STE", MENU_STE },
	{ "RP-STE", MENU_RP_STE },
	{ "MIC", MENU_MIC },
	{ "1-CALL", MENU_1_CALL },
	{ "S-LIST", MENU_S_LIST },
	{ "SLIST1", MENU_SLIST1 },
	{ "SLIST2", MENU_SLIST2 },
	{ "ANI-ID", MENU_ANI_ID },
	{ "UPCODE", MENU_UPCODE },
	{ "DWCODE", MENU_DWCODE },
	{ "D-ST", MENU_D_ST },
	{ "D-RSP", MENU_D_RSP },
	{ "D-HOLD", MENU_D_HOLD },
	{ "D-PRE", MENU_D_PRE },
	{ "PTT-ID", MENU_PTT_ID },
	{ "D-DCD", MENU_D_DCD },
	{ "D-LIST", MENU_D_LIST },
	{ "PONMSG", MENU_PONMSG },
	{ "ROGER", MENU_ROGER },
	{ "COMPND", MENU_COMPND },
	{ "MOD", MENU_MOD },
	{ "DEL-CH", MENU_DEL_CH },
	{ "RESET", MENU_RESET },
	{ "350TX", MENU_350TX },
	{ "F-LOCK", MENU_F_LOCK },
	{ "200TX", MENU_200TX },
	{ "500TX", MENU_500TX },
	{ "350EN", MENU_350EN },
	{ "SCREN", MENU_SCREN },
	{ "BATCAL", MENU_BATCAL },
};

static const uint16_t gSubMenu_Step[7] = {
	125,
	250,
	500,
	625,
	1250,
	2500,
	833,
};

static const char gSubMenu_TXP[3][5] = {
	"LOW",
	"MID",
	"HIGH",
};

static const char gSubMenu_SFT_D[3][4] = {
	"OFF",
	"+",
	"-",
};

static const char gSubMenu_W_N[3][9] = {
	"WIDE",
	"NARROW",
	"NARROWER",
};

static const char gSubMenu_OFF_ON[2][4] = {
	"OFF",
	"ON",
};

static const char gSubMenu_SAVE[5][4] = {
	"OFF",
	"1:1",
	"1:2",
	"1:3",
	"1:4",
};

static const char gSubMenu_CHAN[3][7] = {
	"OFF",
	"CHAN_A",
	"CHAN_B",
};

static const char gSubMenu_SC_REV[3][3] = {
	"TO",
	"CO",
	"SE",
};

static const char gSubMenu_MDF[3][5] = {
	"FREQ",
	"CHAN",
	"NAME",
};

static const char gSubMenu_D_RSP[4][6] = {
	"NULL",
	"RING",
	"REPLY",
	"BOTH",
};

static const char gSubMenu_PTT_ID[4][5] = {
	"OFF",
	"BOT",
	"EOT",
	"BOTH",
};

static const char gSubMenu_ROGER[3][6] = {
	"OFF",
	"ROGER",
	"MDC",
};

static const char gSubMenu_COMPND[4][6] = {
	"OFF",
	"TX",
	"RX",
	"TX/RX",
};

static const char gSubMenu_MOD[3][4] = {
	"FM",
	"AM",
	"SSB",
};

static const char gSubMenu_RESET[2][4] = {
	"VFO",
	"ALL",
};

static const char gSubMenu_F_LOCK[6][4] = {
	"OFF",
	"FCC",
	"CE",
	"GB",
	"430",
	"438",
};

bool gIsInSubMenu;

uint8_t gMenuCursor;
int8_t gMenuScrollDirection;
uint32_t gSubMenuSelection;

void UI_DisplayMenu(void)
{
	uint8_t i;
	char String[16];
	memset(gFrameBuffer, 0, sizeof(gFrameBuffer));

	for (i = 0; i < 3; i++) {
		// No previous item if we are at the start
		if (gMenuCursor > 0 || i > 0) {
			// No next item if we are at the end
			if (gMenuCursor < (gMenuListCount - 1) || (i < 2)) {
				strcpy(String, MenuList[gMenuCursor + i - 1].Name);
				UI_PrintString(String, 0, 127, i * 2, 8, false);
				memset(String, 0, sizeof(String));
			}
		}
	}
	// Invert current menu item pixels
	for (i = 0; i < 48; i++) {
		gFrameBuffer[2][i] ^= 0xFF;
		gFrameBuffer[3][i] ^= 0xFF;
	}
	// Draw vertical line between item name and value
	for (i = 0; i < 7; i++) {
		gFrameBuffer[i][48] = 0xFF;
		gFrameBuffer[i][49] = 0xFF;
	}
	if (gIsInSubMenu) {
		memcpy(gFrameBuffer[0] + 50, BITMAP_CurrentIndicator, sizeof(BITMAP_CurrentIndicator));
	}

	NUMBER_ToDigits(gMenuCursor + 1, String);
	UI_DisplaySmallDigits(2, String + 6, 33, 6);

	uint32_t kHz;
	char Contact[16];

	switch (gMenuCursor) {
	case MENU_SQL:
	case MENU_MIC:
		sprintf(String, "%u", gSubMenuSelection);
		break;

	case MENU_STEP:
		kHz = gSubMenu_Step[gSubMenuSelection] / 100;
		uint8_t Hz = gSubMenu_Step[gSubMenuSelection] % 100;
		sprintf(String, "%u.%02ukHz", kHz, Hz);
		break;

	case MENU_TXP:
		strcpy(String, gSubMenu_TXP[gSubMenuSelection]);
		break;

	case MENU_R_DCS:
	case MENU_T_DCS:
		if (gSubMenuSelection == 0) {
			strcpy(String, "OFF");
		} else if (gSubMenuSelection < 105) {
			sprintf(String, "D%03oN", DCS_Options[gSubMenuSelection - 1]);
		} else {
			sprintf(String, "D%03oI", DCS_Options[gSubMenuSelection - 105]);
		}
		break;

	case MENU_R_CTCS:
	case MENU_T_CTCS:
		if (gSubMenuSelection == 0) {
			strcpy(String, "OFF");
		} else {
			uint8_t Hz = CTCSS_Options[gSubMenuSelection - 1] / 10;
			uint8_t mHz = CTCSS_Options[gSubMenuSelection - 1] % 10;
			sprintf(String, "%u.%01uHz", Hz, mHz);
		}
		break;

	case MENU_SFT_D:
		strcpy(String, gSubMenu_SFT_D[gSubMenuSelection]);
		break;

	case MENU_OFFSET:
		uint8_t i = 0;
		while (i < 3) {
			String[i] = (gInputBox[i] == 10) ? '-' : gInputBox[i] + '0';
			i++;
		}
		String[i] = '.';
		i++;
		while (i < 8) {
			String[i] = (gInputBox[i] == 10) ? '-' : gInputBox[i] + '0';
			i++;
		}
		uint16_t MHz = gSubMenuSelection / 100000;
		kHz = gSubMenuSelection % 100000;
		sprintf(String, "%u.%05u", MHz, kHz);
		break;

	case MENU_W_N:
		strcpy(String, gSubMenu_W_N[gSubMenuSelection]);
		break;

	case MENU_ABR:
		if (gSubMenuSelection == 0) {
			strcpy(String, "OFF");
		} else {
			sprintf(String, "%u", gSubMenuSelection * 2);
		}
		break;

	case MENU_MOD:
		strcpy(String, gSubMenu_MOD[gSubMenuSelection]);
		break;

	case MENU_BCL:
	case MENU_AUTOLK:
	case MENU_S_ADD1:
	case MENU_S_ADD2:
	case MENU_STE:
	case MENU_D_ST:
	case MENU_D_DCD:
	case MENU_350TX:
	case MENU_200TX:
	case MENU_500TX:
		strcpy(String, gSubMenu_OFF_ON[gSubMenuSelection]);
		break;

	case MENU_MEM_CH:
	case MENU_1_CALL:
	case MENU_DEL_CH:
		UI_GenerateChannelStringEx(
			String,
			RADIO_CheckValidChannel((uint16_t)gSubMenuSelection, false, 0),
			(uint8_t)gSubMenuSelection
			);
		break;

	case MENU_SAVE:
		strcpy(String, gSubMenu_SAVE[gSubMenuSelection]);
		break;

	case MENU_TDR:
	case MENU_WX:
		strcpy(String, gSubMenu_CHAN[gSubMenuSelection]);
		break;

	case MENU_TOT:
		if (gSubMenuSelection == 0) {
			strcpy(String, "OFF");
		} else {
			sprintf(String, "%umin", gSubMenuSelection);
		}
		break;

	case MENU_SC_REV:
		strcpy(String, gSubMenu_SC_REV[gSubMenuSelection]);
		break;

	case MENU_MDF:
		strcpy(String, gSubMenu_MDF[gSubMenuSelection]);
		break;

	case MENU_RP_STE:
		if (gSubMenuSelection == 0) {
			strcpy(String, "OFF");
		} else {
			sprintf(String, "%u*100ms", gSubMenuSelection);
		}
		break;

	case MENU_S_LIST:
		sprintf(String, "LIST%u", gSubMenuSelection);
		break;

	case MENU_ANI_ID:
		strcpy(String, gEeprom.ANI_DTMF_ID);
		break;

	case MENU_UPCODE:
		strcpy(String, gEeprom.DTMF_UP_CODE);
		break;

	case MENU_DWCODE:
		strcpy(String, gEeprom.DTMF_DOWN_CODE);
		break;

	case MENU_D_RSP:
		strcpy(String, gSubMenu_D_RSP[gSubMenuSelection]);
		break;

	case MENU_D_HOLD:
		sprintf(String, "%us", gSubMenuSelection);
		break;

	case MENU_D_PRE:
		sprintf(String, "%u*10ms", gSubMenuSelection);
		break;

	case MENU_PTT_ID:
		strcpy(String, gSubMenu_PTT_ID[gSubMenuSelection]);
		break;

	case MENU_D_LIST:
		gIsDtmfContactValid = DTMF_GetContact((uint8_t)gSubMenuSelection - 1, Contact);
		if (!gIsDtmfContactValid) {
			// Ghidra being weird again...
			memcpy(String, "NULL\0\0\0", 8);
		} else {
			memcpy(String, Contact, 8);
		}
		break;

	case MENU_ROGER:
		strcpy(String, gSubMenu_ROGER[gSubMenuSelection]);
		break;

	case MENU_COMPND:
		strcpy(String, gSubMenu_COMPND[gSubMenuSelection]);
		break;

	case MENU_RESET:
		strcpy(String, gSubMenu_RESET[gSubMenuSelection]);
		break;

	case MENU_F_LOCK:
		strcpy(String, gSubMenu_F_LOCK[gSubMenuSelection]);
		break;

	case MENU_BATCAL:
		uint32_t vol = gBatteryVoltageAverage * gBatteryCalibration[3] / gSubMenuSelection;
		sprintf(String, "%u.%02uV-%#4u", vol / 100, vol % 100, gSubMenuSelection);
		break;
	}
	UI_PrintString(String, 50, 127, 2, 8, true);

	switch (gMenuCursor) {
	case MENU_OFFSET:
		UI_PrintString("MHz", 50, 127, 4, 8, true);
		break;

	case MENU_RESET:
	case MENU_MEM_CH:
	case MENU_DEL_CH:
		if (gAskForConfirmation > 0) {
			if (gAskForConfirmation == 1) {
				strcpy(String, "SURE?");
			} else {
				strcpy(String, "WAIT!");
			}
			UI_PrintString(String, 50, 127, 4, 8, true);
		}
		break;

	case MENU_R_CTCS:
	case MENU_R_DCS:
		if (gCssScanMode != CSS_SCAN_MODE_OFF) {
			UI_PrintString("SCAN", 50, 127, 4, 8, true);
		}
		break;

	case MENU_UPCODE:
		if (strlen(gEeprom.DTMF_UP_CODE) > 8) {
			UI_PrintString(gEeprom.DTMF_UP_CODE + 8, 50, 127, 4, 8, true);
		}
		break;

	case MENU_DWCODE:
		if (strlen(gEeprom.DTMF_DOWN_CODE) > 8) {
			UI_PrintString(gEeprom.DTMF_DOWN_CODE + 8, 50, 127, 4, 8, true);
		}
		break;

	case MENU_D_LIST:
		if (gIsDtmfContactValid) {
			Contact[11] = 0;
			memcpy(&gDTMF_ID, Contact + 8, 4);
			sprintf(String, "ID:%s", Contact + 8);
			UI_PrintString(String, 50, 127, 4, 8, true);
		}
		break;
	}

	switch (gMenuCursor) {
	case MENU_R_CTCS:
	case MENU_T_CTCS:
	case MENU_R_DCS:
	case MENU_T_DCS:
	case MENU_D_LIST:
		NUMBER_ToDigits((uint8_t)gSubMenuSelection, String);
		uint8_t Offset = (gMenuCursor == MENU_D_LIST) ? 2 : 3;
		UI_DisplaySmallDigits(Offset, String + (8 - Offset), 105, 0);
		break;

	case MENU_SLIST1:
	case MENU_SLIST2:
		if (gSubMenuSelection == 0xFF) {
			sprintf(String, "NULL");
		} else {
			UI_GenerateChannelStringEx(String, true, (uint8_t)gSubMenuSelection);
		}
		i = gMenuCursor - MENU_SLIST1;
		if (gSubMenuSelection == 0xFF || !gEeprom.SCAN_LIST_ENABLED[i]) {
			UI_PrintString(String, 50, 127, 2, 8, true);
		} else {
			UI_PrintString(String, 50, 127, 0, 8, true);
			if (IS_MR_CHANNEL(gEeprom.SCANLIST_PRIORITY_CH1[i])) {
				sprintf(String, "PRI1:%u", gEeprom.SCANLIST_PRIORITY_CH1[i] + 1);
				UI_PrintString(String, 50, 127, 2, 8, true);
			}
			if (IS_MR_CHANNEL(gEeprom.SCANLIST_PRIORITY_CH2[i])) {
				sprintf(String, "PRI2:%u", gEeprom.SCANLIST_PRIORITY_CH2[i] + 1);
				UI_PrintString(String, 50, 127, 4, 8, true);
			}
		}
		break;
	}
	ST7565_BlitFullScreen();
}

