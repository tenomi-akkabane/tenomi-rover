/**
 * @file rover_led.c
 * @author koh aiaida (koh@aiaida.jp)
 * @brief 接続状態・走行状態のアイコンを 5x5 LED に表示する
 * @date 2026-09-22
 *
 * @copyright Copyright (c) 2026 koh aiaida
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <tk/tkernel.h>
#include "ledmatrix.h"
#include "rover.h"

LOCAL const UB ico_asleep[5] = {
	0x00, 0x1B, 0x00, 0x0E, 0x00
};

LOCAL const UB ico_yes[5] = {
	0x10, 0x08, 0x05, 0x02, 0x00
};

LOCAL const UB ico_no[5] = {
	0x11, 0x0A, 0x04, 0x0A, 0x11
};

LOCAL const UB ico_square[5] = {
	0x1F, 0x11, 0x11, 0x11, 0x1F
};

LOCAL const UB ico_north[5] = {
	0x04, 0x0E, 0x15, 0x04, 0x04
};

LOCAL const UB ico_northeast[5] = {
	0x1C, 0x18, 0x14, 0x02, 0x01
};

LOCAL const UB ico_east[5] = {
	0x04, 0x08, 0x1F, 0x08, 0x04
};

LOCAL const UB ico_southeast[5] = {
	0x01, 0x02, 0x14, 0x18, 0x1C
};

LOCAL const UB ico_south[5] = {
	0x04, 0x04, 0x15, 0x0E, 0x04
};

LOCAL const UB ico_southwest[5] = {
	0x10, 0x08, 0x05, 0x03, 0x07
};

LOCAL const UB ico_west[5] = {
	0x04, 0x02, 0x1F, 0x02, 0x04
};

LOCAL const UB ico_northwest[5] = {
	0x07, 0x03, 0x05, 0x08, 0x10
};

LOCAL const UB *const ico_arrow[] = {
	ico_north,
	ico_northeast,
	ico_east,
	ico_southeast,
	ico_south,
	ico_southwest,
	ico_west,
	ico_northwest
};

/**
 * @brief Initialize the LED matrix and start scanning.
 * @return Result of LedMatrixStartScan().
 */
EXPORT ER rover_led_start(void)
{
	InitLedMatrix();
	return LedMatrixStartScan();
}

/**
 * @brief Show the asleep face (just after startup, not connected).
 */
EXPORT void rover_led_show_asleep(void)
{
	LedMatrixSetBitmap(ico_asleep);
}

/**
 * @brief Show the check mark (BLE connected).
 */
EXPORT void rover_led_show_yes(void)
{
	LedMatrixSetBitmap(ico_yes);
}

/**
 * @brief Show the cross mark (BLE disconnected).
 */
EXPORT void rover_led_show_no(void)
{
	LedMatrixSetBitmap(ico_no);
}

/**
 * @brief Show the square (stopped).
 */
EXPORT void rover_led_show_stop(void)
{
	LedMatrixSetBitmap(ico_square);
}

/**
 * @brief Show an arrow icon.
 * @param dir Arrow direction. Out-of-range values are ignored.
 */
EXPORT void rover_led_show_arrow(ArrowNames dir)
{
	if ((UINT)dir > (UINT)ArrowNames_NorthWest) {
		return;
	}
	LedMatrixSetBitmap(ico_arrow[dir]);
}

/**
 * @brief Show the up arrow (forward).
 */
EXPORT void rover_led_show_forward(void)
{
	rover_led_show_arrow(ArrowNames_North);
}

/**
 * @brief Show the down arrow (reverse).
 */
EXPORT void rover_led_show_reverse(void)
{
	rover_led_show_arrow(ArrowNames_South);
}

/**
 * @brief Show the arrow that matches a driving command.
 * @param left Left drive value
 * @param right Right drive value
 * @details When both values have the same sign:
 *          - Equal magnitudes show the up arrow (forward) or the down arrow
 *            (reverse).
 *          - |left| > |right| (turning right) shows the north-west arrow when
 *            moving forward and the south-west arrow when reversing.
 *          - |right| > |left| (turning left) shows the north-east arrow when
 *            moving forward and the south-east arrow when reversing.
 *          The diagonal arrows are drawn as seen by an operator facing the Rover.
 *          Any other combination shows the square.
 */
EXPORT void rover_led_show_drive(INT left, INT right)
{
	INT al;
	INT ar;

	al = (left < 0) ? -left : left;
	ar = (right < 0) ? -right : right;

	if (left > 0 && right > 0) {
		if (al > ar) {
			rover_led_show_arrow(ArrowNames_NorthWest);	/* 右折 */
		} else if (ar > al) {
			rover_led_show_arrow(ArrowNames_NorthEast);	/* 左折 */
		} else {
			rover_led_show_arrow(ArrowNames_North);
		}
		return;
	}
	if (left < 0 && right < 0) {
		if (al > ar) {
			rover_led_show_arrow(ArrowNames_SouthWest);
		} else if (ar > al) {
			rover_led_show_arrow(ArrowNames_SouthEast);
		} else {
			rover_led_show_arrow(ArrowNames_South);
		}
		return;
	}
	rover_led_show_stop();
}
