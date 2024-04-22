#include "driver/bk4819.h"
#include "misc.h"
#include "scheduler.h"

static const uint8_t RSSI_CEILING = 143; // S9

void TASK_AM_Fix(void) {
	if (!SCHEDULER_CheckTask(TASK_AM_FIX)) {
		return;
	}
	SCHEDULER_ClearTask(TASK_AM_FIX);
	// Read current AGC fix index so we don't suddenly peak
	uint16_t AgcFixIndex = (BK4819_ReadRegister(BK4819_REG_7E) >> 12) & 3;
	// Take the difference between current and desired RSSI readings
	const uint16_t RSSI = BK4819_GetRSSI();
	const int8_t Diff_dB = RSSI - RSSI_CEILING;

	if (Diff_dB > 0 && AgcFixIndex != 4) {
		// Over distortion threshold, reduce gain
		AgcFixIndex = (AgcFixIndex + 7) % 8; // Decrement AGC fix index
	} else if (g_SquelchLost && AgcFixIndex != 3) {
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
