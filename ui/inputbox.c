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
#include "ui/inputbox.h"

#define ARRAY_SIZE(x) (int)(sizeof(x) / sizeof(x[0]))

char gInputBox[8];
uint8_t gInputBoxIndex;

uint16_t INPUTBOX_GetValue(void)
{
	int i = gInputBoxIndex;
	if (i > ARRAY_SIZE(gInputBox)) {
		i = ARRAY_SIZE(gInputBox);
	}
	uint16_t val = 0;
	uint16_t mul = 1;
	while (--i >= 0) {
		val += gInputBox[i] * mul;
		mul *= 10;
	}
	return val;
}

void INPUTBOX_Append(char Digit)
{
	if (gInputBoxIndex >= sizeof(gInputBox)) {
		return;
	}
	if (gInputBoxIndex == 0) {
		memset(gInputBox, 10, sizeof(gInputBox));
	}
	gInputBox[gInputBoxIndex++] = Digit;
}

