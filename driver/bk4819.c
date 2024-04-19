/* Copyright 2023 Dual Tachyon
 * Copyright 2023 fagci
 * Copyright 2023 OneOfEleven
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

#include "bsp/dp32g030/gpio.h"
#include "bsp/dp32g030/portcon.h"
#include "driver/bk4819.h"
#include "driver/gpio.h"
#include "driver/system.h"
#include "driver/systick.h"

#if defined(ENABLE_MDC1200)
#include "mdc1200.h"

// 1o11
__inline static uint16_t ScaleFreq(const uint16_t freq)
{	// with rounding
	return (((uint32_t)freq * 338311u) + (1u << 14)) >> 15; // max freq = 12695
}
#endif

static const uint8_t RSSI_CEILING = 143; // S9

static uint16_t gBK4819_GpioOutState;
bool gRxIdleMode;

void BK4819_Init(void)
{
	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);

	BK4819_WriteRegister(BK4819_REG_00, 0x8000);
	BK4819_WriteRegister(BK4819_REG_00, 0);
	BK4819_WriteRegister(BK4819_REG_37, 0x1D0F);
	BK4819_WriteRegister(BK4819_REG_36, 0x0022);
	BK4819_WriteRegister(BK4819_REG_19, 0x1041);
	BK4819_WriteRegister(BK4819_REG_7D, 0xE940);
	BK4819_WriteRegister(BK4819_REG_48, 0xB3A8);
	BK4819_WriteRegister(BK4819_REG_09, 0x006F);
	BK4819_WriteRegister(BK4819_REG_09, 0x106B);
	BK4819_WriteRegister(BK4819_REG_09, 0x2067);
	BK4819_WriteRegister(BK4819_REG_09, 0x3062);
	BK4819_WriteRegister(BK4819_REG_09, 0x4050);
	BK4819_WriteRegister(BK4819_REG_09, 0x5047);
	BK4819_WriteRegister(BK4819_REG_09, 0x603A);
	BK4819_WriteRegister(BK4819_REG_09, 0x702C);
	BK4819_WriteRegister(BK4819_REG_09, 0x8041);
	BK4819_WriteRegister(BK4819_REG_09, 0x9037);
	BK4819_WriteRegister(BK4819_REG_09, 0xA025);
	BK4819_WriteRegister(BK4819_REG_09, 0xB017);
	BK4819_WriteRegister(BK4819_REG_09, 0xC0E4);
	BK4819_WriteRegister(BK4819_REG_09, 0xD0CB);
	BK4819_WriteRegister(BK4819_REG_09, 0xE0B5);
	BK4819_WriteRegister(BK4819_REG_09, 0xF09F);
	BK4819_WriteRegister(BK4819_REG_1F, 0x5454);
	BK4819_WriteRegister(BK4819_REG_3E, 0xA037);

	gBK4819_GpioOutState = 0x9000;
	BK4819_WriteRegister(BK4819_REG_33, gBK4819_GpioOutState);
	BK4819_WriteRegister(BK4819_REG_3F, 0);
}

uint16_t BK4819_ReadRegister(BK4819_REGISTER_t Register)
{
	uint16_t Value;

	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
	GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
	GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);

	BK4819_WriteU8(Register | 0x80);

	//Value = BK4819_ReadU16();
	PORTCON_PORTC_IE = (PORTCON_PORTC_IE & ~PORTCON_PORTC_IE_C2_MASK) | PORTCON_PORTC_IE_C2_BITS_ENABLE;
	GPIOC->DIR = (GPIOC->DIR & ~GPIO_DIR_2_MASK) | GPIO_DIR_2_BITS_INPUT;
	Value = 0;
	for (int i = 0; i < 16; i++) {
		Value <<= 1;
		Value |= GPIO_CheckBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);

		GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
		GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
	}
	PORTCON_PORTC_IE = (PORTCON_PORTC_IE & ~PORTCON_PORTC_IE_C2_MASK) | PORTCON_PORTC_IE_C2_BITS_DISABLE;
	GPIOC->DIR = (GPIOC->DIR & ~GPIO_DIR_2_MASK) | GPIO_DIR_2_BITS_OUTPUT;

	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);

	return Value;
}

void BK4819_WriteRegister(BK4819_REGISTER_t Register, uint16_t Data)
{
	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
	GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
	GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);

	BK4819_WriteU8(Register);

	//BK4819_WriteU16(Data);
	for (int i = 0; i < 16; i++) {
		if ((Data & 0x8000U) == 0) {
			GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);
		} else {
			GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);
		}
		GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
		Data <<= 1;
		GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
	}

	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);
}

void BK4819_WriteU8(uint8_t Data)
{
	GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);

	for (int i = 0; i < 8; i++) {
		if ((Data & 0x80U) == 0) {
			GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);
		} else {
			GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);
		}
		GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
		Data <<= 1;
		GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
	}
}

void BK4819_AMFix_40ms(void) {
	// Read current AGC fix index so we don't suddenly peak
	uint16_t AgcFixIndex = (BK4819_ReadRegister(BK4819_REG_7E) >> 12) & 3;
	// Take the difference between current and desired RSSI readings
	const uint16_t RSSI = BK4819_GetRSSI();
	const int8_t Diff_dB = RSSI - RSSI_CEILING;

	if (Diff_dB > 0 && AgcFixIndex != 4) {
		// Over distortion threshold, reduce gain
		AgcFixIndex = (AgcFixIndex + 7) % 8; // Decrement AGC fix index
	} else {
		// No gain adjustment needed
		return;
	}
	// Write new fix index to the AGC register
	BK4819_WriteRegister(BK4819_REG_7E, // 1o11
		(AgcFixIndex << 12));           // 3 AGC fix index
}

void BK4819_AMFix_500ms(void) {
	// Read current AGC fix index so we don't suddenly peak
	uint16_t AgcFixIndex = (BK4819_ReadRegister(BK4819_REG_7E) >> 12) & 3;
	// Take the difference between current and desired RSSI readings
	const uint16_t RSSI = BK4819_GetRSSI();
	const int8_t Diff_dB = RSSI - RSSI_CEILING;
	// Gaps between gain values are ~17dB
	if (Diff_dB < -17 && AgcFixIndex != 3) {
		// Attempt to reopen squelch by increasing gain
		// This helps prevent hysteresis as we're not eagerly increasing
		// gain based on an arbitrary floor RSSI value
		AgcFixIndex = (AgcFixIndex + 1) % 8; // Increment AGC fix index
	} else {
		// No gain adjustment needed
		return;
	}
	// Write new fix index to the AGC register
	BK4819_WriteRegister(BK4819_REG_7E, // 1o11
		(AgcFixIndex << 12));           // 3 AGC fix index
}

void BK4819_SetAGC(void)
{
	BK4819_WriteRegister(BK4819_REG_7E, // 1o11
		(0u << 15) |                    // 0 AGC fix mode
		(3u << 12) |                    // 3 AGC fix index
		(5u <<  3) |                    // 5 DC filter bandwidth for Tx
		(6u <<  0));                    // 6 DC filter bandwidth for Rx

	// AGC fix indexes
	BK4819_WriteRegister(BK4819_REG_13, 0x03BE); // 3
	BK4819_WriteRegister(BK4819_REG_12, 0x037B); // 2
	BK4819_WriteRegister(BK4819_REG_11, 0x027B); // 1
	BK4819_WriteRegister(BK4819_REG_10, 0x007A); // 0
	BK4819_WriteRegister(BK4819_REG_14, 0x0019); // -1

	// Affects when to adjust AGC fix index
	BK4819_WriteRegister(BK4819_REG_49, 0x2A38); // 00 1010100 0111000
	// if (ModType != MOD_AM) {
	// 	BK4819_WriteRegister(BK4819_REG_49,
	// 		(0u  << 14) |                   // 0  High/Low Lo selection
	// 		(84u <<  7) |                   // 84 RF AGC high threshold (sensitivity vs. distortion)
	// 		(56u <<  0));                   // 56 RF AGC low threshold
	// } else {
	// 	BK4819_WriteRegister(BK4819_REG_49,
	// 		(0u  << 14) |                   // 0  High/Low Lo selection
	// 		(52u <<  7) |                   // 84 RF AGC high threshold (sensitivity vs. distortion)
	// 		(34u <<  0));                   // 56 RF AGC low threshold
	// }

	BK4819_WriteRegister(BK4819_REG_7B, 0x8420);
}

void BK4819_SetFGC(void)
{
	// AGC increases gain on strong signals which would explain the distortion
	// when a signal saturates the demodulator. OneOfEleven's 'AM fix' inverts
	// this, toning down gain registers when a signal exceeds -89dBm
	// (~33dB above sensitivity).
	BK4819_WriteRegister(BK4819_REG_7E, // 1o11
		(1u << 15) |                    // 0 AGC fix mode
		(3u << 12) |                    // 3 AGC fix index
		(5u <<  3) |                    // 5 DC filter bandwidth for Tx
		(6u <<  0));                    // 6 DC filter bandwidth for Rx

	// AGC fix indexes
	BK4819_WriteRegister(BK4819_REG_13, 0x03BE); // 3
	BK4819_WriteRegister(BK4819_REG_12, 0x037C); // 2
	BK4819_WriteRegister(BK4819_REG_11, 0x027B); // 1
	BK4819_WriteRegister(BK4819_REG_10, 0x017A); // 0
	BK4819_WriteRegister(BK4819_REG_14, 0x0059); // -1

	BK4819_WriteRegister(BK4819_REG_49, 0x2A38);
	BK4819_WriteRegister(BK4819_REG_7B, 0x318C);
	BK4819_WriteRegister(BK4819_REG_7C, 0x595E);
	BK4819_WriteRegister(BK4819_REG_20, 0x8DEF);

	// fagci
	for (int i = 0; i < 8; i++) {
		BK4819_WriteRegister(BK4819_REG_06, (i & 7) << 13 | 0x4A << 7 | 0x36);
	}
	//for (i = 0; i < 8; i++) {
	//	// Bug? The bit 0x2000 below overwrites the (i << 13)
	//	BK4819_WriteRegister(BK4819_REG_06, ((i << 13) | 0x2500U) + 0x36U);
	//}
}

void BK4819_SetGpioOut(BK4819_GPIO_PIN_t Pin)
{
	gBK4819_GpioOutState |= (0x40U >> Pin);

	BK4819_WriteRegister(BK4819_REG_33, gBK4819_GpioOutState);
}

void BK4819_ClearGpioOut(BK4819_GPIO_PIN_t Pin)
{
	gBK4819_GpioOutState &= ~(0x40U >> Pin);

	BK4819_WriteRegister(BK4819_REG_33, gBK4819_GpioOutState);
}

void BK4819_SetCDCSSCodeWord(uint32_t CodeWord)
{
	// Enable CDCSS
	// Transmit positive CDCSS code
	// CDCSS Mode
	// CDCSS 23bit
	// Enable Auto CDCSS Bw Mode
	// Enable Auto CTCSS Bw Mode
	// CTCSS/CDCSS Tx Gain1 Tuning = 51
	BK4819_WriteRegister(BK4819_REG_51, 0
			| BK4819_REG_51_ENABLE_CxCSS
			| BK4819_REG_51_GPIO6_PIN2_NORMAL
			| BK4819_REG_51_TX_CDCSS_POSITIVE
			| BK4819_REG_51_MODE_CDCSS
			| BK4819_REG_51_CDCSS_23_BIT
			| BK4819_REG_51_1050HZ_NO_DETECTION
			| BK4819_REG_51_AUTO_CDCSS_BW_ENABLE
			| BK4819_REG_51_AUTO_CTCSS_BW_ENABLE
			| (51U << BK4819_REG_51_SHIFT_CxCSS_TX_GAIN1));

	// CTC1 Frequency Control Word = 2775
	BK4819_WriteRegister(BK4819_REG_07, 0
			| BK4819_REG_07_MODE_CTC1
			| (2775U << BK4819_REG_07_SHIFT_FREQUENCY));

	// Set the code word
	BK4819_WriteRegister(BK4819_REG_08, 0 | ((CodeWord >>  0) & 0xFFF));
	BK4819_WriteRegister(BK4819_REG_08, 0x8000 | ((CodeWord >> 12) & 0xFFF));
}

void BK4819_SetCTCSSFrequency(uint32_t FreqControlWord)
{
	uint16_t Config;

	if (FreqControlWord == 2625) { // Enables 1050Hz detection mode
		// Enable TxCTCSS
		// CTCSS Mode
		// 1050/4 Detect Enable
		// Enable Auto CDCSS Bw Mode
		// Enable Auto CTCSS Bw Mode
		// CTCSS/CDCSS Tx Gain1 Tuning = 74
		Config = 0x944A;
	} else {
		// Enable TxCTCSS
		// CTCSS Mode
		// Enable Auto CDCSS Bw Mode
		// Enable Auto CTCSS Bw Mode
		// CTCSS/CDCSS Tx Gain1 Tuning = 74
		Config = 0x904A;
	}
	BK4819_WriteRegister(BK4819_REG_51, Config);
	// CTC1 Frequency Control Word
	BK4819_WriteRegister(BK4819_REG_07, 0
			| BK4819_REG_07_MODE_CTC1
			| ((FreqControlWord * 2065) / 1000) << BK4819_REG_07_SHIFT_FREQUENCY);
}

void BK4819_Set55HzTailDetection(void)
{
	// CTC2 Frequency Control Word = round_nearest(25391 / 55) = 462
	BK4819_WriteRegister(BK4819_REG_07, (1U << 13) | 462);
}

void BK4819_SetFilterBandwidth(BK4819_FilterBandwidth_t Bandwidth)
{
	switch (Bandwidth) {
	case BK4819_FILTER_BW_WIDE:
		BK4819_WriteRegister(BK4819_REG_43, 0x3028);
		break;
	case BK4819_FILTER_BW_NARROW:
		BK4819_WriteRegister(BK4819_REG_43, 0x4048);
		break;
	case BK4819_FILTER_BW_NARROWER:
		BK4819_WriteRegister(BK4819_REG_43, 0x2058);
		break;
	}
}

void BK4819_SetupPowerAmplifier(uint8_t Bias, uint32_t Frequency)
{
	uint8_t Gain;

	if (Frequency < 28000000) {
		// Gain 1 = 1
		// Gain 2 = 0
		Gain = 0x08U;
	} else {
		// Gain 1 = 4
		// Gain 2 = 2
		Gain = 0x22U;
	}
	// Enable PACTL output
	BK4819_WriteRegister(BK4819_REG_36, (Bias << 8) | 0x80U | Gain);
}

void BK4819_SetFrequency(uint32_t Frequency)
{
	BK4819_WriteRegister(BK4819_REG_38, (Frequency >>  0) & 0xFFFF);
	BK4819_WriteRegister(BK4819_REG_39, (Frequency >> 16) & 0xFFFF);
}

void BK4819_SetupSquelch(uint8_t SquelchOpenRSSIThresh, uint8_t SquelchCloseRSSIThresh, uint8_t SquelchOpenNoiseThresh, uint8_t SquelchCloseNoiseThresh, uint8_t SquelchCloseGlitchThresh, uint8_t SquelchOpenGlitchThresh)
{
	BK4819_WriteRegister(BK4819_REG_70, 0);
	BK4819_WriteRegister(BK4819_REG_4D, 0xA000 | SquelchCloseGlitchThresh);
	// 0x6f = 0110 1111 meaning the default sql delays from the datasheet are used (101 and 111)
	//BK4819_WriteRegister(BK4819_REG_4E, 0x6F00 | SquelchOpenGlitchThresh);
	BK4819_WriteRegister(BK4819_REG_4E,      // 1o11
			(1u << 14) |                     // 1 ???
			(4u << 11) |                     // 5 squelch = open delay .. 0 ~ 7
			(2u <<  9) |                     // 3 squelch = close delay .. 0 ~ 3
			(SquelchOpenGlitchThresh << 0)); // 0 ~ 255
	BK4819_WriteRegister(BK4819_REG_4F, (SquelchCloseNoiseThresh << 8) | SquelchOpenNoiseThresh);
	BK4819_WriteRegister(BK4819_REG_78, (SquelchOpenRSSIThresh << 8) | SquelchCloseRSSIThresh);
	BK4819_SetAF(BK4819_AF_MUTE);
	BK4819_RX_TurnOn();
}

void BK4819_SetModulation(BK4819_MOD_Type_t ModType) {
	switch(ModType) {
		case MOD_FM:
			BK4819_SetAF(BK4819_AF_OPEN);
			BK4819_WriteRegister(BK4819_REG_3D, 0);
			break;
		case MOD_AM:
			BK4819_SetAF(BK4819_AF_AM);
			BK4819_WriteRegister(BK4819_REG_3D, 0);
			break;
		case MOD_LSB:
			BK4819_SetAF(BK4819_AF_LSB);
			BK4819_WriteRegister(BK4819_REG_3D, 0x2B45);
			break;
		case MOD_USB:
			BK4819_SetAF(BK4819_AF_USB);
			BK4819_WriteRegister(BK4819_REG_3D, 0x2B45);
			break;
	}
}

void BK4819_SetAF(BK4819_AF_Type_t AF)
{
	// AF Output Inverse Mode = Inverse
	// Undocumented bits 0x2040
	BK4819_WriteRegister(BK4819_REG_47, 0x6040 | (AF << 8));
}

void BK4819_RX_TurnOn(void)
{
	// DSP Voltage Setting = 1
	// ANA LDO = 2.7v
	// VCO LDO = 2.7v
	// RF LDO = 2.7v
	// PLL LDO = 2.7v
	// ANA LDO bypass
	// VCO LDO bypass
	// RF LDO bypass
	// PLL LDO bypass
	// Reserved bit is 1 instead of 0
	// Enable DSP
	// Enable XTAL
	// Enable Band Gap
	BK4819_WriteRegister(BK4819_REG_37, 0x1F0F);

	// DSP Voltage Setting = 1
	// ANA LDO = 2.4v
	// VCO LDO = 2.7v
	// RF LDO = 2.7v
	// PLL LDO = 2.4v
	// ANA LDO enable
	// VCO LDO enable
	// RF LDO enable
	// PLL LDO enable
	// Reserved bit is 1 instead of 0
	// Enable DSP
	// Enable XTAL
	// Enable Band Gap
	//BK4819_WriteRegister(BK4819_REG_37, 0x160F);

	// Turn off everything
	BK4819_WriteRegister(BK4819_REG_30, 0);

	// Enable VCO Calibration
	// Enable RX Link
	// Enable AF DAC
	// Enable PLL/VCO
	// Disable PA Gain
	// Disable MIC ADC
	// Disable TX DSP
	// Enable RX DSP
	BK4819_WriteRegister(BK4819_REG_30, 0xBFF1);
}

void BK4819_SelectFilter(uint32_t Frequency)
{
	if (Frequency < 28000000) {
		BK4819_SetGpioOut(BK4819_GPIO4_PIN32_VHF_LNA);
		BK4819_ClearGpioOut(BK4819_GPIO3_PIN31_UHF_LNA);
	} else if (Frequency == 0xFFFFFFFF) {
		BK4819_ClearGpioOut(BK4819_GPIO4_PIN32_VHF_LNA);
		BK4819_ClearGpioOut(BK4819_GPIO3_PIN31_UHF_LNA);
	} else {
		BK4819_ClearGpioOut(BK4819_GPIO4_PIN32_VHF_LNA);
		BK4819_SetGpioOut(BK4819_GPIO3_PIN31_UHF_LNA);
	}
}

void BK4819_SetCompander(uint8_t Mode)
{
	// 1o11
	if (Mode == 1 || Mode == 3) {
		// REG_29
		//
		// <15:14> 10 Compress (AF Tx) Ratio
		//         00 = Disable
		//         01 = 1.333:1
		//         10 = 2:1
		//         11 = 4:1
		//
		// <13:7>  86 Compress (AF Tx) 0 dB point (dB)
		//
		// <6:0>   64 Compress (AF Tx) noise point (dB)
		//
		BK4819_WriteRegister(BK4819_REG_29,
			(2u << 14) |  // compress ratio 2:1
			(86u <<  7) | // compress 0dB
			(64u <<  0)); // compress noise dB
	}
	if (Mode == 2 || Mode == 3) {
		// REG_28
		//
		// <15:14> 01 Expander (AF Rx) Ratio
		//         00 = Disable
		//         01 = 1:2
		//         10 = 1:3
		//         11 = 1:4
		//
		// <13:7>  86 Expander (AF Rx) 0 dB point (dB)
		//
		// <6:0>   56 Expander (AF Rx) noise point (dB)
		//
		BK4819_WriteRegister(BK4819_REG_28,
			(1u << 14) |  // expander ratio 1:2
			(86u <<  7) | // expander 0dB
			(56u <<  0)); // expander noise dB
	}
	// mode 0 .. OFF
	// mode 1 .. TX
	// mode 2 .. RX
	// mode 3 .. TX and RX
	//
	if (Mode == 0) {
		BK4819_WriteRegister(BK4819_REG_31, (0u << 3));
	} else {
		BK4819_WriteRegister(BK4819_REG_31, (1u << 3));
	}
}

void BK4819_DisableDTMF(void)
{
	BK4819_WriteRegister(BK4819_REG_24, 0);
}

void BK4819_EnableDTMF(void)
{
	BK4819_WriteRegister(BK4819_REG_21, 0x06D8);
	BK4819_WriteRegister(BK4819_REG_24, 0
		| (1U << BK4819_REG_24_SHIFT_UNKNOWN_15)
		| (24 << BK4819_REG_24_SHIFT_THRESHOLD)
		| (1U << BK4819_REG_24_SHIFT_UNKNOWN_6)
		| BK4819_REG_24_ENABLE
		| BK4819_REG_24_SELECT_DTMF
		| (14U << BK4819_REG_24_SHIFT_MAX_SYMBOLS)
		);
}

void BK4819_EnterTxMute(void)
{
	BK4819_WriteRegister(BK4819_REG_50, 0xBB20);
}

void BK4819_ExitTxMute(void)
{
	BK4819_WriteRegister(BK4819_REG_50, 0x3B20);
}

void BK4819_Sleep(void)
{
	BK4819_WriteRegister(BK4819_REG_30, 0);
	BK4819_WriteRegister(BK4819_REG_37, 0x1D00);
}

void BK4819_PrepareTransmit(void)
{
	//BK4819_ExitBypass();
	BK4819_SetAF(BK4819_AF_MUTE);
	BK4819_WriteRegister(BK4819_REG_7E, 0x302E);

	BK4819_ExitTxMute();

	//BK4819_TxOn_Beep();
	BK4819_WriteRegister(BK4819_REG_37, 0x1D0F);
	BK4819_WriteRegister(BK4819_REG_52, 0x028F);
	BK4819_WriteRegister(BK4819_REG_30, 0);
	BK4819_WriteRegister(BK4819_REG_30, 0xC1FE);
}

void BK4819_ExitSubAu(void)
{
	BK4819_WriteRegister(BK4819_REG_51, 0);
}

void BK4819_EnableRX(void)
{
	if (gRxIdleMode) {
		BK4819_SetGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE);
		BK4819_RX_TurnOn();
	}
}

void BK4819_EnterDTMF_TX(bool bLocalLoopback)
{
	BK4819_EnableDTMF();
	BK4819_EnterTxMute();
	if (bLocalLoopback) {
		BK4819_SetAF(BK4819_AF_BEEP);
	} else {
		BK4819_SetAF(BK4819_AF_MUTE);
	}
	BK4819_WriteRegister(BK4819_REG_70, 0
		| BK4819_REG_70_MASK_ENABLE_TONE1
		| (83 << BK4819_REG_70_SHIFT_TONE1_TUNING_GAIN)
		| BK4819_REG_70_MASK_ENABLE_TONE2
		| (83 << BK4819_REG_70_SHIFT_TONE2_TUNING_GAIN)
		);

	BK4819_EnableTXLink();
}

void BK4819_ExitDTMF_TX(bool bKeep)
{
	BK4819_EnterTxMute();
	BK4819_SetAF(BK4819_AF_MUTE);
	BK4819_WriteRegister(BK4819_REG_70, 0);
	BK4819_DisableDTMF();
	BK4819_WriteRegister(BK4819_REG_30, 0xC1FE);
	if (!bKeep) {
		BK4819_ExitTxMute();
	}
}

void BK4819_EnableTXLink(void)
{
	BK4819_WriteRegister(BK4819_REG_30, 0
		| BK4819_REG_30_ENABLE_VCO_CALIB
		| BK4819_REG_30_ENABLE_UNKNOWN
		| BK4819_REG_30_DISABLE_RX_LINK
		| BK4819_REG_30_ENABLE_AF_DAC
		| BK4819_REG_30_ENABLE_DISC_MODE
		| BK4819_REG_30_ENABLE_PLL_VCO
		| BK4819_REG_30_ENABLE_PA_GAIN
		| BK4819_REG_30_DISABLE_MIC_ADC
		| BK4819_REG_30_ENABLE_TX_DSP
		| BK4819_REG_30_DISABLE_RX_DSP
		);
}

void BK4819_PlayDTMF(char Code)
{
	switch (Code) {
	case '0':
		BK4819_WriteRegister(BK4819_REG_71, 0x25F3);
		BK4819_WriteRegister(BK4819_REG_72, 0x35E1);
		break;
	case '1':
		BK4819_WriteRegister(BK4819_REG_71, 0x1C1C);
		BK4819_WriteRegister(BK4819_REG_72, 0x30C2);
		break;
	case '2':
		BK4819_WriteRegister(BK4819_REG_71, 0x1C1C);
		BK4819_WriteRegister(BK4819_REG_72, 0x35E1);
		break;
	case '3':
		BK4819_WriteRegister(BK4819_REG_71, 0x1C1C);
		BK4819_WriteRegister(BK4819_REG_72, 0x3B91);
		break;
	case '4':
		BK4819_WriteRegister(BK4819_REG_71, 0x1F0E);
		BK4819_WriteRegister(BK4819_REG_72, 0x30C2);
		break;
	case '5':
		BK4819_WriteRegister(BK4819_REG_71, 0x1F0E);
		BK4819_WriteRegister(BK4819_REG_72, 0x35E1);
		break;
	case '6':
		BK4819_WriteRegister(BK4819_REG_71, 0x1F0E);
		BK4819_WriteRegister(BK4819_REG_72, 0x3B91);
		break;
	case '7':
		BK4819_WriteRegister(BK4819_REG_71, 0x225C);
		BK4819_WriteRegister(BK4819_REG_72, 0x30C2);
		break;
	case '8':
		BK4819_WriteRegister(BK4819_REG_71, 0x225c);
		BK4819_WriteRegister(BK4819_REG_72, 0x35E1);
		break;
	case '9':
		BK4819_WriteRegister(BK4819_REG_71, 0x225C);
		BK4819_WriteRegister(BK4819_REG_72, 0x3B91);
		break;
	case 'A':
		BK4819_WriteRegister(BK4819_REG_71, 0x1C1C);
		BK4819_WriteRegister(BK4819_REG_72, 0x41DC);
		break;
	case 'B':
		BK4819_WriteRegister(BK4819_REG_71, 0x1F0E);
		BK4819_WriteRegister(BK4819_REG_72, 0x41DC);
		break;
	case 'C':
		BK4819_WriteRegister(BK4819_REG_71, 0x225C);
		BK4819_WriteRegister(BK4819_REG_72, 0x41DC);
		break;
	case 'D':
		BK4819_WriteRegister(BK4819_REG_71, 0x25F3);
		BK4819_WriteRegister(BK4819_REG_72, 0x41DC);
		break;
	case '*':
		BK4819_WriteRegister(BK4819_REG_71, 0x25F3);
		BK4819_WriteRegister(BK4819_REG_72, 0x30C2);
		break;
	case '#':
		BK4819_WriteRegister(BK4819_REG_71, 0x25F3);
		BK4819_WriteRegister(BK4819_REG_72, 0x3B91);
		break;
	}
}

void BK4819_PlayDTMFString(const char *pString, bool bDelayFirst, uint16_t FirstCodePersistTime, uint16_t HashCodePersistTime, uint16_t CodePersistTime, uint16_t CodeInternalTime)
{
	uint8_t i;
	uint16_t Delay;

	for (i = 0; pString[i]; i++) {
		BK4819_PlayDTMF(pString[i]);
		BK4819_ExitTxMute();
		if (bDelayFirst && i == 0) {
			Delay = FirstCodePersistTime;
		} else if (pString[i] == '*' || pString[i] == '#') {
			Delay = HashCodePersistTime;
		} else {
			Delay = CodePersistTime;
		}
		SYSTEM_DelayMs(Delay);
		BK4819_EnterTxMute();
		SYSTEM_DelayMs(CodeInternalTime);
	}
}

void BK4819_TransmitTone(bool bLocalLoopback, uint32_t Frequency)
{
	BK4819_EnterTxMute();
	BK4819_WriteRegister(BK4819_REG_70, 0
			| BK4819_REG_70_MASK_ENABLE_TONE1
			| (96U << BK4819_REG_70_SHIFT_TONE1_TUNING_GAIN));
	BK4819_WriteRegister(BK4819_REG_71, (uint16_t)((Frequency * 1032444) / 100000));
	if (bLocalLoopback) {
		BK4819_SetAF(BK4819_AF_BEEP);
	} else {
		BK4819_SetAF(BK4819_AF_MUTE);
	}
	BK4819_EnableTXLink();
	SYSTEM_DelayMs(50);
	BK4819_ExitTxMute();
}

void BK4819_GenTail(uint8_t Tail)
{
	switch (Tail) {
	case 0: // CTC134
		BK4819_WriteRegister(BK4819_REG_52, 0x828F);
		break;
	case 1: // CTC120
		BK4819_WriteRegister(BK4819_REG_52, 0xA28F);
		break;
	case 2: // CTC180
		BK4819_WriteRegister(BK4819_REG_52, 0xC28F);
		break;
	case 3: // CTC240
		BK4819_WriteRegister(BK4819_REG_52, 0xE28F);
		break;
	case 4: // CTC55
		BK4819_WriteRegister(BK4819_REG_07, 0x046f);
		break;
	}
}

void BK4819_EnableCDCSS(void)
{
	BK4819_GenTail(0); // CTC134
	BK4819_WriteRegister(BK4819_REG_51, 0x804A);
}

void BK4819_EnableCTCSS(void)
{
	BK4819_GenTail(4); // CTC55
	BK4819_WriteRegister(BK4819_REG_51, 0x904A);
}

uint16_t BK4819_GetRSSI(void)
{
	return BK4819_ReadRegister(BK4819_REG_67) & 0x01FF;
}

bool BK4819_GetFrequencyScanResult(uint32_t *pFrequency)
{
	uint16_t High, Low;
	bool Finished;

	High = BK4819_ReadRegister(BK4819_REG_0D);
	Finished = (High & 0x8000) == 0;
	if (Finished) {
		Low = BK4819_ReadRegister(BK4819_REG_0E);
		*pFrequency = (uint32_t)((High & 0x7FF) << 16) | Low;
	}

	return Finished;
}

BK4819_CssScanResult_t BK4819_GetCxCSSScanResult(uint32_t *pCdcssFreq, uint16_t *pCtcssFreq)
{
	uint16_t High, Low;

	High = BK4819_ReadRegister(BK4819_REG_69);
	if ((High & 0x8000) == 0) {
		Low = BK4819_ReadRegister(BK4819_REG_6A);
		*pCdcssFreq = ((High & 0xFFF) << 12) | (Low & 0xFFF);
		return BK4819_CSS_RESULT_CDCSS;
	}

	Low = BK4819_ReadRegister(BK4819_REG_68);
	if ((Low & 0x8000) == 0) {
		*pCtcssFreq = (Low & 0x1FFF) * 4843 / 10000;
		return BK4819_CSS_RESULT_CTCSS;
	}

	return BK4819_CSS_RESULT_NOT_FOUND;
}

void BK4819_DisableFrequencyScan(void)
{
	BK4819_WriteRegister(BK4819_REG_32, 0x0244);
}

void BK4819_EnableFrequencyScan(void)
{
	BK4819_WriteRegister(BK4819_REG_32, 0x0245);
}

void BK4819_SetScanFrequency(uint32_t Frequency)
{
	BK4819_SetFrequency(Frequency);
	BK4819_WriteRegister(BK4819_REG_51, 0
		| BK4819_REG_51_DISABLE_CxCSS
		| BK4819_REG_51_GPIO6_PIN2_NORMAL
		| BK4819_REG_51_TX_CDCSS_POSITIVE
		| BK4819_REG_51_MODE_CDCSS
		| BK4819_REG_51_CDCSS_23_BIT
		| BK4819_REG_51_1050HZ_NO_DETECTION
		| BK4819_REG_51_AUTO_CDCSS_BW_DISABLE
		| BK4819_REG_51_AUTO_CTCSS_BW_DISABLE
		);
	BK4819_RX_TurnOn();
}

void BK4819_Disable(void)
{
	BK4819_WriteRegister(BK4819_REG_30, 0);
}

uint8_t BK4819_GetDTMF_5TONE_Code(void)
{
	return (BK4819_ReadRegister(BK4819_REG_0B) >> 8) & 0x0F;
}

uint8_t BK4819_GetCDCSSCodeType(void)
{
	return (BK4819_ReadRegister(BK4819_REG_0C) >> 14) & 3;
}

uint8_t BK4819_GetCTCType(void)
{
	return (BK4819_ReadRegister(BK4819_REG_0C) >> 10) & 3;
}

void BK4819_PlayRoger(void)
{
	BK4819_EnterTxMute();
	BK4819_SetAF(BK4819_AF_MUTE);
	BK4819_WriteRegister(BK4819_REG_70, 0xE000);
	BK4819_EnableTXLink();
	SYSTEM_DelayMs(50);
	BK4819_WriteRegister(BK4819_REG_71, 0x142A);
	BK4819_ExitTxMute();
	SYSTEM_DelayMs(80);
	BK4819_EnterTxMute();
	BK4819_WriteRegister(BK4819_REG_71, 0x1C3B);
	BK4819_ExitTxMute();
	SYSTEM_DelayMs(80);
	BK4819_EnterTxMute();
	BK4819_WriteRegister(BK4819_REG_70, 0);
	BK4819_WriteRegister(BK4819_REG_30, 0xC1FE);
}

#if defined(ENABLE_MDC1200)
void BK4819_DisableMDC1200Rx(void)
{
	// REG_70
	//
	// <15>    0 TONE-1
	//         1 = enable
	//         0 = disable
	//
	// <14:8>  0 TONE-1 gain
	//
	// <7>     0 TONE-2
	//         1 = enable
	//         0 = disable
	//
	// <6:0>   0 TONE-2 / FSK gain
	//         0 ~ 127
	//
	// enable tone-2, set gain

	// REG_72
	//
	// <15:0>  0x2854 TONE-2 / FSK frequency control word
	//         = freq(Hz) * 10.32444 for XTAL 13M / 26M or
	//         = freq(Hz) * 10.48576 for XTAL 12.8M / 19.2M / 25.6M / 38.4M
	//
	// tone-2 = 1200Hz

	// REG_58
	//
	// <15:13> 1 FSK TX mode selection
	//         0 = FSK 1.2K and FSK 2.4K TX .. no tones, direct FM
	//         1 = FFSK 1200 / 1800 TX
	//         2 = ???
	//         3 = FFSK 1200 / 2400 TX
	//         4 = ???
	//         5 = NOAA SAME TX
	//         6 = ???
	//         7 = ???
	//
	// <12:10> 0 FSK RX mode selection
	//         0 = FSK 1.2K, FSK 2.4K RX and NOAA SAME RX .. no tones, direct FM
	//         1 = ???
	//         2 = ???
	//         3 = ???
	//         4 = FFSK 1200 / 2400 RX
	//         5 = ???
	//         6 = ???
	//         7 = FFSK 1200 / 1800 RX
	//
	// <9:8>   0 FSK RX gain
	//         0 ~ 3
	//
	// <7:6>   0 ???
	//         0 ~ 3
	//
	// <5:4>   0 FSK preamble type selection
	//         0 = 0xAA or 0x55 due to the MSB of FSK sync byte 0
	//         1 = ???
	//         2 = 0x55
	//         3 = 0xAA
	//
	// <3:1>   1 FSK RX bandwidth setting
	//         0 = FSK 1.2K .. no tones, direct FM
	//         1 = FFSK 1200 / 1800
	//         2 = NOAA SAME RX
	//         3 = ???
	//         4 = FSK 2.4K and FFSK 1200 / 2400
	//         5 = ???
	//         6 = ???
	//         7 = ???
	//
	// <0>     1 FSK enable
	//         0 = disable
	//         1 = enable

	// REG_5C
	//
	// <15:7>  ???
	//
	// <6>     1 CRC option enable
	//         0 = disable
	//         1 = enable
	//
	// <5:0>   ???
	//
	// disable CRC

	// REG_5D
	//
	// set the packet size

	BK4819_WriteRegister(BK4819_REG_70, 0);
	BK4819_WriteRegister(BK4819_REG_58, 0);
}

void BK4819_EnableMDC1200Rx(void)
{
	const uint16_t fsk_reg59 =
		(0u << 15) |   // 1 = clear TX FIFO
		(0u << 14) |   // 1 = clear RX FIFO
		(0u << 13) |   // 1 = scramble
		(0u << 12) |   // 1 = enable RX
		(0u << 11) |   // 1 = enable TX
		(0u << 10) |   // 1 = invert data when RX
		(0u <<  9) |   // 1 = invert data when TX
		(0u <<  8) |   // ???
		(0u <<  4) |   // 0 ~ 15 preamble length selection .. mdc1200 does not send bit reversals :(
		(1u <<  3) |   // 0/1 sync length selection
		(0u <<  0);    // 0 ~ 7  ???

	BK4819_WriteRegister(0x70,
		( 0u << 15) |    // 0
		( 0u <<  8) |    // 0
		( 1u <<  7) |    // 1
		(96u <<  0));    // 96

	BK4819_WriteRegister(0x72, ScaleFreq(1200));

	BK4819_WriteRegister(0x58,
		(1u << 13) |		// 1 FSK TX mode selection
							//   0 = FSK 1.2K and FSK 2.4K TX .. no tones, direct FM
							//   1 = FFSK 1200 / 1800 TX
							//   2 = ???
							//   3 = FFSK 1200 / 2400 TX
							//   4 = ???
							//   5 = NOAA SAME TX
							//   6 = ???
							//   7 = ???
							//
		(7u << 10) |		// 0 FSK RX mode selection
							//   0 = FSK 1.2K, FSK 2.4K RX and NOAA SAME RX .. no tones, direct FM
							//   1 = ???
							//   2 = ???
							//   3 = ???
							//   4 = FFSK 1200 / 2400 RX
							//   5 = ???
							//   6 = ???
							//   7 = FFSK 1200 / 1800 RX
							//
		(3u << 8) |			// 0 FSK RX gain
							//   0 ~ 3
							//
		(0u << 6) |			// 0 ???
							//   0 ~ 3
							//
		(0u << 4) |			// 0 FSK preamble type selection
							//   0 = 0xAA or 0x55 due to the MSB of FSK sync byte 0
							//   1 = ???
							//   2 = 0x55
							//   3 = 0xAA
							//
		(1u << 1) |			// 1 FSK RX bandwidth setting
							//   0 = FSK 1.2K .. no tones, direct FM
							//   1 = FFSK 1200 / 1800
							//   2 = NOAA SAME RX
							//   3 = ???
							//   4 = FSK 2.4K and FFSK 1200 / 2400
							//   5 = ???
							//   6 = ???
							//   7 = ???
							//
		(1u << 0));			// 1 FSK enable
							//   0 = disable
							//   1 = enable

	// REG_5A .. bytes 0 & 1 sync pattern
	//
	// <15:8> sync byte 0
	// < 7:0> sync byte 1
	BK4819_WriteRegister(0x5A, ((uint16_t)mdc1200_sync_suc_xor[1] << 8) | (mdc1200_sync_suc_xor[2] << 0));

	// REG_5B .. bytes 2 & 3 sync pattern
	//
	// <15:8> sync byte 2
	// < 7:0> sync byte 3
	BK4819_WriteRegister(0x5B, ((uint16_t)mdc1200_sync_suc_xor[3] << 8) | (mdc1200_sync_suc_xor[4] << 0));

	// disable CRC
	BK4819_WriteRegister(0x5C, 0x5625);   // 01010110 0 0 100101

	// set the almost full threshold
	BK4819_WriteRegister(0x5E, (64u << 3) | (1u << 0));  // 0 ~ 127, 0 ~ 7

	{	// packet size .. sync + 14 bytes - size of a single mdc1200 packet
		uint16_t size = 0 + (MDC1200_FEC_K * 2);
		size = ((size + 1) / 2) * 2;             // round up to even, else FSK RX doesn't work
		BK4819_WriteRegister(0x5D, ((size - 1) << 8));
	}

	// clear FIFO's then enable RX
	BK4819_WriteRegister(0x59, (1u << 15) | (1u << 14) | fsk_reg59);
	BK4819_WriteRegister(0x59, (1u << 12) | fsk_reg59);

	// clear interrupt flags
	BK4819_WriteRegister(0x02, 0);
}

void BK4819_SendMDC1200(uint8_t op, uint8_t arg, uint16_t id, bool long_preamble, BK4819_FilterBandwidth_t Bandwidth)
{
	uint16_t fsk_reg59;
	uint8_t  packet[42];

	// create the MDC1200 packet
	const unsigned int size = MDC1200_encode_single_packet(packet, op, arg, id);

	BK4819_WriteRegister(0x50, 0x3B20);  // 0011 1011 0010 0000

	BK4819_WriteRegister(0x30,
		BK4819_REG_30_ENABLE_VCO_CALIB |
		BK4819_REG_30_ENABLE_UNKNOWN   |
		BK4819_REG_30_ENABLE_AF_DAC    |
		BK4819_REG_30_ENABLE_DISC_MODE |
		BK4819_REG_30_ENABLE_PLL_VCO   |
		BK4819_REG_30_ENABLE_PA_GAIN   |
		BK4819_REG_30_ENABLE_TX_DSP    |
	0);

	GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
	BK4819_SetAF(BK4819_AF_MUTE);

	// REG_51
	//
	// <15>  TxCTCSS/CDCSS   0 = disable 1 = Enable
	//
	// turn off CTCSS/CDCSS during FFSK
	const uint16_t css_val = BK4819_ReadRegister(0x51);
	BK4819_WriteRegister(0x51, 0);

	// set the FM deviation level
	const uint16_t dev_val = BK4819_ReadRegister(0x40);

	uint16_t deviation;
	switch (Bandwidth) {
	case BK4819_FILTER_BW_WIDE:
		deviation = 1050;
		break;
	case BK4819_FILTER_BW_NARROW:
		deviation = 850;
		break;
	case BK4819_FILTER_BW_NARROWER:
		deviation = 750;
		break;
	default:
		// Fix warning of using this uninitialised
		deviation = 0;
	}
	BK4819_WriteRegister(0x40, (dev_val & 0xf000) | (deviation & 0xfff));

	// REG_2B   0
	//
	// <15> 1 Enable CTCSS/CDCSS DC cancellation after FM Demodulation   1 = enable 0 = disable
	// <14> 1 Enable AF DC cancellation after FM Demodulation            1 = enable 0 = disable
	// <10> 0 AF RX HPF 300Hz filter     0 = enable 1 = disable
	// <9>  0 AF RX LPF 3kHz filter      0 = enable 1 = disable
	// <8>  0 AF RX de-emphasis filter   0 = enable 1 = disable
	// <2>  0 AF TX HPF 300Hz filter     0 = enable 1 = disable
	// <1>  0 AF TX LPF filter           0 = enable 1 = disable
	// <0>  0 AF TX pre-emphasis filter  0 = enable 1 = disable
	//
	// disable the 300Hz HPF and FM pre-emphasis filter
	//
	const uint16_t filt_val = BK4819_ReadRegister(0x2B);
	BK4819_WriteRegister(0x2B, (1u << 2) | (1u << 0));

	// *******************************************
	// setup the FFSK modem as best we can for MDC1200

	// MDC1200 uses 1200/1800 Hz FSK tone frequencies 1200 bits/s
	//
	BK4819_WriteRegister(0x58, // 0x37C3);   // 001 101 11 11 00 001 1
		(1u << 13) |		// 1 FSK TX mode selection
							//   0 = FSK 1.2K and FSK 2.4K TX .. no tones, direct FM
							//   1 = FFSK 1200/1800 TX
							//   2 = ???
							//   3 = FFSK 1200/2400 TX
							//   4 = ???
							//   5 = NOAA SAME TX
							//   6 = ???
							//   7 = ???
							//
		(7u << 10) |		// 0 FSK RX mode selection
							//   0 = FSK 1.2K, FSK 2.4K RX and NOAA SAME RX .. no tones, direct FM
							//   1 = ???
							//   2 = ???
							//   3 = ???
							//   4 = FFSK 1200/2400 RX
							//   5 = ???
							//   6 = ???
							//   7 = FFSK 1200/1800 RX
							//
		(0u << 8) |			// 0 FSK RX gain
							//   0 ~ 3
							//
		(0u << 6) |			// 0 ???
							//   0 ~ 3
							//
		(0u << 4) |			// 0 FSK preamble type selection
							//   0 = 0xAA or 0x55 due to the MSB of FSK sync byte 0
							//   1 = ???
							//   2 = 0x55
							//   3 = 0xAA
							//
		(1u << 1) |			// 1 FSK RX bandwidth setting
							//   0 = FSK 1.2K .. no tones, direct FM
							//   1 = FFSK 1200/1800
							//   2 = NOAA SAME RX
							//   3 = ???
							//   4 = FSK 2.4K and FFSK 1200/2400
							//   5 = ???
							//   6 = ???
							//   7 = ???
							//
		(1u << 0));			// 1 FSK enable
							//   0 = disable
							//   1 = enable

	// REG_72
	//
	// <15:0> 0x2854 TONE-2 / FSK frequency control word
	//        = freq(Hz) * 10.32444 for XTAL 13M / 26M or
	//        = freq(Hz) * 10.48576 for XTAL 12.8M / 19.2M / 25.6M / 38.4M
	//
	// tone-2 = 1200Hz
	//
	BK4819_WriteRegister(0x72, ScaleFreq(1200));

	// REG_70
	//
	// <15>   0 TONE-1
	//        1 = enable
	//        0 = disable
	//
	// <14:8> 0 TONE-1 tuning
	//
	// <7>    0 TONE-2
	//        1 = enable
	//        0 = disable
	//
	// <6:0>  0 TONE-2 / FSK tuning
	//        0 ~ 127
	//
	// enable tone-2, set gain
	//
	BK4819_WriteRegister(0x70,   // 0 0000000 1 1100000
		( 0u << 15) |    // 0
		( 0u <<  8) |    // 0
		( 1u <<  7) |    // 1
		(96u <<  0));    // 96

	// REG_59
	//
	// <15>  0 TX FIFO             1 = clear
	// <14>  0 RX FIFO             1 = clear
	// <13>  0 FSK Scramble        1 = Enable
	// <12>  0 FSK RX              1 = Enable
	// <11>  0 FSK TX              1 = Enable
	// <10>  0 FSK data when RX    1 = Invert
	// <9>   0 FSK data when TX    1 = Invert
	// <8>   0 ???
	//
	// <7:4> 0 FSK preamble length selection
	//       0  =  1 byte
	//       1  =  2 bytes
	//       2  =  3 bytes
	//       15 = 16 bytes
	//
	// <3>   0 FSK sync length selection
	//       0 = 2 bytes (FSK Sync Byte 0, 1)
	//       1 = 4 bytes (FSK Sync Byte 0, 1, 2, 3)
	//
	// <2:0> 0 ???
	//
	fsk_reg59 = (0u << 15) |   // 0/1     1 = clear TX FIFO
				(0u << 14) |   // 0/1     1 = clear RX FIFO
				(0u << 13) |   // 0/1     1 = scramble
				(0u << 12) |   // 0/1     1 = enable RX
				(0u << 11) |   // 0/1     1 = enable TX
				(0u << 10) |   // 0/1     1 = invert data when RX
				(0u <<  9) |   // 0/1     1 = invert data when TX
				(0u <<  8) |   // 0/1     ???
				(0u <<  4) |   // 0 ~ 15  preamble length .. bit toggling
				(1u <<  3) |   // 0/1     sync length
				(0u <<  0);    // 0 ~ 7   ???
	fsk_reg59 |= long_preamble ? 15u << 4 : 3u << 4; 
		
	// Set packet length (not including pre-amble and sync bytes that we can't seem to disable)
	BK4819_WriteRegister(0x5D, ((size - 1) << 8));

	// REG_5A
	//
	// <15:8> 0x55 FSK Sync Byte 0 (Sync Byte 0 first, then 1,2,3)
	// <7:0>  0x55 FSK Sync Byte 1
	//
	BK4819_WriteRegister(0x5A, 0);                   // bytes 1 & 2

	// REG_5B
	//
	// <15:8> 0x55 FSK Sync Byte 2 (Sync Byte 0 first, then 1,2,3)
	// <7:0>  0xAA FSK Sync Byte 3
	//
	BK4819_WriteRegister(0x5B, 0);                   // bytes 2 & 3

	// CRC setting (plus other stuff we don't know what)
	//
	// REG_5C
	//
	// <15:7> ???
	//
	// <6>    1 CRC option enable    0 = disable  1 = enable
	//
	// <5:0>  ???
	//
	// disable CRC
	//
	// NB, this also affects TX pre-amble in some way
	//
	BK4819_WriteRegister(0x5C, 0x5625);   // 010101100 0 100101

	BK4819_WriteRegister(0x59, (1u << 15) | (1u << 14) | fsk_reg59);   // clear FIFOs
	BK4819_WriteRegister(0x59, fsk_reg59);                             // release the FIFO reset

	// load the entire packet data into the TX FIFO buffer
	unsigned int i;
	const uint16_t *p = (const uint16_t *)packet;
	for (i = 0; i < (size / sizeof(p[0])); i++) {
		BK4819_WriteRegister(0x5F, p[i]); // load 16-bits at a time
	}

	// enable tx interrupt
	BK4819_WriteRegister(0x3F, BK4819_REG_3F_FSK_TX_FINISHED);

	// enable FSK TX
	BK4819_WriteRegister(0x59, (1u << 11) | fsk_reg59);

	// packet time is ..
	// 173ms for PTT ID, acks, emergency
	// 266ms for call alert and sel-calls

	// allow up to 310ms for the TX to complete
	// if it takes any longer then somethings gone wrong, we shut the TX down
	unsigned int timeout = 75;

	while (timeout-- > 0) {
		if (BK4819_ReadRegister(0x0C) & (1u << 0)) {
			// we have interrupt flags
			BK4819_WriteRegister(0x02, 0);
			if (BK4819_ReadRegister(0x02) & BK4819_REG_02_FSK_TX_FINISHED) {
				timeout = 0; // TX is complete
			}
		}
	}

	GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);

	// disable FSK
	BK4819_WriteRegister(0x59, fsk_reg59);

	BK4819_WriteRegister(0x3F, 0);   // disable interrupts
	BK4819_WriteRegister(0x70, 0);
	BK4819_WriteRegister(0x58, 0);

	// restore FM deviation level
	BK4819_WriteRegister(0x40, dev_val);

	// restore TX/RX filtering
	BK4819_WriteRegister(0x2B, filt_val);

	// restore the CTCSS/CDCSS setting
	BK4819_WriteRegister(0x51, css_val);

	//BK4819_EnterTxMute();
	BK4819_WriteRegister(0x50, 0xBB20); // 1011 1011 0010 0000

	//BK4819_SetAF(BK4819_AF_MUTE);
	BK4819_WriteRegister(0x47, (1u << 14) | (1u << 13) | (BK4819_AF_MUTE << 8) | (1u << 6));

	BK4819_WriteRegister(0x30,
		BK4819_REG_30_ENABLE_VCO_CALIB |
		BK4819_REG_30_ENABLE_UNKNOWN   |
		BK4819_REG_30_ENABLE_DISC_MODE |
		BK4819_REG_30_ENABLE_PLL_VCO   |
		BK4819_REG_30_ENABLE_PA_GAIN   |
		BK4819_REG_30_ENABLE_MIC_ADC   |
		BK4819_REG_30_ENABLE_TX_DSP    |
	0);

	BK4819_WriteRegister(0x50, 0x3B20);  // 0011 1011 0010 0000
}
#endif

void BK4819_PlayDTMFEx(bool bLocalLoopback, char Code)
{
	BK4819_EnableDTMF();
	BK4819_EnterTxMute();
	if (bLocalLoopback) {
		BK4819_SetAF(BK4819_AF_BEEP);
	} else {
		BK4819_SetAF(BK4819_AF_MUTE);
	}
	BK4819_WriteRegister(BK4819_REG_70, 0xD3D3);
	BK4819_EnableTXLink();
	SYSTEM_DelayMs(50);
	BK4819_PlayDTMF(Code);
	BK4819_ExitTxMute();
}

