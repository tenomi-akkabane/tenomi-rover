/**
 * @file ledmatrix.c
 * @author koh aiaida (koh@aiaida.jp)
 * @brief micro:bit V2 LED matrix GPIO and TIMER1 row scan
 * @date 2026-08-27
 *
 * @copyright Copyright (c) 2026 koh aiaida
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <tk/tkernel.h>
#include "ledmatrix.h"

#define LED_TMR(r)		(TIMER1_BASE + TIMER_##r)
#define LED_TMR_INTNO		INTNO(TIMER1_BASE)
#define LED_TMR_INTPRI		6
#define LED_TMR_CLK_MHZ		16
#define LED_SCAN_CYCLE_US	2000

LOCAL const UW col_port[5] = { 0, 0, 0, 1, 0 };
LOCAL const UW col_pin[5]  = { 28, 11, 31, 5, 30 };
LOCAL const UW row_port[5] = { 0, 0, 0, 0, 0 };
LOCAL const UW row_pin[5]  = { 21, 22, 15, 24, 19 };

LOCAL volatile UB led_bitmap[5];
LOCAL volatile UW led_scan_row;

/**
 * @brief Configure a GPIO pin as an output.
 * @param port GPIO port (0 or 1)
 * @param pin Pin number within the port
 */
void cfg_gpio_output(UW port, UW pin)
{
	if (port == 1) {
		out_w(GPIO(P1, PIN_CNF(pin)), 1);
	} else {
		out_w(GPIO(P0, PIN_CNF(pin)), 1);
	}
}

/**
 * @brief Drive a GPIO output pin high or low.
 * @param port GPIO port (0 or 1)
 * @param pin Pin number within the port
 * @param val Non-zero for high, 0 for low
 */
void out_gpio_pin(UW port, UW pin, UW val)
{
	INT port_addr;

	if (port == 1) {
		port_addr = val ? GPIO(P1, OUTSET) : GPIO(P1, OUTCLR);
	} else {
		port_addr = val ? GPIO(P0, OUTSET) : GPIO(P0, OUTCLR);
	}
	out_w(port_addr, (1u << pin));
}

/**
 * @brief Configure the row and column pins of the LED matrix as outputs and
 *        turn all LEDs off.
 */
void InitLedMatrix(void)
{
	UW i;

	cfg_gpio_output(0, 28);
	cfg_gpio_output(0, 11);
	cfg_gpio_output(0, 31);
	cfg_gpio_output(1, 5);
	cfg_gpio_output(0, 30);

	cfg_gpio_output(0, 21);
	cfg_gpio_output(0, 22);
	cfg_gpio_output(0, 15);
	cfg_gpio_output(0, 24);
	cfg_gpio_output(0, 19);

	for (i = 0; i < 5; i++) {
		out_gpio_pin(row_port[i], row_pin[i], 0);
		out_gpio_pin(col_port[i], col_pin[i], 1);
		led_bitmap[i] = 0;
	}
	led_scan_row = 0;
}

/**
 * @brief Set the pattern to show on the LED matrix.
 * @param rows Five row bitmaps from top to bottom. Bit 0 is the leftmost column
 *             and bit 4 the rightmost; the upper 3 bits are ignored.
 * @note The pattern appears from the next scan; this function does not touch
 *       the pins.
 */
void LedMatrixSetBitmap(const UB rows[5])
{
	UW i;

	for (i = 0; i < 5; i++) {
		led_bitmap[i] = rows[i] & 0x1Fu;
	}
}

/**
 * @brief Advance the scan by one row: turn off the current row, output the
 *        column pattern of the next row, and turn that row on.
 * @note Called from the TIMER1 interrupt handler.
 */
void LedMatrixScanTick(void)
{
	UW row;
	UW col;
	UB bits;

	row = led_scan_row;
	out_gpio_pin(row_port[row], row_pin[row], 0);

	row++;
	if (row >= 5) {
		row = 0;
	}
	led_scan_row = row;

	bits = led_bitmap[row];
	for (col = 0; col < 5; col++) {
		out_gpio_pin(col_port[col], col_pin[col], (bits & (1u << col)) ? 0 : 1);
	}
	out_gpio_pin(row_port[row], row_pin[row], 1);
}

/**
 * @brief TIMER1 compare interrupt handler. Clears the event and advances the
 *        LED matrix scan by one row.
 */
void TIMER1_IRQHandler(void)
{
	out_w(LED_TMR(EVENTS_COMPARE(0)), 0);
	(void)in_w(LED_TMR(EVENTS_COMPARE(0)));
	LedMatrixScanTick();
}

/**
 * @brief Start the periodic row scan with TIMER1.
 * @details TIMER1 runs at 16 MHz and raises a compare interrupt every
 *          LED_SCAN_CYCLE_US (2 ms), so the five rows are refreshed every 10 ms.
 * @return E_OK
 */
ER LedMatrixStartScan(void)
{
	UW limit;

	out_w(LED_TMR(TASKS_STOP), 1);
	out_w(LED_TMR(INTENCLR), 0x003f0000);

	out_w(LED_TMR(PRESCALER), 0);
	out_w(LED_TMR(BITMODE), 3);
	out_w(LED_TMR(MODE), 0);

	out_w(LED_TMR(TASKS_CLEAR), 1);
	limit = (UW)(LED_SCAN_CYCLE_US * LED_TMR_CLK_MHZ - 1);
	out_w(LED_TMR(CC(0)), limit);
	out_w(LED_TMR(SHORTS), 0x0001);
	out_w(LED_TMR(EVENTS_COMPARE(0)), 0);

	EnableInt(LED_TMR_INTNO, LED_TMR_INTPRI);
	out_w(LED_TMR(INTENSET), 0x00010000);
	out_w(LED_TMR(TASKS_START), 1);

	return E_OK;
}
