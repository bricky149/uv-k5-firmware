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
#if defined(ENABLE_FMRADIO)
#include "app/fm.h"
#endif
#include "bitmaps.h"
#include "driver/keyboard.h"
#include "driver/st7565.h"
#include "functions.h"
#include "helper/battery.h"
#include "misc.h"
#include "settings.h"
#include "ui/status.h"

void UI_DisplayStatus(void)
{
	memset(gStatusLine, 0, sizeof(gStatusLine));
	if (gCurrentFunction == FUNCTION_POWER_SAVE) {
		memcpy(gStatusLine, BITMAP_PowerSave, sizeof(BITMAP_PowerSave));
	}

	switch (gBatteryDisplayLevel) {
	case 5:
		memcpy(gStatusLine + 110, BITMAP_BatteryLevel5, sizeof(BITMAP_BatteryLevel5));
		break;
	case 4:
		memcpy(gStatusLine + 110, BITMAP_BatteryLevel4, sizeof(BITMAP_BatteryLevel4));
		break;
	case 3:
		memcpy(gStatusLine + 110, BITMAP_BatteryLevel3, sizeof(BITMAP_BatteryLevel3));
		break;
	case 2:
		memcpy(gStatusLine + 110, BITMAP_BatteryLevel2, sizeof(BITMAP_BatteryLevel2));
		break;
	default:
		if (gLowBatteryBlink == 1) {
			memcpy(gStatusLine + 110, BITMAP_BatteryLevel1, sizeof(BITMAP_BatteryLevel1));
		}
	}

	if (gChargingWithTypeC) {
		memcpy(gStatusLine + 100, BITMAP_USB_C, sizeof(BITMAP_USB_C));
	}
	if (gEeprom.KEY_LOCK) {
		memcpy(gStatusLine + 90, BITMAP_KeyLock, sizeof(BITMAP_KeyLock));
	} else if (gWasFKeyPressed) {
		memcpy(gStatusLine + 90, BITMAP_F_Key, sizeof(BITMAP_F_Key));
	}
	if (gEeprom.CROSS_BAND_RX_TX != CROSS_BAND_OFF) {
		memcpy(gStatusLine + 58, BITMAP_WX, sizeof(BITMAP_WX));
	}
	if (gEeprom.DUAL_WATCH != DUAL_WATCH_OFF) {
		memcpy(gStatusLine + 45, BITMAP_TDR, sizeof(BITMAP_TDR));
	}
#if defined(ENABLE_FMRADIO)
	else if (gFmRadioMode) {
		memcpy(gStatusLine + 21, BITMAP_FM, sizeof(BITMAP_FM));
	}
#endif
	ST7565_BlitStatusLine();
}

