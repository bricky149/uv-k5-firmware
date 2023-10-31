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

#ifndef MISC_H
#define MISC_H

#include <stdbool.h>
#include <stdint.h>

#define IS_MR_CHANNEL(x) ((x) <= MR_CHANNEL_LAST)
#define IS_FREQ_CHANNEL(x) ((x) >= FREQ_CHANNEL_FIRST && (x) <= FREQ_CHANNEL_LAST)
#define IS_VALID_CHANNEL(x) ((x) < LAST_CHANNEL)

enum {
	MR_CHANNEL_FIRST = 0U,
	MR_CHANNEL_LAST = 199U,
	FREQ_CHANNEL_FIRST = 200U,
	FREQ_CHANNEL_LAST = 206U,
	LAST_CHANNEL,
};

enum {
	FLASHLIGHT_OFF = 0U,
	FLASHLIGHT_ON = 1U,
	FLASHLIGHT_BLINK = 2U,
};

enum {
	VFO_CONFIGURE_NONE = 0U,
	VFO_CONFIGURE = 1U,
	VFO_CONFIGURE_RELOAD = 2U,
};

enum ReceptionMode_t {
	RX_MODE_NONE      = 0U,
	RX_MODE_DETECTED  = 1U,
	RX_MODE_LISTENING = 2U,
};

typedef enum ReceptionMode_t ReceptionMode_t;

enum CssScanMode_t {
	CSS_SCAN_MODE_OFF      = 0U,
	CSS_SCAN_MODE_SCANNING = 1U,
	CSS_SCAN_MODE_FOUND    = 2U,
};

typedef enum CssScanMode_t CssScanMode_t;

extern const uint32_t *gUpperLimitFrequencyBandTable;
extern const uint32_t *gLowerLimitFrequencyBandTable;

extern bool gSetting_350TX;
extern bool gSetting_200TX;
extern bool gSetting_500TX;
extern bool gSetting_350EN;
extern uint8_t gSetting_F_LOCK;

extern const uint32_t gDefaultAesKey[4];
extern uint32_t gCustomAesKey[4];
extern bool bHasCustomAesKey;
extern uint32_t gChallenge[4];
extern uint8_t gTryCount;

extern uint16_t gEEPROM_RSSI_CALIB[7][4];

extern uint16_t gEEPROM_1F8A;
extern uint16_t gEEPROM_1F8C;

extern uint8_t gMR_ChannelAttributes[207];

extern volatile bool gNextTimeslice500ms;
extern volatile uint16_t gBatterySaveCountdown;
extern volatile uint16_t gDualWatchCountdown;
extern volatile uint16_t gTxTimerCountdown;
extern volatile uint16_t gTailNoteEliminationCountdown;
extern volatile uint16_t gFmPlayCountdown;
extern bool gEnableSpeaker;
extern uint8_t gKeyLockCountdown;
extern uint8_t gRTTECountdown;
extern bool bIsInLockScreen;
extern uint8_t gUpdateStatus;
extern uint8_t gFoundCTCSS;
extern uint8_t gFoundCDCSS;
extern bool gEndOfRxDetectedMaybe;
extern uint8_t gVFO_RSSI_Level[2];
extern uint8_t gReducedService;
extern uint8_t gBatteryVoltageIndex;
extern CssScanMode_t gCssScanMode;
extern bool gUpdateRSSI;
extern uint8_t gVoltageMenuCountdown;
extern bool gPttWasReleased;
extern bool gPttWasPressed;
extern bool gFlagReconfigureVfos;
extern uint8_t gVfoConfigureMode;
extern bool gFlagResetVfos;
extern bool gRequestSaveVFO;
extern uint8_t gRequestSaveChannel;
extern bool gRequestSaveSettings;
#if defined(ENABLE_FMRADIO)
extern bool gRequestSaveFM;
#endif
extern uint8_t gKeypadLocked;
extern bool gFlagPrepareTX;
extern bool gFlagAcceptSetting;
extern bool gFlagRefreshSetting;
extern bool gFlagSaveVfo;
extern bool gFlagSaveSettings;
extern bool gFlagSaveChannel;
#if defined(ENABLE_FMRADIO)
extern bool gFlagSaveFM;
#endif
extern uint8_t gDTMF_RequestPending;
extern bool g_CDCSS_Lost;
extern uint8_t gCDCSSCodeType;
extern bool g_CTCSS_Lost;
extern bool g_CxCSS_TAIL_Found;
extern bool g_SquelchLost;
extern uint8_t gFlashLightState;
extern volatile uint16_t gFlashLightBlinkCounter;
extern bool gFlagEndTransmission;
extern uint16_t gLowBatteryCountdown;
extern uint8_t gNextMrChannel;
extern ReceptionMode_t gRxReceptionMode;
extern uint8_t gRestoreMrChannel;
extern uint8_t gCurrentScanList;
extern uint8_t gPreviousMrChannel;
extern uint32_t gRestoreFrequency;
extern uint8_t gRxVfoIsActive;
extern bool gKeyBeingHeld;
extern bool gPttIsPressed;
extern uint8_t gPttDebounceCounter;
extern uint8_t gMenuListCount;
extern uint8_t gBackupCROSS_BAND_RX_TX;
extern uint8_t gScanDelay;
extern uint8_t gFSKWriteIndex;


extern volatile bool gNextTimeslice;
extern bool gUpdateDisplay;
extern bool gF_LOCK;
extern uint8_t gShowChPrefix;
extern volatile uint8_t gFoundCDCSSCountdown;
extern volatile uint8_t gFoundCTCSSCountdown;
extern volatile bool gTxTimeoutReached;
extern volatile bool gNextTimeslice40ms;
extern volatile bool gSchedulePowerSave;
extern volatile bool gBatterySaveCountdownExpired;
extern volatile bool gScheduleDualWatch;
extern volatile bool gFlagTteComplete;
#if defined(ENABLE_FMRADIO)
extern volatile bool gScheduleFM;
#endif

extern uint16_t gCurrentRSSI;

extern uint8_t gIsLocked;

// --------

void NUMBER_Get(char *pDigits, uint32_t *pInteger);
void NUMBER_ToDigits(uint32_t Value, char *pDigits);
uint16_t NUMBER_AddWithWraparound(uint16_t Base, int8_t Add, uint16_t LowerLimit, uint16_t UpperLimit);

#endif

