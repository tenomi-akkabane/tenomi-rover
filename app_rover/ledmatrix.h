/**
 * @file ledmatrix.h
 * @author koh aiaida (koh@aiaida.jp)
 * @brief micro:bit V2 5x5 LED matrix
 * @date 2026-08-27
 *
 * @copyright Copyright (c) 2026 koh aiaida
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LEDMATRIX_H
#define LEDMATRIX_H

#include <tk/tkernel.h>

#define GPIO_PIN_MAP(port, pin) (((port) << 5) | ((pin) & 0x1F))

void InitLedMatrix(void);
void out_gpio_pin(UW port, UW pin, UW val);
void cfg_gpio_output(UW port, UW pin);
void LedMatrixSetBitmap(const UB rows[5]);
void LedMatrixScanTick(void);
ER LedMatrixStartScan(void);

#endif /* LEDMATRIX_H */
