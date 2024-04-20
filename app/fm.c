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
#include "app/fm.h"
#include "app/generic.h"
#include "bsp/dp32g030/gpio.h"
#if defined(ENABLE_FMRADIO)
#include "driver/bk1080.h"
#endif
#include "driver/eeprom.h"
#include "driver/gpio.h"
#include "functions.h"
#include "misc.h"
#include "settings.h"
#include "ui/inputbox.h"
#include "ui/ui.h"

const uint16_t FM_LowerLimit = 760;
const uint16_t FM_UpperLimit = 1080;

uint16_t gFM_Channels[20];
bool gFmRadioMode;
uint8_t gFmRadioCountdown;
volatile uint16_t gFmPlayCountdown;
volatile int8_t gFM_ScanState;
bool gFM_AutoScan;
uint8_t gFM_ChannelPosition;
bool gFM_FoundFrequency;
bool gFM_AutoScan;
uint8_t gFM_ResumeCountdown;
uint16_t gFM_RestoreCountdown;

bool FM_CheckValidChannel(uint8_t Channel)
{
	if (Channel < 20 && (gFM_Channels[Channel] >= FM_LowerLimit && gFM_Channels[Channel] < FM_UpperLimit)) {
		return true;
	}

	return false;
}

uint8_t FM_FindNextChannel(uint8_t Channel, uint8_t Direction)
{
	uint8_t i;

	for (i = 0; i < 20; i++) {
		if (Channel == 0xFF) {
			Channel = 19;
		} else if (Channel > 19) {
			Channel = 0;
		}
		if (FM_CheckValidChannel(Channel)) {
			return Channel;
		}
		Channel += Direction;
	}

	return 0xFF;
}

bool FM_ConfigureChannelState(void)
{
	uint8_t Channel;

	gEeprom.FM_FrequencyPlaying = gFM.SelectedFrequency;
	if (gFM.IsMrMode) {
		Channel = FM_FindNextChannel(gFM.SelectedChannel, FM_CHANNEL_UP);
		if (Channel == 0xFF) {
			gFM.IsMrMode = false;
			return true;
		}
		gFM.SelectedChannel = Channel;
		gEeprom.FM_FrequencyPlaying = gFM_Channels[Channel];
	}

	return false;
}

void FM_TurnOff(void)
{
	gFmRadioMode = false;
	gFM_ScanState = FM_SCAN_OFF;
	gFM_RestoreCountdown = 0;
	GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
	gEnableSpeaker = false;
	BK1080_Sleep();
	gUpdateStatus = true;
}

void FM_EraseChannels(void)
{
	uint8_t i;
	uint8_t Template[8];

	memset(Template, 0xFF, sizeof(Template));
	for (i = 0; i < 5; i++) {
		EEPROM_WriteBuffer(0x0E40 + (i * 8), Template);
	}

	memset(gFM_Channels, 0xFF, sizeof(gFM_Channels));
}

void FM_Tune(uint16_t Frequency, int8_t Step, bool bFlag)
{
	GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
	gEnableSpeaker = false;
	if (gFM_ScanState == FM_SCAN_OFF) {
		gFmPlayCountdown = 120;
	} else {
		gFmPlayCountdown = 10;
	}
	gFM_FoundFrequency = false;
	gAskToSave = false;
	gAskToDelete = false;
	gEeprom.FM_FrequencyPlaying = Frequency;
	if (!bFlag) {
		Frequency += Step;
		if (Frequency < FM_LowerLimit) {
			Frequency = FM_UpperLimit;
		} else if (Frequency > FM_UpperLimit) {
			Frequency = FM_LowerLimit;
		}
		gEeprom.FM_FrequencyPlaying = Frequency;
	}

	gFM_ScanState = Step;
	BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying);
}

void FM_PlayAndUpdate(void)
{
	gFM_ScanState = FM_SCAN_OFF;
	if (gFM_AutoScan) {
		gFM.IsMrMode = true;
		gFM.SelectedChannel = 0;
	}
	FM_ConfigureChannelState();
	BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying);
	SETTINGS_SaveFM();
	gFmPlayCountdown = 0;
	gAskToSave = false;
	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
	gEnableSpeaker = true;
}

