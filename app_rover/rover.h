/**
 * @file rover.h
 * @author koh aiaida (koh@aiaida.jp)
 * @brief uT-Kernel rover control.
 * @date 2026-08-27
 *
 * @copyright Copyright (c) 2026 koh aiaida
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ROVER_H
#define ROVER_H

#include <tk/tkernel.h>

#define ROVER_DRIVE_MIN		(-100)
#define ROVER_DRIVE_MAX		(100)
#define ROVER_JSON_MAX		128
#define ROVER_STATE_JSON_MAX	96

typedef enum {
	ROVER_ST_STOP = 0,
	ROVER_ST_FORWARD,
	ROVER_ST_REVERSE
} rover_state_t;

typedef enum {
	ROVER_EVT_FORWARD = 0,
	ROVER_EVT_STOP,
	ROVER_EVT_REVERSE,
	ROVER_EVT_FORBIDDEN
} rover_event_t;

typedef enum {
	ROVER_MSG_UNKNOWN = 0,
	ROVER_MSG_DRIVE,
	ROVER_MSG_QUERY,
	ROVER_MSG_STOP
} rover_msg_t;

typedef struct {
	rover_msg_t	msg;
	INT		left;
	INT		right;
	INT		duration;
} rover_command_t;

typedef struct {
	rover_state_t	state;
	INT		left;
	INT		right;
	INT		remaining;
} rover_snapshot_t;

/* motor */
EXPORT void rover_motor_init(void);
EXPORT void rover_motor_set(INT left, INT right);

typedef enum {
	ArrowNames_North = 0,
	ArrowNames_NorthEast,
	ArrowNames_East,
	ArrowNames_SouthEast,
	ArrowNames_South,
	ArrowNames_SouthWest,
	ArrowNames_West,
	ArrowNames_NorthWest
} ArrowNames;

EXPORT ER rover_led_start(void);
EXPORT void rover_led_show_asleep(void);
EXPORT void rover_led_show_yes(void);
EXPORT void rover_led_show_no(void);
EXPORT void rover_led_show_stop(void);
EXPORT void rover_led_show_arrow(ArrowNames dir);
EXPORT void rover_led_show_forward(void);
EXPORT void rover_led_show_reverse(void);
EXPORT void rover_led_show_drive(INT left, INT right);

EXPORT ER rover_json_parse(const char *line, rover_command_t *cmd);
EXPORT INT rover_json_format_state(char *buf, INT buflen, const rover_snapshot_t *snap);

EXPORT ER rover_sm_init(void);
EXPORT void rover_sm_apply_drive(INT left, INT right, INT duration);
EXPORT void rover_sm_stop(void);
EXPORT void rover_sm_on_disconnect(void);
EXPORT void rover_sm_tick_1s(void);
EXPORT void rover_sm_get_snapshot(rover_snapshot_t *snap);
EXPORT const char *rover_sm_state_name(rover_state_t st);

#endif /* ROVER_H */
