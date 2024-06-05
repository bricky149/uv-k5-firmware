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
#include "frequencies.h"
#include "functions.h"
#include "helper/battery.h"
#if defined(ENABLE_MDC1200)
#include "mdc1200.h"
#endif
#include "misc.h"
#include "scheduler.h"
#include "settings.h"
#include "ui/battery.h"
#include "ui/inputbox.h"
#include "ui/menu.h"
#include "ui/rssi.h"
#include "ui/status.h"
#include "ui/ui.h"

static void TASK_ProcessKey(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
	if (gCurrentFunction == FUNCTION_POWER_SAVE) {
		FUNCTION_Select(FUNCTION_FOREGROUND);
	}
	gBatterySaveCountdown = 1000;
	if (gEeprom.AUTO_KEYPAD_LOCK) {
		gKeyLockCountdown = 30;
	}

	if (bKeyPressed) {
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
}

void TASK_CheckKeys(void)
{
	if (!SCHEDULER_CheckTask(TASK_CHECK_KEYS)) {
		return;
	}
	SCHEDULER_ClearTask(TASK_CHECK_KEYS);
	
	KEY_Code_t Key = KEYBOARD_Poll();

	if (gKeyReading0 != Key) {
		if (gKeyReading0 != KEY_INVALID && Key != KEY_INVALID) {
			TASK_ProcessKey(gKeyReading1, false, gKeyBeingHeld);
		}
		gKeyReading0 = Key;
		gDebounceCounter = 0;
		return;
	}
	gDebounceCounter++;
	if (gDebounceCounter == 2) {
		if (Key == KEY_INVALID) {
			if (gKeyReading1 != KEY_INVALID) {
				TASK_ProcessKey(gKeyReading1, false, gKeyBeingHeld);
				gKeyReading1 = KEY_INVALID;
			}
		} else {
			gKeyReading1 = Key;
			TASK_ProcessKey(Key, true, false);
		}
		gKeyBeingHeld = false;
	} else if (gDebounceCounter == 128) {
		if (Key == KEY_STAR || Key == KEY_F || Key == KEY_SIDE2 || Key == KEY_SIDE1 || Key == KEY_UP || Key == KEY_DOWN) {
			gKeyBeingHeld = true;
			TASK_ProcessKey(Key, true, true);
		}
	} else if (gDebounceCounter > 128) {
		if (Key == KEY_UP || Key == KEY_DOWN) {
			gKeyBeingHeld = true;
			if ((gDebounceCounter & 15) == 0) {
				TASK_ProcessKey(Key, true, true);
			}
		}
		if (gDebounceCounter != 0xFFFF) {
			return;
		}
		gDebounceCounter = 128;
	}
}
