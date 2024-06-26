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
#include "driver/eeprom.h"
#if defined(ENABLE_UART)
#include "driver/uart.h"
#endif
#include "helper/battery.h"
#include "misc.h"
#include "settings.h"

EEPROM_Config_t gEeprom;
EEPROM_VFO_t gVFO;
EEPROM_Calibration_t gCalibration;

#if defined(ENABLE_FMRADIO)
EEPROM_FM_t gFM;

void SETTINGS_SaveFM(void)
{

#if defined(ENABLE_UART)
	UART_LogSend("sFm\r\n", 5);
#endif

	EEPROM_WriteBuffer(0x0E88, &gFM);
	for (uint8_t i = 0; i < 5; i++) {
		EEPROM_WriteBuffer(0x0E40 + (i * 8), &gFM_Channels[i * 4]);
	}
}
#endif

void SETTINGS_SaveVfoIndices(void)
{
	uint8_t State[8];

#if defined(ENABLE_UART)
	UART_LogSend("sidx\r\n", 6);
#endif

	State[0] = gEeprom.ScreenChannel[0];
	State[1] = gEeprom.MrChannel[0];
	State[2] = gEeprom.FreqChannel[0];
	State[3] = gEeprom.ScreenChannel[1];
	State[4] = gEeprom.MrChannel[1];
	State[5] = gEeprom.FreqChannel[1];
	for (uint8_t i = 6; i < 8; i++) {
		State[i] = 0xFF;
	}

	EEPROM_WriteBuffer(0x0E80, State);
}

void SETTINGS_SaveSettings(void)
{
	uint8_t State[8];
	uint32_t Buf[2];

#if defined(ENABLE_UART)
	UART_LogSend("spub\r\n", 6);
#endif

	State[0] = gEeprom.CHAN_1_CALL;
	State[1] = gEeprom.SQUELCH_LEVEL;
	State[2] = gEeprom.TX_TIMEOUT_TIMER;
	State[3] = 0xFF;
	State[4] = gEeprom.KEY_LOCK;
	for (uint8_t i = 5; i < 7; i++) {
		State[i] = 0xFF;
	}
	State[7] = gEeprom.MIC_SENSITIVITY;

	EEPROM_WriteBuffer(0x0E70, State);

	State[0] = 0xFF;
	State[1] = gEeprom.CHANNEL_DISPLAY_MODE;
	State[2] = gEeprom.CROSS_BAND_RX_TX;
	//State[3] = 0xFF;
	State[4] = gEeprom.DUAL_WATCH;
	State[5] = gEeprom.BACKLIGHT;
	State[6] = gEeprom.TAIL_NOTE_ELIMINATION;
	State[7] = gEeprom.VFO_OPEN;

	EEPROM_WriteBuffer(0x0E78, State);

	//State[0] = 0xFF;
	State[1] = gEeprom.KEY_1_SHORT_PRESS_ACTION;
	State[2] = gEeprom.KEY_1_LONG_PRESS_ACTION;
	State[3] = gEeprom.KEY_2_SHORT_PRESS_ACTION;
	State[4] = gEeprom.KEY_2_LONG_PRESS_ACTION;
	State[5] = gEeprom.SCAN_RESUME_MODE;
	State[6] = gEeprom.AUTO_KEYPAD_LOCK;
	State[7] = 0xFF;

	EEPROM_WriteBuffer(0x0E90, State);

	Buf[0] = gEeprom.POWER_ON_PASSWORD;
	Buf[1] = 0xFF;

	EEPROM_WriteBuffer(0x0E98, Buf);

	Buf[0] = gEeprom.MDC1200_ID;
	//Buf[1] = 0xFF;

	EEPROM_WriteBuffer(0x0EA0, Buf);

	//State[0] = 0xFF;
	State[1] = gEeprom.ROGER;
	State[2] = gEeprom.REPEATER_TAIL_TONE_ELIMINATION;
	State[3] = gEeprom.TX_VFO;
	for (uint8_t i = 4; i < 8; i++) {
		State[i] = 0xFF;
	}

	EEPROM_WriteBuffer(0x0EA8, State);

	State[0] = gEeprom.DTMF_SIDE_TONE;
	State[1] = gEeprom.DTMF_SEPARATE_CODE;
	State[2] = gEeprom.DTMF_GROUP_CALL_CODE;
	State[3] = gEeprom.DTMF_DECODE_RESPONSE;
	State[4] = gEeprom.DTMF_AUTO_RESET_TIME;
	State[5] = gEeprom.DTMF_PRELOAD_TIME / 10U;
	State[6] = gEeprom.DTMF_FIRST_CODE_PERSIST_TIME / 10U;
	State[7] = gEeprom.DTMF_HASH_CODE_PERSIST_TIME / 10U;

	EEPROM_WriteBuffer(0x0ED0, State);

	memset(State, 0xFF, sizeof(State));

	State[0] = gEeprom.DTMF_CODE_PERSIST_TIME / 10U;
	State[1] = gEeprom.DTMF_CODE_INTERVAL_TIME / 10U;
	for (uint8_t i = 2; i < 8; i++) {
		State[i] = 0xFF;
	}

	EEPROM_WriteBuffer(0x0ED8, State);

	State[0] = gEeprom.SCAN_LIST_DEFAULT;
	State[1] = gEeprom.SCAN_LIST_ENABLED[0];
	State[2] = gEeprom.SCANLIST_PRIORITY_CH1[0];
	State[3] = gEeprom.SCANLIST_PRIORITY_CH2[0];
	State[4] = gEeprom.SCAN_LIST_ENABLED[1];
	State[5] = gEeprom.SCANLIST_PRIORITY_CH1[1];
	State[6] = gEeprom.SCANLIST_PRIORITY_CH2[1];
	State[7] = 0xFF;

	EEPROM_WriteBuffer(0x0F18, State);

	State[0] = gSetting_F_LOCK;
	State[1] = gSetting_350TX;
	State[2] = 0xFF;
	State[3] = gSetting_200TX;
	State[4] = gSetting_500TX;
	for (uint8_t i = 5; i < 8; i++) {
		State[i] = 0xFF;
	}

	EEPROM_WriteBuffer(0x0F40, State);

	Buf[0] = gBatteryCalibration[4];
	Buf[1] = gBatteryCalibration[5];

	EEPROM_WriteBuffer(0x1F48, Buf);
}

