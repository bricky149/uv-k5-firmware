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

#include "bsp/dp32g030/gpio.h"
#include "driver/bk1080.h"
#include "driver/gpio.h"
#include "driver/i2c.h"

//#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

static const uint16_t BK1080_RegisterTable[34] = {
	0x0008, 0x1080, 0x0201, 0x0000,
	0x40C0, 0x0A1F, 0x002E, 0x02FF,
	0x5B11, 0x0000, 0x411E, 0x0000,
	0xCE00, 0x0000, 0x0000, 0x1000,
	0x3197, 0x0000, 0x13FF, 0x9852,
	0x0000, 0x0000, 0x0008, 0x0000,
	0x51E1, 0xA8BC, 0x2645, 0x00E4,
	0x1CD8, 0x3A50, 0xEAE0, 0x3000,
	0x0200, 0x0000,
};

static bool gIsInitBK1080;

uint16_t BK1080_BaseFrequency;
uint16_t BK1080_FrequencyDeviation;

void BK1080_Enable(void)
{
	GPIO_ClearBit(&GPIOB->DATA, GPIOB_PIN_BK1080);
	if (!gIsInitBK1080) {
		for (int i = 0; i < 34; i++) {
			BK1080_WriteRegister(i, BK1080_RegisterTable[i]);
		}
		BK1080_WriteRegister(BK1080_REG_25_INTERNAL, 0xA83C);
		BK1080_WriteRegister(BK1080_REG_25_INTERNAL, 0xA8BC);
		gIsInitBK1080 = true;
	} else {
		BK1080_WriteRegister(BK1080_REG_02_POWER_CONFIGURATION, 0x0201);
	}
	BK1080_WriteRegister(BK1080_REG_05_SYSTEM_CONFIGURATION2, 0x0A5F);
}

void BK1080_Sleep(void)
{
	BK1080_WriteRegister(BK1080_REG_02_POWER_CONFIGURATION, 0x0241);
	GPIO_SetBit(&GPIOB->DATA, GPIOB_PIN_BK1080);
}

uint16_t BK1080_ReadRegister(BK1080_Register_t Register)
{
	I2C_Start();
	I2C_Write(0x80);
	I2C_Write((Register << 1) | I2C_READ);
	uint8_t Value[2];
	I2C_ReadBuffer(Value, sizeof(Value));
	I2C_Stop();
	return (Value[0] << 8) | Value[1];
}

void BK1080_WriteRegister(BK1080_Register_t Register, uint16_t Value)
{
	I2C_Start();
	I2C_Write(0x80);
	I2C_Write((Register << 1) | I2C_WRITE);
	Value = ((Value >> 8) & 0xFF) | ((Value & 0xFF) << 8);
	I2C_WriteBuffer(&Value, sizeof(Value));
	I2C_Stop();
}

void BK1080_Mute(void)
{
	BK1080_WriteRegister(BK1080_REG_02_POWER_CONFIGURATION, 0x4201);
}

void BK1080_Unmute(void)
{
	BK1080_WriteRegister(BK1080_REG_02_POWER_CONFIGURATION, 0x0201);
}

void BK1080_SetFrequency(uint16_t Frequency)
{
	BK1080_WriteRegister(BK1080_REG_03_CHANNEL, Frequency - 760);
	BK1080_WriteRegister(BK1080_REG_03_CHANNEL, (Frequency - 760) | 0x8000);
}

void BK1080_GetFrequencyDeviation(uint16_t Frequency)
{
	BK1080_BaseFrequency = Frequency;
	BK1080_FrequencyDeviation = BK1080_ReadRegister(BK1080_REG_07) / 16;
}