bool FM_CheckFrequencyLock(uint16_t Frequency, uint16_t LowerLimit)
{
	uint16_t Test2 = BK1080_ReadRegister(BK1080_REG_07);
	// This is supposed to be a signed value, but above function is unsigned
	uint16_t Deviation = BK1080_REG_07_GET_FREQD(Test2);

	if (BK1080_REG_07_GET_SNR(Test2) >= 2) {
		uint16_t Status = BK1080_ReadRegister(BK1080_REG_10);

		if ((Status & BK1080_REG_10_MASK_AFCRL) == BK1080_REG_10_AFCRL_NOT_RAILED && BK1080_REG_10_GET_RSSI(Status) >= 10) {
			// if (Deviation > -281 && Deviation < 280)
			if (Deviation < 280 || Deviation > 3815) {
				// not BLE(less than or equal)
				if (Frequency > LowerLimit && (Frequency - BK1080_BaseFrequency) == 1) {
					if (BK1080_FrequencyDeviation & 0x800) {
						BK1080_FrequencyDeviation = Deviation;
						BK1080_BaseFrequency = Frequency;
						return true;
					}
					if (BK1080_FrequencyDeviation < 20) {
						BK1080_FrequencyDeviation = Deviation;
						BK1080_BaseFrequency = Frequency;
						return true;
					}
				}
				// not BLT(less than)
				if (Frequency >= LowerLimit && (BK1080_BaseFrequency - Frequency) == 1) {
					if ((BK1080_FrequencyDeviation & 0x800) == 0) {
						BK1080_FrequencyDeviation = Deviation;
						BK1080_BaseFrequency = Frequency;
						return true;
					}
					// if (BK1080_FrequencyDeviation > -21) {
					if (BK1080_FrequencyDeviation > 4075) {
						BK1080_FrequencyDeviation = Deviation;
						BK1080_BaseFrequency = Frequency;
						return true;
					}
				}
			}
		}
	}
	return false;
}

static void FM_Key_DIGITS(KEY_Code_t Key)
{
#define STATE_FREQ_MODE 0
#define STATE_MR_MODE   1
#define STATE_SAVE      2

	if (!gWasFKeyPressed) {
		uint8_t State;

		if (gAskToDelete) {
			return;
		}
		if (gAskToSave) {
			State = STATE_SAVE;
		} else {
			if (gFM_ScanState != FM_SCAN_OFF) {
				return;
			}
			if (gFM.IsMrMode) {
				State = STATE_MR_MODE;
			} else {
				State = STATE_FREQ_MODE;
			}
		}
		INPUTBOX_Append(Key);
		gRequestDisplayScreen = DISPLAY_FM;
		if (State == STATE_FREQ_MODE) {
			if (gInputBoxIndex == 1) {
				if (gInputBox[0] > 1) {
					gInputBox[1] = gInputBox[0];
					gInputBox[0] = 0;
					gInputBoxIndex = 2;
				}
			} else if (gInputBoxIndex > 3) {
				uint32_t Frequency;

				gInputBoxIndex = 0;
				NUMBER_Get(gInputBox, &Frequency);
				Frequency = Frequency / 10000;
				if (Frequency < FM_LowerLimit || FM_UpperLimit < Frequency) {
					gRequestDisplayScreen = DISPLAY_FM;
					return;
				}
				gFM.SelectedFrequency = (uint16_t)Frequency;
				gEeprom.FM_FrequencyPlaying = gFM.SelectedFrequency;
				BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying);
				gRequestSaveFM = true;
				return;
			}
		} else if (gInputBoxIndex == 2) {
			uint8_t Channel;

			gInputBoxIndex = 0;
			Channel = ((gInputBox[0] * 10) + gInputBox[1]) - 1;
			if (State == STATE_MR_MODE) {
				if (FM_CheckValidChannel(Channel)) {
					gFM.SelectedChannel = Channel;
					gEeprom.FM_FrequencyPlaying = gFM_Channels[Channel];
					BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying);
					gRequestSaveFM = true;
					return;
				}
			} else if (Channel < 20) {
				gRequestDisplayScreen = DISPLAY_FM;
				gInputBoxIndex = 0;
				gFM_ChannelPosition = Channel;
				return;
			}
			return;
		}
		return;
	}
	gWasFKeyPressed = false;
	gUpdateStatus = true;
	gRequestDisplayScreen = DISPLAY_FM;
	switch (Key) {
	case KEY_0:
		ACTION_FM();
		break;

	case KEY_1:
		gFM.IsMrMode = !gFM.IsMrMode;
		if (!FM_ConfigureChannelState()) {
			BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying);
			gRequestSaveFM = true;
		}
		break;

	case KEY_2:
		ACTION_Scan(true);
		break;

	case KEY_3:
		ACTION_Scan(false);
		break;

	default:
		break;
	}
}

static void FM_Key_EXIT(void)
{
	if (gFM_ScanState == FM_SCAN_OFF) {
		if (gInputBoxIndex == 0) {
			if (!gAskToSave && !gAskToDelete) {
				ACTION_FM();
				return;
			}
			gAskToSave = false;
			gAskToDelete = false;
		} else {
			gInputBoxIndex--;
			gInputBox[gInputBoxIndex] = 10;
			if (gInputBoxIndex > 0) {
				if (gInputBoxIndex != 1) {
					gRequestDisplayScreen = DISPLAY_FM;
					return;
				}
				if (gInputBox[0] != 0) {
					gRequestDisplayScreen = DISPLAY_FM;
					return;
				}
			}
			gInputBoxIndex = 0;
		}
	} else {
		FM_PlayAndUpdate();
	}
	gRequestDisplayScreen = DISPLAY_FM;
}

