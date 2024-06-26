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

#ifndef DRIVER_BK4819_h
#define DRIVER_BK4819_h

#include <stdbool.h>
#include <stdint.h>
#include "driver/bk4819-regs.h"

enum BK4819_ModType_t {
	MOD_FM = 0U,
	MOD_AM = 1U,
	MOD_USB = 2U, // Single Sideband
	MOD_DIG = 3U, // Digital
};

typedef enum BK4819_ModType_t BK4819_ModType_t;

enum BK4819_AF_Type_t {
	BK4819_AF_MUTE = 0U,
	BK4819_AF_FM = 1U,
	BK4819_AF_ALAM = 2U,
	BK4819_AF_BEEP = 3U,
	//BK4819_AF_RAW = 4U,
	BK4819_AF_USB = 5U,  // Single Sideband
	BK4819_AF_CTCO = 6U,
	BK4819_AF_AM = 7U,
	BK4819_AF_FSKO = 8U,
	//BK4819_AF_BYP = 9U,
};

typedef enum BK4819_AF_Type_t BK4819_AF_Type_t;

enum BK4819_FilterBandwidth_t {
	BK4819_FILTER_BW_WIDE = 0U,     // 25kHz
	BK4819_FILTER_BW_NARROW = 1U,   // 12.5kHz
	BK4819_FILTER_BW_NARROWER = 2U, // 6.25kHz
#if defined(ENABLE_DIGITAL_MODULATION)
	BK4819_FILTER_BW_DIGITAL_WIDE = 3U,
	BK4819_FILTER_BW_DIGITAL_NARROW = 4U,
#endif
};

typedef enum BK4819_FilterBandwidth_t BK4819_FilterBandwidth_t;

enum BK4819_CssScanResult_t {
	BK4819_CSS_RESULT_NOT_FOUND = 0U,
	BK4819_CSS_RESULT_CTCSS = 1U,
	BK4819_CSS_RESULT_CDCSS = 2U,
};

typedef enum BK4819_CssScanResult_t BK4819_CssScanResult_t;

extern bool gRxIdleMode;

void BK4819_Init(void);
uint16_t BK4819_ReadRegister(BK4819_REGISTER_t Register);
void BK4819_WriteRegister(BK4819_REGISTER_t Register, uint16_t Data);
void BK4819_WriteU8(uint8_t Data);

void BK4819_InitAGC(void);
void BK4819_SetAGC(BK4819_ModType_t ModType);

void BK4819_SetGpioOut(BK4819_GPIO_PIN_t Pin);
void BK4819_ClearGpioOut(BK4819_GPIO_PIN_t Pin);

void BK4819_SetCDCSSCodeWord(uint32_t CodeWord);
void BK4819_SetCTCSSFrequency(uint32_t BaudRate);
void BK4819_Set55HzTailDetection(void);

void BK4819_SetFilterBandwidth(BK4819_FilterBandwidth_t Bandwidth, bool weak_no_different);
void BK4819_SetupPowerAmplifier(uint8_t Bias, uint32_t Frequency);
void BK4819_SetFrequency(uint32_t Frequency);
void BK4819_SetupSquelch(
		uint8_t SquelchOpenRSSIThresh, uint8_t SquelchCloseRSSIThresh,
		uint8_t SquelchOpenNoiseThresh, uint8_t SquelchCloseNoiseThresh,
		uint8_t SquelchCloseGlitchThresh, uint8_t SquelchOpenGlitchThresh);

void BK4819_SetModulation(BK4819_ModType_t ModType);
void BK4819_SetAF(BK4819_AF_Type_t AF);
void BK4819_RX_TurnOn(void);
void BK4819_SelectFilter(uint32_t Frequency);

void BK4819_SetCompander(uint8_t Mode);

void BK4819_DisableDTMF(void);
void BK4819_EnableDTMF(void);
//void BK4819_PlayTone(uint16_t Frequency, bool bTuningGainSwitch);
void BK4819_EnterTxMute(void);
void BK4819_ExitTxMute(void);
void BK4819_Sleep(void);
//void BK4819_TurnsOffTones_TurnsOnRX(void);

//void BK4819_ExitBypass(void);

#if defined(ENABLE_DIGITAL_MODULATION)
void BK4819_PrepareDigitalTransmit(const BK4819_FilterBandwidth_t Bandwidth);
#endif

void BK4819_PrepareTransmit(void);
void BK4819_TxOn_Beep(void);
void BK4819_ExitSubAu(void);

void BK4819_EnableRX(void);

void BK4819_EnterDTMF_TX(bool bLocalLoopback);
void BK4819_ExitDTMF_TX(bool bKeep);
void BK4819_EnableTXLink(void);

void BK4819_PlayDTMF(char Code);
void BK4819_PlayDTMFString(const char *pString, bool bDelayFirst, uint16_t FirstCodePersistTime, uint16_t HashCodePersistTime, uint16_t CodePersistTime, uint16_t CodeInternalTime);

void BK4819_TransmitTone(bool bLocalLoopback, uint32_t Frequency);

void BK4819_GenTail(uint8_t Tail);
void BK4819_EnableCDCSS(void);
void BK4819_EnableCTCSS(void);

uint16_t BK4819_GetRSSI(void);

bool BK4819_GetFrequencyScanResult(uint32_t *pFrequency);
BK4819_CssScanResult_t BK4819_GetCxCSSScanResult(uint32_t *pCdcssFreq, uint16_t *pCtcssFreq);
void BK4819_DisableFrequencyScan(void);
void BK4819_EnableFrequencyScan(void);
void BK4819_SetScanFrequency(uint32_t Frequency);

void BK4819_Disable(void);

uint8_t BK4819_GetDTMF_5TONE_Code(void);

uint8_t BK4819_GetCDCSSCodeType(void);
uint8_t BK4819_GetCTCType(void);

void BK4819_PlayRoger(void);
//void BK4819_PlayRogerMDC(void);

#if defined(ENABLE_MDC1200)
void BK4819_DisableMDC1200Rx(void);
void BK4819_EnableMDC1200Rx(void);
void BK4819_SendMDC1200(uint8_t op, uint8_t arg, uint16_t id, bool long_preamble, BK4819_FilterBandwidth_t Bandwidth);
#endif

//void BK4819_Enable_AfDac_DiscMode_TxDsp(void);

void BK4819_PlayDTMFEx(bool bLocalLoopback, char Code);

#endif

