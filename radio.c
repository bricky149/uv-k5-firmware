/* Copyright 2023 Dual Tachyon
 * Copyright 2023 OneOfEleven
 * Copyright 2024 mobilinkd
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
#if defined(ENABLE_FMRADIO)
#include "app/fm.h"
#endif
#include "bsp/dp32g030/gpio.h"
#include "dcs.h"
#include "driver/bk4819.h"
#include "driver/eeprom.h"
#include "driver/gpio.h"
#include "driver/system.h"
#include "frequencies.h"
#include "functions.h"
#include "helper/battery.h"
#if defined(ENABLE_MDC1200)
#include "mdc1200.h"
#endif
#include "misc.h"
#include "radio.h"
#include "settings.h"

VFO_Info_t *gTxVfo;
VFO_Info_t *gRxVfo;
VFO_Info_t *gCurrentVfo;

DCS_CodeType_t gCurrentCodeType;
DCS_CodeType_t gSelectedCodeType;
uint8_t gSelectedCode;

STEP_Setting_t gStepSetting;

VfoState_t VfoState[2];

bool RADIO_CheckValidChannel(uint16_t Channel, bool bCheckScanList, uint8_t VFO)
{
	uint8_t Attributes;
	uint8_t PriorityCh1;
	uint8_t PriorityCh2;

	if (!IS_MR_CHANNEL(Channel)) {
		return false;
	}

	// Check channel is valid
	Attributes = gMR_ChannelAttributes[Channel];
	if ((Attributes & MR_CH_BAND_MASK) > BAND7_470MHz) {
		return false;
	}
	
	if (bCheckScanList) {
		switch (VFO) {
		case 0:
			if ((Attributes & MR_CH_SCANLIST1) == 0) {
				return false;
			}
			PriorityCh1 = gEeprom.SCANLIST_PRIORITY_CH1[0];
			PriorityCh2 = gEeprom.SCANLIST_PRIORITY_CH2[0];
			break;
		case 1:
			if ((Attributes & MR_CH_SCANLIST2) == 0) {
				return false;
			}
			PriorityCh1 = gEeprom.SCANLIST_PRIORITY_CH1[1];
			PriorityCh2 = gEeprom.SCANLIST_PRIORITY_CH2[1];
			break;
		default:
			return true;
		}
		if (PriorityCh1 == Channel) {
			return false;
		}
		if (PriorityCh2 == Channel) {
			return false;
		}
	}

	return true;
}

uint8_t RADIO_FindNextChannel(uint8_t Channel, int8_t Direction, bool bCheckScanList, uint8_t VFO)
{
	uint8_t i;

	for (i = 0; i <= MR_CHANNEL_LAST; i++) {
		if (Channel == 0xFF) {
			Channel = MR_CHANNEL_LAST;
		} else if (Channel > MR_CHANNEL_LAST) {
			Channel = MR_CHANNEL_FIRST;
		}
		if (RADIO_CheckValidChannel(Channel, bCheckScanList, VFO)) {
			return Channel;
		}
		Channel += Direction;
	}

	return 0xFF;
}

void RADIO_InitInfo(VFO_Info_t *pInfo, uint8_t ChannelSave, uint8_t Band, uint32_t Frequency)
{
	memset(pInfo, 0, sizeof(*pInfo));
	pInfo->Band = Band;
	pInfo->STEP_SETTING = STEP_6_25kHz;
	pInfo->StepFrequency = StepFrequencyTable[3];
	pInfo->CHANNEL_SAVE = ChannelSave;
	pInfo->ConfigRX.Frequency = Frequency;
	pInfo->ConfigTX.Frequency = Frequency;
	pInfo->pRX = &pInfo->ConfigRX;
	pInfo->pTX = &pInfo->ConfigTX;
	if (ChannelSave == (FREQ_CHANNEL_FIRST + BAND2_108MHz)) {
		pInfo->MODULATION_MODE = MOD_AM;
	}
	RADIO_ConfigureSquelchAndOutputPower(pInfo);
}

void RADIO_ConfigureChannel(uint8_t VFO, uint32_t Configure)
{
	VFO_Info_t *pRadio;
	uint8_t Channel;
	uint8_t Attributes;
	uint8_t Band;
	bool bParticipation2;
	uint16_t Base;
	uint32_t Frequency;

	pRadio = &gVFO.Info[VFO];

	Channel = gEeprom.ScreenChannel[VFO];
	if (IS_VALID_CHANNEL(Channel)) {
		if (IS_MR_CHANNEL(Channel)) {
			Channel = RADIO_FindNextChannel(Channel, RADIO_CHANNEL_UP, false, VFO);
			if (Channel == 0xFF) {
				Channel = gEeprom.FreqChannel[VFO];
				gEeprom.ScreenChannel[VFO] = gEeprom.FreqChannel[VFO];
			} else {
				gEeprom.ScreenChannel[VFO] = Channel;
				gEeprom.MrChannel[VFO] = Channel;
			}
		}
	} else {
		Channel = FREQ_CHANNEL_FIRST + BAND6_400MHz;
	}

	Attributes = gMR_ChannelAttributes[Channel];
	if (Attributes == 0xFF) {
		uint8_t Index;

		if (IS_MR_CHANNEL(Channel)) {
			Channel = gEeprom.FreqChannel[VFO];
			gEeprom.ScreenChannel[VFO] = gEeprom.FreqChannel[VFO];
		}
		Index = Channel - FREQ_CHANNEL_FIRST;
		RADIO_InitInfo(pRadio, Channel, Index, LowerLimitFrequencyBandTable[Index]);
		// Out-of-bounds when radio is reset, a VFO is set and radio is rebooted
		// Save any non-configured VFO to prevent the above from happening
		SETTINGS_SaveChannel(Channel, VFO, pRadio, 0);
		return;
	}

	Band = Attributes & MR_CH_BAND_MASK;
	if (Band > BAND7_470MHz) {
		Band = BAND6_400MHz;
	}

	if (IS_MR_CHANNEL(Channel)) {
		gVFO.Info[VFO].Band = Band;
		gVFO.Info[VFO].SCANLIST1_PARTICIPATION = !!(Attributes & MR_CH_SCANLIST1);
		bParticipation2 = !!(Attributes & MR_CH_SCANLIST2);
	} else {
		Band = Channel - FREQ_CHANNEL_FIRST;
		gVFO.Info[VFO].Band = Band;
		bParticipation2 = true;
		gVFO.Info[VFO].SCANLIST1_PARTICIPATION = true;
	}
	gVFO.Info[VFO].SCANLIST2_PARTICIPATION = bParticipation2;
	gVFO.Info[VFO].CHANNEL_SAVE = Channel;

	if (IS_MR_CHANNEL(Channel)) {
		Base = Channel * 16;
	} else {
		Base = 0x0C80 + ((Channel - FREQ_CHANNEL_FIRST) * 32) + (VFO * 16);
	}

	uint8_t Data[8];
	memset(Data, 0, sizeof(Data));

	if (Configure == VFO_CONFIGURE_RELOAD || Channel >= FREQ_CHANNEL_FIRST) {
		struct {
			uint32_t Frequency;
			uint32_t Offset;
		} Info;
		EEPROM_ReadBuffer(Base, &Info, sizeof(Info));

		gVFO.Info[VFO].ConfigRX.Frequency = Info.Frequency;
		gVFO.Info[VFO].FREQUENCY_OF_DEVIATION = Info.Offset;

		EEPROM_ReadBuffer(Base + 8, Data, 8);

		gVFO.Info[VFO].ConfigRX.Code = Data[0];
		gVFO.Info[VFO].ConfigTX.Code = Data[1];

		gVFO.Info[VFO].ConfigRX.CodeType = (Data[2] & 0x0F);
		gVFO.Info[VFO].ConfigTX.CodeType = (Data[2] >> 4) & 0x0F;

		// Non-stock memory layout from now on
		gVFO.Info[VFO].FREQUENCY_DEVIATION_SETTING = (Data[3] & 3);
		gVFO.Info[VFO].FrequencyReverse = (Data[3] >> 4) & 1;

		gVFO.Info[VFO].CHANNEL_BANDWIDTH = (Data[4] & 3);
		gVFO.Info[VFO].OUTPUT_POWER = (Data[4] >> 2) & 3;
		gVFO.Info[VFO].BUSY_CHANNEL_LOCK = (Data[4] >> 4) & 1;

		gVFO.Info[VFO].DTMF_DECODING_ENABLE = (Data[5] & 1);
		gVFO.Info[VFO].DTMF_PTT_ID_TX_MODE = (Data[5] >> 1) & 3;

		gVFO.Info[VFO].STEP_SETTING = Data[6];
		gVFO.Info[VFO].StepFrequency = StepFrequencyTable[Data[6]];

		gVFO.Info[VFO].CompanderMode = (Data[7] & 3);
		gVFO.Info[VFO].MODULATION_MODE = (Data[7] >> 2) & 3;
#if defined (ENABLE_MDC1200)
		gVFO.Info[VFO].MDC1200_MODE = (Data[7] >> 4) & 3;
#endif
	}

	Frequency = gVFO.Info[VFO].ConfigRX.Frequency;
	if (Frequency < LowerLimitFrequencyBandTable[Band]) {
		gVFO.Info[VFO].ConfigRX.Frequency = LowerLimitFrequencyBandTable[Band];
	} else if (Frequency > UpperLimitFrequencyBandTable[Band]) {
		gVFO.Info[VFO].ConfigRX.Frequency = UpperLimitFrequencyBandTable[Band];
	} else if (Channel >= FREQ_CHANNEL_FIRST) {
		gVFO.Info[VFO].ConfigRX.Frequency = FREQUENCY_FloorToStep(pRadio->ConfigRX.Frequency, gVFO.Info[VFO].StepFrequency, LowerLimitFrequencyBandTable[Band]);
	}

	if (Frequency >= 10800000 && Frequency <= 13599990) {
		gVFO.Info[VFO].FREQUENCY_DEVIATION_SETTING = FREQUENCY_DEVIATION_OFF;
	} else if (!IS_MR_CHANNEL(Channel)) {
		Frequency = FREQUENCY_FloorToStep(pRadio->FREQUENCY_OF_DEVIATION, pRadio->StepFrequency, 0);
		gVFO.Info[VFO].FREQUENCY_OF_DEVIATION = Frequency;
	}
	RADIO_ApplyOffset(pRadio);
	if (IS_MR_CHANNEL(Channel)) {
		memset(gVFO.Info[VFO].Name, 0, sizeof(gVFO.Info[VFO].Name));
		EEPROM_ReadBuffer(0x0F50 + (Channel * 0x10), gVFO.Info[VFO].Name, 16);
		// 16 bytes allocated but only 12 used
		//EEPROM_ReadBuffer(0x0F50 + (Channel * 0x10), gVFO.Info[VFO].Name + 0, 8);
		//EEPROM_ReadBuffer(0x0F58 + (Channel * 0x10), gVFO.Info[VFO].Name + 8, 8);
	}

	if (!gVFO.Info[VFO].FrequencyReverse) {
		gVFO.Info[VFO].pRX = &gVFO.Info[VFO].ConfigRX;
		gVFO.Info[VFO].pTX = &gVFO.Info[VFO].ConfigTX;
	} else {
		gVFO.Info[VFO].pRX = &gVFO.Info[VFO].ConfigTX;
		gVFO.Info[VFO].pTX = &gVFO.Info[VFO].ConfigRX;
	}

	if (gVFO.Info[VFO].Band == BAND2_108MHz) {
		// Airband
		gVFO.Info[VFO].MODULATION_MODE = MOD_AM;
	}
	if (gVFO.Info[VFO].MODULATION_MODE != MOD_FM) {
		gVFO.Info[VFO].DTMF_DECODING_ENABLE = false;
		gVFO.Info[VFO].ConfigRX.CodeType = CODE_TYPE_OFF;
		gVFO.Info[VFO].ConfigTX.CodeType = CODE_TYPE_OFF;
		gVFO.Info[VFO].CompanderMode = COMPND_OFF;
	}
	if (gVFO.Info[VFO].MODULATION_MODE == MOD_USB) {
		// SSB will not work with any other bandwidth mode
		gVFO.Info[VFO].CHANNEL_BANDWIDTH = BANDWIDTH_NARROWER;
	}

	RADIO_ConfigureSquelchAndOutputPower(pRadio);
}

void RADIO_ConfigureSquelchAndOutputPower(VFO_Info_t *pInfo)
{
	uint16_t Base;

	FREQUENCY_Band_t Band = FREQUENCY_GetBand(pInfo->pRX->Frequency);
	if (Band < BAND4_174MHz) {
		Base = 0x1E60;
	} else {
		Base = 0x1E00;
	}

	if (gEeprom.SQUELCH_LEVEL == 0) {
		pInfo->SquelchOpenRSSI = 0x00;
		pInfo->SquelchOpenNoise = 0x7F;
		pInfo->SquelchCloseGlitch = 0xFF;
		pInfo->SquelchCloseRSSI = 0x00;
		pInfo->SquelchCloseNoise = 0x7F;
		pInfo->SquelchOpenGlitch = 0xFF;
	} else {
		Base += gEeprom.SQUELCH_LEVEL;
		EEPROM_ReadBuffer(Base + 0x00, &pInfo->SquelchOpenRSSI, 1);
		EEPROM_ReadBuffer(Base + 0x10, &pInfo->SquelchCloseRSSI, 1);
		EEPROM_ReadBuffer(Base + 0x20, &pInfo->SquelchOpenNoise, 1);
		EEPROM_ReadBuffer(Base + 0x30, &pInfo->SquelchCloseNoise, 1);
		EEPROM_ReadBuffer(Base + 0x40, &pInfo->SquelchCloseGlitch, 1);
		EEPROM_ReadBuffer(Base + 0x50, &pInfo->SquelchOpenGlitch, 1);
		if (pInfo->SquelchOpenNoise >= 0x80) {
			pInfo->SquelchOpenNoise = 0x7F;
		}
		if (pInfo->SquelchCloseNoise >= 0x80) {
			pInfo->SquelchCloseNoise = 0x7F;
		}
	}

	uint8_t Txp[3];
	Band = FREQUENCY_GetBand(pInfo->pTX->Frequency);
	EEPROM_ReadBuffer(0x1ED0 + (Band * 0x10) + (pInfo->OUTPUT_POWER * 3), Txp, 3);
	// 1o11
	if (pInfo->OUTPUT_POWER == OUTPUT_POWER_LOW) {
		// Tested at 1.5W on 70cm band
		for (int i = 0; i < 3; i++) {
			Txp[i] /= 3;
		}
	}

	pInfo->TXP_CalculatedSetting =
		FREQUENCY_CalculateOutputPower(
				Txp[0],
				Txp[1],
				Txp[2],
				LowerLimitFrequencyBandTable[Band],
				MiddleFrequencyBandTable[Band],
				UpperLimitFrequencyBandTable[Band],
				pInfo->pTX->Frequency);
}

void RADIO_ApplyOffset(VFO_Info_t *pInfo)
{
	uint32_t Frequency;

	Frequency = pInfo->ConfigRX.Frequency;
	switch (pInfo->FREQUENCY_DEVIATION_SETTING) {
	case FREQUENCY_DEVIATION_ADD:
		Frequency += pInfo->FREQUENCY_OF_DEVIATION;
		break;
	case FREQUENCY_DEVIATION_SUB:
		Frequency -= pInfo->FREQUENCY_OF_DEVIATION;
		break;
	default:
		break;
	}

	if (Frequency < 5000000) {
		Frequency = 5000000;
	} else if (Frequency > 60000000) {
		Frequency = 60000000;
	}

	pInfo->ConfigTX.Frequency = Frequency;
}

static void RADIO_SelectCurrentVfo(void)
{
	if (gEeprom.CROSS_BAND_RX_TX == CROSS_BAND_OFF) {
		gCurrentVfo = gRxVfo;
	} else {
		gCurrentVfo = &gVFO.Info[gEeprom.TX_VFO];
	}
}

void RADIO_SelectVfos(void)
{
	if (gEeprom.CROSS_BAND_RX_TX == CROSS_BAND_CHAN_B) {
		gEeprom.TX_VFO = 1;
	} else if (gEeprom.CROSS_BAND_RX_TX == CROSS_BAND_CHAN_A) {
		gEeprom.TX_VFO = 0;
	} else if (gEeprom.DUAL_WATCH == DUAL_WATCH_CHAN_B) {
		gEeprom.TX_VFO = 1;
	} else if (gEeprom.DUAL_WATCH == DUAL_WATCH_CHAN_A) {
		gEeprom.TX_VFO = 0;
	}

	gTxVfo = &gVFO.Info[gEeprom.TX_VFO];
	if (gEeprom.CROSS_BAND_RX_TX == CROSS_BAND_OFF) {
		gEeprom.RX_VFO = gEeprom.TX_VFO;
	} else {
		if (gEeprom.TX_VFO == 0) {
			gEeprom.RX_VFO = 1;
		} else {
			gEeprom.RX_VFO = 0;
		}
	}

	gRxVfo = &gVFO.Info[gEeprom.RX_VFO];
	RADIO_SelectCurrentVfo();
}

void RADIO_SetupRegisters(bool bSwitchToFunction0)
{
	uint16_t Status;
	uint16_t InterruptMask;
	uint32_t Frequency;

	BK4819_FilterBandwidth_t Bandwidth = gRxVfo->CHANNEL_BANDWIDTH;
#if defined(ENABLE_DIGITAL_MODULATION)
	if (gRxVfo->MODULATION_MODE == MOD_DIG) {
		if (Bandwidth == BK4819_FILTER_BW_WIDE) {
			Bandwidth = BK4819_FILTER_BW_DIGITAL_WIDE;
		} else {
			Bandwidth = BK4819_FILTER_BW_DIGITAL_NARROW;
		}
		BK4819_SetFilterBandwidth(Bandwidth, false);
	} else
	// Do not turn off the audio path for digital modulation.
	// This is needed to reduce turn-around time.
#endif
	{
		GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
		gEnableSpeaker = false;
		BK4819_ClearGpioOut(BK4819_GPIO6_PIN2_GREEN);
		BK4819_SetFilterBandwidth(Bandwidth, true);
	}

	BK4819_ClearGpioOut(BK4819_GPIO5_PIN1_RED);
	BK4819_SetupPowerAmplifier(0, 0);
	BK4819_ClearGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE);

	while (1) {
		Status = BK4819_ReadRegister(BK4819_REG_0C);
		if ((Status & 1U) == 0) { // INTERRUPT REQUEST
			break;
		}
		BK4819_WriteRegister(BK4819_REG_02, 0);
	}
	BK4819_WriteRegister(BK4819_REG_3F, 0);
#if defined(ENABLE_DIGITAL_MODULATION)
	if (gRxVfo->MODULATION_MODE == MOD_DIG) {
		BK4819_WriteRegister(BK4819_REG_7D, 0xE940);
	} else
#endif
	{
		BK4819_WriteRegister(BK4819_REG_7D, gEeprom.MIC_SENSITIVITY_TUNING | 0xE940);
	}
	Frequency = gRxVfo->pRX->Frequency;
	BK4819_SetFrequency(Frequency);
	BK4819_SetupSquelch(
			gRxVfo->SquelchOpenRSSI, gRxVfo->SquelchCloseRSSI,
			gRxVfo->SquelchOpenNoise, gRxVfo->SquelchCloseNoise,
			gRxVfo->SquelchCloseGlitch, gRxVfo->SquelchOpenGlitch);
	BK4819_SelectFilter(Frequency);
	BK4819_SetGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE);
	BK4819_WriteRegister(BK4819_REG_48, 0xB3A8);

	InterruptMask = 0
		| BK4819_REG_3F_SQUELCH_FOUND
		| BK4819_REG_3F_SQUELCH_LOST
		;

	if (gRxVfo->MODULATION_MODE == MOD_FM) {
		uint8_t CodeType;
		uint8_t Code;

		CodeType = gSelectedCodeType;
		Code = gSelectedCode;
		if (gCssScanMode == CSS_SCAN_MODE_OFF) {
			CodeType = gRxVfo->pRX->CodeType;
			Code = gRxVfo->pRX->Code;
		}
		switch (CodeType) {
		case CODE_TYPE_DIGITAL:
		case CODE_TYPE_REVERSE_DIGITAL:
			BK4819_SetCDCSSCodeWord(DCS_GetGolayCodeWord(CodeType, Code));
			InterruptMask = 0
				| BK4819_REG_3F_CxCSS_TAIL
				| BK4819_REG_3F_CDCSS_FOUND
				| BK4819_REG_3F_CDCSS_LOST
				| BK4819_REG_3F_SQUELCH_FOUND
				| BK4819_REG_3F_SQUELCH_LOST
				;
			break;
		case CODE_TYPE_CONTINUOUS_TONE:
			BK4819_SetCTCSSFrequency(CTCSS_Options[Code]);
			BK4819_Set55HzTailDetection();
			InterruptMask = 0
				| BK4819_REG_3F_CxCSS_TAIL
				| BK4819_REG_3F_CTCSS_FOUND
				| BK4819_REG_3F_CTCSS_LOST
				| BK4819_REG_3F_SQUELCH_FOUND
				| BK4819_REG_3F_SQUELCH_LOST
				;
			break;
		default:
			BK4819_SetCTCSSFrequency(670);
			BK4819_Set55HzTailDetection();
			InterruptMask = 0
				| BK4819_REG_3F_CxCSS_TAIL
				| BK4819_REG_3F_SQUELCH_FOUND
				| BK4819_REG_3F_SQUELCH_LOST
				;
			break;
		}
	}
	BK4819_SetModulation(gRxVfo->MODULATION_MODE);
	BK4819_SetAGC(gRxVfo->MODULATION_MODE);

	if (gRxVfo->MODULATION_MODE != MOD_FM || !gRxVfo->DTMF_DECODING_ENABLE) {
		BK4819_DisableDTMF();
#if defined(ENABLE_MDC1200)
		BK4819_DisableMDC1200Rx();
#endif
	} else {
#if defined(ENABLE_DIGITAL_MODULATION)
	if (gRxVfo->MODULATION_MODE != MOD_DIG) {
#endif
		BK4819_EnableDTMF();
		InterruptMask |= BK4819_REG_3F_DTMF_5TONE_FOUND;
#if defined(ENABLE_DIGITAL_MODULATION)
	}
#endif
#if defined(ENABLE_MDC1200)
		BK4819_EnableMDC1200Rx();
		InterruptMask |= BK4819_REG_3F_FSK_RX_SYNC | BK4819_REG_3F_FSK_RX_FINISHED | BK4819_REG_3F_FSK_FIFO_ALMOST_FULL;
#endif
	}
	BK4819_WriteRegister(BK4819_REG_3F, InterruptMask);

	FUNCTION_Init();

	if (bSwitchToFunction0) {
		FUNCTION_Select(FUNCTION_FOREGROUND);
	}
}

void RADIO_SetTxParameters(void)
{
	BK4819_FilterBandwidth_t Bandwidth = gCurrentVfo->CHANNEL_BANDWIDTH;
#if defined(ENABLE_DIGITAL_MODULATION)
	if (gCurrentVfo->MODULATION_MODE == MOD_DIG) {
		if (Bandwidth == BK4819_FILTER_BW_WIDE) {
			Bandwidth = BK4819_FILTER_BW_DIGITAL_WIDE;
		} else {
			Bandwidth = BK4819_FILTER_BW_DIGITAL_NARROW;
		}
		BK4819_SetFilterBandwidth(Bandwidth, false);
	} else
	// Do not turn off the audio path for digital modulation.
	// This is needed to reduce turn-around time.
#endif
	{
		GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
		gEnableSpeaker = false;
		BK4819_ClearGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE);
		BK4819_SetFilterBandwidth(Bandwidth, true);
	}

	BK4819_SetFrequency(gCurrentVfo->pTX->Frequency);
	if (gRxVfo->MODULATION_MODE == MOD_FM) {
		BK4819_SetCompander(gCurrentVfo->CompanderMode);
	} else {
		BK4819_SetCompander(0);
	}
	
#if defined(ENABLE_DIGITAL_MODULATION)
	if (gCurrentVfo->MODULATION_MODE == MOD_DIG) {
		BK4819_PrepareDigitalTransmit(gCurrentVfo->CHANNEL_BANDWIDTH);
	} else
#endif
	{
		BK4819_PrepareTransmit();
	}

	BK4819_SelectFilter(gCurrentVfo->pTX->Frequency);
	BK4819_SetGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE);

	BK4819_SetupPowerAmplifier(gCurrentVfo->TXP_CalculatedSetting, gCurrentVfo->pTX->Frequency);

	switch (gCurrentVfo->pTX->CodeType) {
	case CODE_TYPE_CONTINUOUS_TONE:
		BK4819_SetCTCSSFrequency(CTCSS_Options[gCurrentVfo->pTX->Code]);
		break;
	case CODE_TYPE_DIGITAL:
	case CODE_TYPE_REVERSE_DIGITAL:
		BK4819_SetCDCSSCodeWord(
			DCS_GetGolayCodeWord(
				gCurrentVfo->pTX->CodeType,
				gCurrentVfo->pTX->Code
				)
			);
		break;
	default:
		BK4819_ExitSubAu();
		break;
	}
}

void RADIO_SetVfoState(VfoState_t State)
{
	if (State == VFO_STATE_NORMAL) {
		VfoState[0] = VFO_STATE_NORMAL;
		VfoState[1] = VFO_STATE_NORMAL;
#if defined(ENABLE_FMRADIO)
		gFM_ResumeCountdown = 0;
#endif
	} else {
		if (State == VFO_STATE_VOL_HIGH) {
			VfoState[0] = VFO_STATE_VOL_HIGH;
			VfoState[1] = VFO_STATE_TX_DISABLE;
		} else {
			uint8_t Channel;

			if (gEeprom.CROSS_BAND_RX_TX == CROSS_BAND_OFF) {
				Channel = gEeprom.RX_VFO;
			} else {
				Channel = gEeprom.TX_VFO;
			}
			VfoState[Channel] = State;
		}
#if defined(ENABLE_FMRADIO)
		gFM_ResumeCountdown = 5;
#endif
	}
	gUpdateDisplay = true;
}

void RADIO_PrepareTX(void)
{
	if (gEeprom.DUAL_WATCH != DUAL_WATCH_OFF) {
		gDualWatchCountdown = 360;
		gScheduleDualWatch = false;
		if (!gRxVfoIsActive) {
			gEeprom.RX_VFO = gEeprom.TX_VFO;
			gRxVfo = &gVFO.Info[gEeprom.TX_VFO];
		}
		gRxVfoIsActive = true;
	}
	RADIO_SelectCurrentVfo();

	VfoState_t State;

	if (!FREQUENCY_Check(gCurrentVfo)) {
		if (gCurrentVfo->BUSY_CHANNEL_LOCK && gCurrentFunction == FUNCTION_RECEIVE) {
			State = VFO_STATE_BUSY;
		} else if (gBatteryDisplayLevel == 1) {
			State = VFO_STATE_BAT_LOW;
		} else if (gBatteryDisplayLevel == 6) {
			State = VFO_STATE_VOL_HIGH;
		}
#if defined(ENABLE_DIGITAL_MODULATION)
		else if (gCurrentVfo->MODULATION_MODE == MOD_DIG) {
			// Allow TX in digital mode.
			State = VFO_STATE_NORMAL;
		}
#endif
		else if (gEeprom.KEY_LOCK) {
			State = VFO_STATE_DISALLOWED;
		} else {
			State = VFO_STATE_NORMAL;
		}
	} else {
		State = VFO_STATE_TX_DISABLE;
	}

	if (State != VFO_STATE_NORMAL) {
		RADIO_SetVfoState(State);
		gDTMF_ReplyState = DTMF_REPLY_NONE;
		return;
	}

	if (gDTMF_ReplyState == DTMF_REPLY_ANI) {
		if (gDTMF_CallMode == DTMF_CALL_MODE_DTMF) {
			gDTMF_IsTx = true;
			gDTMF_CallState = DTMF_CALL_STATE_NONE;
			gDTMF_TxStopCountdown = 6;
		} else {
			gDTMF_CallState = DTMF_CALL_STATE_CALL_OUT;
			gDTMF_IsTx = false;
		}
	}
	FUNCTION_Select(FUNCTION_TRANSMIT);
	gTxTimerCountdown = gEeprom.TX_TIMEOUT_TIMER * 120;
	gTxTimeoutReached = false;
	gFlagEndTransmission = false;
	gRTTECountdown = 0;
	gDTMF_ReplyState = DTMF_REPLY_NONE;
}

void RADIO_EnableCxCSS(void)
{
	switch (gCurrentVfo->pTX->CodeType) {
	case CODE_TYPE_DIGITAL:
	case CODE_TYPE_REVERSE_DIGITAL:
		BK4819_EnableCDCSS();
		break;
	case CODE_TYPE_CONTINUOUS_TONE:
		BK4819_EnableCTCSS();
		break;
	default:
		break;
	}
	SYSTEM_DelayMs(200);
}

void RADIO_PrepareCssTX(void)
{
	RADIO_PrepareTX();
	SYSTEM_DelayMs(200);
	RADIO_EnableCxCSS();
	RADIO_SetupRegisters(true);
}

void RADIO_SendEndOfTransmission(void)
{
	if (gEeprom.ROGER) {
		BK4819_PlayRoger();
	}
	if ((gCurrentVfo->DTMF_PTT_ID_TX_MODE == PTT_ID_EOT || gCurrentVfo->DTMF_PTT_ID_TX_MODE == PTT_ID_BOTH)
		&& gDTMF_CallState == DTMF_CALL_STATE_NONE)
	{
		if (gEeprom.DTMF_SIDE_TONE) {
			GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
			gEnableSpeaker = true;
			SYSTEM_DelayMs(60);
		}
		BK4819_EnterDTMF_TX(gEeprom.DTMF_SIDE_TONE);
		BK4819_PlayDTMFString(
			gEeprom.DTMF_DOWN_CODE,
			0,
			gEeprom.DTMF_FIRST_CODE_PERSIST_TIME,
			gEeprom.DTMF_HASH_CODE_PERSIST_TIME,
			gEeprom.DTMF_CODE_PERSIST_TIME,
			gEeprom.DTMF_CODE_INTERVAL_TIME
			);
		GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
		gEnableSpeaker = false;
	}
#if defined(ENABLE_MDC1200)
	if (gCurrentVfo->MDC1200_MODE == MDC1200_MODE_EOT ||
		gCurrentVfo->MDC1200_MODE == MDC1200_MODE_BOTH)
	{
		BK4819_SendMDC1200(MDC1200_OP_CODE_POST_ID, 0x00, gEeprom.MDC1200_ID, false, gCurrentVfo->CHANNEL_BANDWIDTH);
	}
#endif
	BK4819_ExitDTMF_TX(true);
}