static void FM_Key_MENU(void)
{
	gRequestDisplayScreen = DISPLAY_FM;

	if (gFM_ScanState == FM_SCAN_OFF) {
		if (!gFM.IsMrMode) {
			if (gAskToSave) {
				gFM_Channels[gFM_ChannelPosition] = gEeprom.FM_FrequencyPlaying;
				gAskToSave = false;
				gRequestSaveFM = true;
			} else {
				gAskToSave = true;
			}
		} else {
			if (gAskToDelete) {
				gFM_Channels[gFM.SelectedChannel] = 0xFFFF;
				FM_ConfigureChannelState();
				BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying);
				gRequestSaveFM = true;
				gAskToDelete = false;
			} else {
				gAskToDelete = true;
			}
		}
	} else {
		if (gFM_AutoScan || !gFM_FoundFrequency) {
			gInputBoxIndex = 0;
			return;
		} else if (gAskToSave) {
			gFM_Channels[gFM_ChannelPosition] = gEeprom.FM_FrequencyPlaying;
			gAskToSave = false;
			gRequestSaveFM = true;
		} else {
			gAskToSave = true;
		}
	}
}

static void FM_Key_UP_DOWN(bool bKeyPressed, int8_t Step)
{
	if (!bKeyPressed || gInputBoxIndex > 0) {
		return;
	}
	if (gAskToSave) {
		gRequestDisplayScreen = DISPLAY_FM;
		gFM_ChannelPosition = NUMBER_AddWithWraparound(gFM_ChannelPosition, Step, 0, 19);
		return;
	}
	if (gFM_ScanState != FM_SCAN_OFF) {
		if (gFM_AutoScan) {
			return;
		}
		FM_Tune(gEeprom.FM_FrequencyPlaying, Step, false);
		gRequestDisplayScreen = DISPLAY_FM;
		return;
	}
	if (gFM.IsMrMode) {
		uint8_t Channel;

		Channel = FM_FindNextChannel(gFM.SelectedChannel + Step, Step);
		if (Channel == 0xFF || gFM.SelectedChannel == Channel) {
			BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying);
			gRequestDisplayScreen = DISPLAY_FM;
			return;
		}
		gFM.SelectedChannel = Channel;
		gEeprom.FM_FrequencyPlaying = gFM_Channels[Channel];
	} else {
		uint16_t Frequency;

		Frequency = gFM.SelectedFrequency + Step;
		if (Frequency < FM_LowerLimit) {
			Frequency = FM_UpperLimit;
		} else if (Frequency > FM_UpperLimit) {
			Frequency = FM_LowerLimit;
		}
		gEeprom.FM_FrequencyPlaying = Frequency;
		gFM.SelectedFrequency = gEeprom.FM_FrequencyPlaying;
	}
	gRequestSaveFM = true;

	BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying);
	gRequestDisplayScreen = DISPLAY_FM;
}

void FM_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
	switch (Key) {
	case KEY_0: case KEY_1: case KEY_2: case KEY_3:
	case KEY_4: case KEY_5: case KEY_6: case KEY_7:
	case KEY_8: case KEY_9:
		if (!bKeyHeld && bKeyPressed) {
			FM_Key_DIGITS(Key);
		}
		break;
	case KEY_MENU:
		if (!bKeyHeld && bKeyPressed) {
			FM_Key_MENU();
		}
		return;
	case KEY_UP:
		FM_Key_UP_DOWN(bKeyPressed, 1);
		break;
	case KEY_DOWN:
		FM_Key_UP_DOWN(bKeyPressed, -1);
		break;;
	case KEY_EXIT:
		if (!bKeyHeld && bKeyPressed) {
			FM_Key_EXIT();
		}
		break;
	case KEY_F:
		GENERIC_Key_F(bKeyPressed, bKeyHeld);
		break;
	default:
		break;
	}
}

void FM_Play(void)
{
	if (FM_CheckFrequencyLock(gEeprom.FM_FrequencyPlaying, FM_LowerLimit)) {
		if (!gFM_AutoScan) {
			gFmPlayCountdown = 0;
			gFM_FoundFrequency = true;
			if (!gFM.IsMrMode) {
				gFM.SelectedFrequency = gEeprom.FM_FrequencyPlaying;
			}
			GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
			gEnableSpeaker = true;
			GUI_SelectNextDisplay(DISPLAY_FM);
			return;
		}
		if (gFM_ChannelPosition < 20) {
			gFM_Channels[gFM_ChannelPosition++] = gEeprom.FM_FrequencyPlaying;
		}
		if (gFM_ChannelPosition >= 20) {
			FM_PlayAndUpdate();
			GUI_SelectNextDisplay(DISPLAY_FM);
			return;
		}
	}

	if (gFM_AutoScan && gEeprom.FM_FrequencyPlaying >= FM_UpperLimit) {
		FM_PlayAndUpdate();
	} else {
		FM_Tune(gEeprom.FM_FrequencyPlaying, gFM_ScanState, false);
	}

	GUI_SelectNextDisplay(DISPLAY_FM);
}

void FM_Start(void)
{
	gFmRadioMode = true;
	gFM_ScanState = FM_SCAN_OFF;
	gFM_RestoreCountdown = 0;
	BK1080_Enable();
	BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying);
	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
	gEnableSpeaker = true;
	gUpdateStatus = true;
}