void SETTINGS_SaveChannel(uint8_t Channel, uint8_t VFO, const VFO_Info_t *pVFO, uint8_t Mode)
{
#if defined(ENABLE_UART)
	UART_LogSend("schn\r\n", 6);
#endif

	uint16_t OffsetMR;
	uint16_t OffsetVFO;

	if (!IS_MR_CHANNEL(Channel)) {
		if (VFO == 0) {
			OffsetVFO = 0x0C80 + ((Channel - FREQ_CHANNEL_FIRST) * 32);
		} else {
			OffsetVFO = 0x0C90 + ((Channel - FREQ_CHANNEL_FIRST) * 32);
		}
		OffsetMR = OffsetVFO;
	} else {
		OffsetMR = 0x0000 + (Channel * 16);
		OffsetVFO = OffsetMR;
	}

	if (Mode == 2 || !IS_MR_CHANNEL(Channel)) {
		uint32_t State32[2];
		uint8_t State8[8];

		State32[0] = pVFO->ConfigRX.Frequency;
		State32[1] = pVFO->FREQUENCY_OF_DEVIATION;

		EEPROM_WriteBuffer(OffsetVFO + 0, State32);

		State8[0] = pVFO->ConfigRX.Code;
		State8[1] = pVFO->ConfigTX.Code;
		State8[2] = (pVFO->ConfigTX.CodeType << 4) | pVFO->ConfigRX.CodeType;
		// Non-stock memory layout from now on
		State8[3] = (pVFO->FrequencyReverse << 4) | pVFO->FREQUENCY_DEVIATION_SETTING;
		State8[4] = (pVFO->BUSY_CHANNEL_LOCK << 4)
			| (pVFO->OUTPUT_POWER << 2)
			| (pVFO->CHANNEL_BANDWIDTH << 0);
		State8[5] = (pVFO->DTMF_PTT_ID_TX_MODE << 1) | pVFO->DTMF_DECODING_ENABLE;
		State8[6] = pVFO->STEP_SETTING;
		State8[7] = (pVFO->MDC1200_MODE << 4)
			| (pVFO->MODULATION_MODE << 2)
			| (pVFO->CompanderMode << 0);

		EEPROM_WriteBuffer(OffsetVFO + 8, State8);

		if (Mode > 0) {
			SETTINGS_UpdateChannel(Channel, pVFO, true);
		} else {
			// When the radio is reset and only one VFO is set, the other
			// will go through RADIO_Init with Mode=0 here. Don't give it
			// any attributes until it's configured by the user.
			SETTINGS_UpdateChannel(Channel, pVFO, false);
		}

		if (IS_MR_CHANNEL(Channel)) {
			// DualTachyon
			memset(&State32, 0, sizeof(State32));
			EEPROM_WriteBuffer(OffsetMR + 0x0F50, State32);
			EEPROM_WriteBuffer(OffsetMR + 0x0F58, State32);
		}
	}
}

void SETTINGS_UpdateChannel(uint8_t Channel, const VFO_Info_t *pVFO, bool bUpdate)
{
#if defined(ENABLE_UART)
	UART_LogSend("svalid\r\n", 8);
#endif

	uint8_t State[8];
	uint16_t Offset;
	uint8_t Attributes;

	Offset = 0x0D60 + (Channel & ~7U);
	EEPROM_ReadBuffer(Offset, State, sizeof(State));
	if (bUpdate) {
		Attributes = 0
			| (pVFO->SCANLIST1_PARTICIPATION << 7)
			| (pVFO->SCANLIST2_PARTICIPATION << 6)
			| (pVFO->Band << 0);
		if (State[Channel & 7U] == Attributes) {
			return;
		}
	} else {
		Attributes = 0xFF;
	}
	State[Channel & 7U] = Attributes;
	EEPROM_WriteBuffer(Offset, State);
	gMR_ChannelAttributes[Channel] = Attributes;
}

