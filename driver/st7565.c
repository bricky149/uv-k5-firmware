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

#include <stdint.h>
#include "bsp/dp32g030/gpio.h"
#include "bsp/dp32g030/spi.h"
#include "driver/gpio.h"
#include "driver/spi.h"
#include "driver/st7565.h"

//#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

uint8_t gStatusLine[128];
uint8_t gFrameBuffer[7][128];

void ST7565_DrawLine(uint8_t Column, uint8_t Line, uint16_t Size, const uint8_t *pBitmap, bool bIsClearMode)
{
	SPI_DisableMasterMode(&SPI0->CR);
	ST7565_SelectColumnAndLine(Column + 4U, Line);
	SPI_WaitForUndocumentedTxFifoStatusBit();

	GPIO_SetBit(&GPIOB->DATA, GPIOB_PIN_ST7565_A0);
	uint16_t i;
	if (!bIsClearMode) {
		for (i = 0; i < Size; i++) {
			while ((SPI0->FIFOST & SPI_FIFOST_TFF_MASK) != SPI_FIFOST_TFF_BITS_NOT_FULL) {
			}
			SPI0->WDR = pBitmap[i];
		}
	} else {
		for (i = 0; i < Size; i++) {
			while ((SPI0->FIFOST & SPI_FIFOST_TFF_MASK) != SPI_FIFOST_TFF_BITS_NOT_FULL) {
			}
			SPI0->WDR = 0;
		}
	}

	SPI_WaitForUndocumentedTxFifoStatusBit();
	SPI_EnableMasterMode(&SPI0->CR);
}

void ST7565_BlitFullScreen(void)
{
	SPI_DisableMasterMode(&SPI0->CR);
	ST7565_WriteByte(0x40);

	uint8_t Line; // ARRAY_SIZE(gFrameBuffer)
	uint8_t Column; // ARRAY_SIZE(gFrameBuffer[0])
	for (Line = 0; Line < 7; Line++) {
		ST7565_SelectColumnAndLine(4U, Line + 1U);
		SPI_WaitForUndocumentedTxFifoStatusBit();

		GPIO_SetBit(&GPIOB->DATA, GPIOB_PIN_ST7565_A0);
		for (Column = 0; Column < 128; Column++) {
			while ((SPI0->FIFOST & SPI_FIFOST_TFF_MASK) != SPI_FIFOST_TFF_BITS_NOT_FULL) {
			}
			SPI0->WDR = gFrameBuffer[Line][Column];
		}
		SPI_WaitForUndocumentedTxFifoStatusBit();
	}

	SPI_EnableMasterMode(&SPI0->CR);
}

void ST7565_BlitStatusLine(void)
{
	SPI_DisableMasterMode(&SPI0->CR);
	ST7565_WriteByte(0x40);

	ST7565_SelectColumnAndLine(4U, 0U);
	SPI_WaitForUndocumentedTxFifoStatusBit();

	GPIO_SetBit(&GPIOB->DATA, GPIOB_PIN_ST7565_A0);
	for (uint8_t Column = 0; Column < 128; Column++) {
		while ((SPI0->FIFOST & SPI_FIFOST_TFF_MASK) != SPI_FIFOST_TFF_BITS_NOT_FULL) {
		}
		SPI0->WDR = gStatusLine[Column];
	}
	SPI_WaitForUndocumentedTxFifoStatusBit();

	SPI_EnableMasterMode(&SPI0->CR);
}

void ST7565_Init(void)
{
	SPI0_Init();
	ST7565_HardwareReset();
	SPI_DisableMasterMode(&SPI0->CR);

	ST7565_WriteByte(0xE2);
	ST7565_WriteByte(0xA2);
	ST7565_WriteByte(0xC0);
	ST7565_WriteByte(0xA1);
	ST7565_WriteByte(0xA6);
	ST7565_WriteByte(0xA4);
	ST7565_WriteByte(0x24);
	ST7565_WriteByte(0x81);
	ST7565_WriteByte(0x1F);
	ST7565_WriteByte(0x2B);
	ST7565_WriteByte(0x2E);
	ST7565_WriteByte(0x2F);
	ST7565_WriteByte(0x2F);
	ST7565_WriteByte(0x2F);
	ST7565_WriteByte(0x2F);
	ST7565_WriteByte(0x40);
	ST7565_WriteByte(0xAF);

	SPI_WaitForUndocumentedTxFifoStatusBit();
	SPI_EnableMasterMode(&SPI0->CR);
	//SPI1_Init();
}

void ST7565_HardwareReset(void)
{
	GPIO_ClearBit(&GPIOB->DATA, GPIOB_PIN_ST7565_RES);
	GPIO_SetBit(&GPIOB->DATA, GPIOB_PIN_ST7565_RES);
}

void ST7565_SelectColumnAndLine(uint8_t Column, uint8_t Line)
{
	GPIO_ClearBit(&GPIOB->DATA, GPIOB_PIN_ST7565_A0);
	while ((SPI0->FIFOST & SPI_FIFOST_TFF_MASK) != SPI_FIFOST_TFF_BITS_NOT_FULL) {
	}
	SPI0->WDR = Line + 0xB0;
	while ((SPI0->FIFOST & SPI_FIFOST_TFF_MASK) != SPI_FIFOST_TFF_BITS_NOT_FULL) {
	}
	SPI0->WDR = ((Column >> 4) & 0x0F) | 0x10;
	while ((SPI0->FIFOST & SPI_FIFOST_TFF_MASK) != SPI_FIFOST_TFF_BITS_NOT_FULL) {
	}
	SPI0->WDR = ((Column >> 0) & 0x0F);
}

void ST7565_WriteByte(uint8_t Value)
{
	GPIO_ClearBit(&GPIOB->DATA, GPIOB_PIN_ST7565_A0);
	while ((SPI0->FIFOST & SPI_FIFOST_TFF_MASK) != SPI_FIFOST_TFF_BITS_NOT_FULL) {
	}
	SPI0->WDR = Value;
}

