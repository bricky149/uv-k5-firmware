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
	pInfo->SCANLIST1_PARTICIPATION = false;
	pInfo->SCANLIST2_PARTICIPATION = false;
	pInfo->STEP_SETTING = STEP_6_25kHz;
	pInfo->StepFrequency = StepFrequencyTable[3];
	pInfo->CHANNEL_SAVE = ChannelSave;
	pInfo->FrequencyReverse = false;
	pInfo->OUTPUT_POWER = OUTPUT_POWER_LOW;
	pInfo->ConfigRX.Frequency = Frequency;
	pInfo->ConfigTX.Frequency = Frequency;
	pInfo->pRX = &pInfo->ConfigRX;
	pInfo->pTX = &pInfo->ConfigTX;
	pInfo->FREQUENCY_OF_DEVIATION = 0;
	pInfo->CompanderMode = 0;
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
	uint8_t Tmp;
	uint32_t Frequency;

	pRadio = &gEeprom.VfoInfo[VFO];

	if (!gSetting_350EN) {
		if (gEeprom.FreqChannel[VFO] == FREQ_CHANNEL_FIRST + BAND5_350MHz) {
			gEeprom.FreqChannel[VFO] = FREQ_CHANNEL_FIRST + BAND6_400MHz;
		}
		if (gEeprom.ScreenChannel[VFO] == FREQ_CHANNEL_FIRST + BAND5_350MHz) {
			gEeprom.ScreenChannel[VFO] = FREQ_CHANNEL_FIRST + BAND6_400MHz;
		}
	}

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
		RADIO_InitInfo(pRadio, Channel, Index, gLowerLimitFrequencyBandTable[Index]);
		return;
	}

	Band = Attributes & MR_CH_BAND_MASK;
	if (Band > BAND7_470MHz) {
		Band = BAND6_400MHz;
	}

	if (IS_MR_CHANNEL(Channel)) {
		gEeprom.VfoInfo[VFO].Band = Band;
		gEeprom.VfoInfo[VFO].SCANLIST1_PARTICIPATION = !!(Attributes & MR_CH_SCANLIST1);
		bParticipation2 = !!(Attributes & MR_CH_SCANLIST2);
	} else {
		Band = Channel - FREQ_CHANNEL_FIRST;
		gEeprom.VfoInfo[VFO].Band = Band;
		bParticipation2 = true;
		gEeprom.VfoInfo[VFO].SCANLIST1_PARTICIPATION = true;
	}
	gEeprom.VfoInfo[VFO].SCANLIST2_PARTICIPATION = bParticipation2;
	gEeprom.VfoInfo[VFO].CHANNEL_SAVE = Channel;

	if (IS_MR_CHANNEL(Channel)) {
		Base = Channel * 16;
	} else {
		Base = 0x0C80 + ((Channel - FREQ_CHANNEL_FIRST) * 32) + (VFO * 16);
	}

	if (Configure == VFO_CONFIGURE_RELOAD || Channel >= FREQ_CHANNEL_FIRST) {
		uint8_t Data[8];
		memset(Data, 0, sizeof(Data));
		EEPROM_ReadBuffer(Base + 8, Data, 8);

		Tmp = Data[0];
		switch (gEeprom.VfoInfo[VFO].ConfigRX.CodeType) {
		case CODE_TYPE_CONTINUOUS_TONE:
			if (Tmp >= 50) {
				Tmp = 0;
			}
			break;

		case CODE_TYPE_DIGITAL:
		case CODE_TYPE_REVERSE_DIGITAL:
			if (Tmp >= 104) {
				Tmp = 0;
			}
			break;

		default:
			gEeprom.VfoInfo[VFO].ConfigRX.CodeType = CODE_TYPE_OFF;
			Tmp = 0;
			break;
		}
		gEeprom.VfoInfo[VFO].ConfigRX.Code = Tmp;

		Tmp = Data[1];
		switch (gEeprom.VfoInfo[VFO].ConfigTX.CodeType) {
		case CODE_TYPE_CONTINUOUS_TONE:
			if (Tmp >= 50) {
				Tmp = 0;
			}
			break;

		case CODE_TYPE_DIGITAL:
		case CODE_TYPE_REVERSE_DIGITAL:
			if (Tmp >= 104) {
				Tmp = 0;
			}
			break;

		default:
			gEeprom.VfoInfo[VFO].ConfigTX.CodeType = CODE_TYPE_OFF;
			Tmp = 0;
			break;
		}
		gEeprom.VfoInfo[VFO].ConfigTX.Code = Tmp;

		gEeprom.VfoInfo[VFO].ConfigRX.CodeType = (Data[2] >> 0) & 0x0F;
		gEeprom.VfoInfo[VFO].ConfigTX.CodeType = (Data[2] >> 4) & 0x0F;

		Tmp = Data[3] & 0x0F;
		if (Tmp > 2) {
			Tmp = 0;
		}
		gEeprom.VfoInfo[VFO].FREQUENCY_DEVIATION_SETTING = Tmp;
		gEeprom.VfoInfo[VFO].AM_CHANNEL_MODE = !!(Data[3] & 0x10);

		if (Data[4] == 0xFF) {
			gEeprom.VfoInfo[VFO].FrequencyReverse = false;
			gEeprom.VfoInfo[VFO].CHANNEL_BANDWIDTH = BK4819_FILTER_BW_WIDE;
			gEeprom.VfoInfo[VFO].OUTPUT_POWER = OUTPUT_POWER_LOW;
			gEeprom.VfoInfo[VFO].BUSY_CHANNEL_LOCK = 0;
		} else {
			gEeprom.VfoInfo[VFO].FrequencyReverse = !!(Data[4] & 0x01);
			gEeprom.VfoInfo[VFO].CHANNEL_BANDWIDTH = !!(Data[4] & 0x02);
			gEeprom.VfoInfo[VFO].OUTPUT_POWER = (Data[4] >> 2) & 0x03;
			gEeprom.VfoInfo[VFO].BUSY_CHANNEL_LOCK = !!(Data[4] & 0x10);
		}

		if (Data[5] == 0xFF) {
			gEeprom.VfoInfo[VFO].DTMF_DECODING_ENABLE = 0;
			gEeprom.VfoInfo[VFO].DTMF_PTT_ID_TX_MODE = PTT_ID_OFF;
		} else {
			gEeprom.VfoInfo[VFO].DTMF_DECODING_ENABLE = !!(Data[5] & 1);
			gEeprom.VfoInfo[VFO].DTMF_PTT_ID_TX_MODE = (Data[5] >> 0x01) & 0x03;
		}

		Tmp = Data[6];
		if (Tmp > STEP_8_33kHz) {
			Tmp = STEP_25_0kHz;
		}
		gEeprom.VfoInfo[VFO].STEP_SETTING = Tmp;
		gEeprom.VfoInfo[VFO].StepFrequency = StepFrequencyTable[Tmp];

		Tmp = Data[7];
		if (Tmp > 3) {
			Tmp = 0;
		}
		gEeprom.VfoInfo[VFO].CompanderMode = Tmp;

		struct {
			uint32_t Frequency;
			uint32_t Offset;
		} Info;
		EEPROM_ReadBuffer(Base, &Info, sizeof(Info));

		pRadio->ConfigRX.Frequency = Info.Frequency;
		if (Info.Offset >= 100000000) {
			Info.Offset = 1000000;
		}
		gEeprom.VfoInfo[VFO].FREQUENCY_OF_DEVIATION = Info.Offset;
	}

	Frequency = pRadio->ConfigRX.Frequency;
	if (Frequency < gLowerLimitFrequencyBandTable[Band]) {
		pRadio->ConfigRX.Frequency = gLowerLimitFrequencyBandTable[Band];
	} else if (Frequency > gUpperLimitFrequencyBandTable[Band]) {
		pRadio->ConfigRX.Frequency = gUpperLimitFrequencyBandTable[Band];
	} else if (Channel >= FREQ_CHANNEL_FIRST) {
		pRadio->ConfigRX.Frequency = FREQUENCY_FloorToStep(pRadio->ConfigRX.Frequency, gEeprom.VfoInfo[VFO].StepFrequency, gLowerLimitFrequencyBandTable[Band]);
	}

	if (Frequency >= 10800000 && Frequency <= 13599990) {
		gEeprom.VfoInfo[VFO].FREQUENCY_DEVIATION_SETTING = FREQUENCY_DEVIATION_OFF;
	} else if (!IS_MR_CHANNEL(Channel)) {
		Frequency = FREQUENCY_FloorToStep(gEeprom.VfoInfo[VFO].FREQUENCY_OF_DEVIATION, gEeprom.VfoInfo[VFO].StepFrequency, 0);
		gEeprom.VfoInfo[VFO].FREQUENCY_OF_DEVIATION = Frequency;
	}
	RADIO_ApplyOffset(pRadio);
	if (IS_MR_CHANNEL(Channel)) {
		memset(gEeprom.VfoInfo[VFO].Name, 0, sizeof(gEeprom.VfoInfo[VFO].Name));
		EEPROM_ReadBuffer(0x0F50 + (Channel * 0x10), gEeprom.VfoInfo[VFO].Name, 16);
		// 16 bytes allocated but only 12 used
		//EEPROM_ReadBuffer(0x0F50 + (Channel * 0x10), gEeprom.VfoInfo[VFO].Name + 0, 8);
		//EEPROM_ReadBuffer(0x0F58 + (Channel * 0x10), gEeprom.VfoInfo[VFO].Name + 8, 8);
	}

	if (!gEeprom.VfoInfo[VFO].FrequencyReverse) {
		gEeprom.VfoInfo[VFO].pRX = &gEeprom.VfoInfo[VFO].ConfigRX;
		gEeprom.VfoInfo[VFO].pTX = &gEeprom.VfoInfo[VFO].ConfigTX;
	} else {
		gEeprom.VfoInfo[VFO].pRX = &gEeprom.VfoInfo[VFO].ConfigTX;
		gEeprom.VfoInfo[VFO].pTX = &gEeprom.VfoInfo[VFO].ConfigRX;
	}

	if (!gSetting_350EN) {
		FREQ_Config_t *pConfig = gEeprom.VfoInfo[VFO].pRX;
		if (pConfig->Frequency >= 35000000 && pConfig->Frequency <= 39999990) {
			pConfig->Frequency = 41001250;
		}
	}

	if (gEeprom.VfoInfo[VFO].Band == BAND2_108MHz) {
		gEeprom.VfoInfo[VFO].AM_CHANNEL_MODE = 1;
		gEeprom.VfoInfo[VFO].IsAM = true;
		gEeprom.VfoInfo[VFO].DTMF_DECODING_ENABLE = false;
		gEeprom.VfoInfo[VFO].ConfigRX.CodeType = CODE_TYPE_OFF;
		gEeprom.VfoInfo[VFO].ConfigTX.CodeType = CODE_TYPE_OFF;
		gEeprom.VfoInfo[VFO].CompanderMode = 0;
	} else {
		// Override AM mode when entering/leaving Band2
		gEeprom.VfoInfo[VFO].AM_CHANNEL_MODE = 0;
		gEeprom.VfoInfo[VFO].IsAM = false;
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
	// make low even lower
	if (pInfo->OUTPUT_POWER == OUTPUT_POWER_LOW) {
		Txp[0] /= 6;
		Txp[1] /= 6;
		Txp[2] /= 6;
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
	case FREQUENCY_DEVIATION_OFF:
		break;
	case FREQUENCY_DEVIATION_ADD:
		Frequency += pInfo->FREQUENCY_OF_DEVIATION;
		break;
	case FREQUENCY_DEVIATION_SUB:
		Frequency -= pInfo->FREQUENCY_OF_DEVIATION;
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
		gCurrentVfo = &gEeprom.VfoInfo[gEeprom.TX_VFO];
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

	gTxVfo = &gEeprom.VfoInfo[gEeprom.TX_VFO];
	if (gEeprom.CROSS_BAND_RX_TX == CROSS_BAND_OFF) {
		gEeprom.RX_VFO = gEeprom.TX_VFO;
	} else {
		if (gEeprom.TX_VFO == 0) {
			gEeprom.RX_VFO = 1;
		} else {
			gEeprom.RX_VFO = 0;
		}
	}

	gRxVfo = &gEeprom.VfoInfo[gEeprom.RX_VFO];
	RADIO_SelectCurrentVfo();
}

void RADIO_SetupRegisters(bool bSwitchToFunction0)
{
	uint16_t Status;
	uint16_t InterruptMask;
	uint32_t Frequency;

	GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
	gEnableSpeaker = false;
	BK4819_ClearGpioOut(BK4819_GPIO6_PIN2_GREEN);

	BK4819_FilterBandwidth_t Bandwidth = gRxVfo->CHANNEL_BANDWIDTH;
	BK4819_SetFilterBandwidth(Bandwidth);

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
	BK4819_WriteRegister(BK4819_REG_7D, gEeprom.MIC_SENSITIVITY_TUNING | 0xE940);
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

	if (!gRxVfo->IsAM) {
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

	if (gRxVfo->IsAM || !gRxVfo->DTMF_DECODING_ENABLE) {
		BK4819_DisableDTMF();
	} else {
		BK4819_EnableDTMF();
		InterruptMask |= BK4819_REG_3F_DTMF_5TONE_FOUND;
	}
	BK4819_WriteRegister(BK4819_REG_3F, InterruptMask);

	FUNCTION_Init();

	if (bSwitchToFunction0) {
		FUNCTION_Select(FUNCTION_FOREGROUND);
	}
}

void RADIO_SetTxParameters(void)
{
	GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
	gEnableSpeaker = false;
	BK4819_ClearGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE);

	BK4819_FilterBandwidth_t Bandwidth = gCurrentVfo->CHANNEL_BANDWIDTH;
	BK4819_SetFilterBandwidth(Bandwidth);

	BK4819_SetFrequency(gCurrentVfo->pTX->Frequency);
	BK4819_PrepareTransmit();

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
			gRxVfo = &gEeprom.VfoInfo[gEeprom.TX_VFO];
		}
		gRxVfoIsActive = true;
	}
	RADIO_SelectCurrentVfo();

	VfoState_t State;

	if (!FREQUENCY_Check(gCurrentVfo)) {
		if (gCurrentVfo->BUSY_CHANNEL_LOCK && gCurrentFunction == FUNCTION_RECEIVE) {
			State = VFO_STATE_BUSY;
		} else if (gBatteryDisplayLevel == 0) {
			State = VFO_STATE_BAT_LOW;
		} else if (gBatteryDisplayLevel == 6) {
			State = VFO_STATE_VOL_HIGH;
		} else if (gEeprom.KEY_LOCK) {
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
	default:
		BK4819_EnableCTCSS();
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
	if (gEeprom.ROGER == ROGER_MODE_ROGER) {
		BK4819_PlayRoger();
	} else if (gEeprom.ROGER == ROGER_MODE_MDC) {
		BK4819_PlayRogerMDC();
	}
	if (gDTMF_CallState == DTMF_CALL_STATE_NONE && (gCurrentVfo->DTMF_PTT_ID_TX_MODE == PTT_ID_EOT || gCurrentVfo->DTMF_PTT_ID_TX_MODE == PTT_ID_BOTH)) {
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
	BK4819_ExitDTMF_TX(true);
}

