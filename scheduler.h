#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

enum {
	TASK_SLEEP                  = 0x0000U,
	TASK_UPDATE_SCREEN          = 0x0001U,
	TASK_CHECK_KEYS             = 0x0002U,
	TASK_CHECK_RADIO_INTERRUPTS = 0x0004U,
	//TASK_CHECK_BATTERY          = 0x0008U,
	TASK_SCANNER                = 0x0010U,
	TASK_FM_RADIO               = 0x0020U,
};

bool SCHEDULER_CheckTask(uint16_t Task);
void SCHEDULER_ClearTask(uint16_t Task);

#endif

