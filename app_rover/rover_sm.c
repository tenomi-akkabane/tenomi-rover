/**
 * @file rover_sm.c
 * @author koh aiaida (koh@aiaida.jp)
 * @brief 走行状態機械 (FORWARD / STOP / REVERSE) とタイムド駆動
 * @date 2026-08-27
 *
 * @copyright Copyright (c) 2026 koh aiaida
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <tk/tkernel.h>
#include "rover.h"

LOCAL rover_state_t current_state;
LOCAL INT current_left;
LOCAL INT current_right;
LOCAL INT remaining_sec;
LOCAL ID idMtx;

LOCAL const char *const state_name[3] = {
	"STOP",
	"FORWARD",
	"REVERSE"
};

/**
 * @brief Classify a driving command.
 * @param left Left drive value
 * @param right Right drive value
 * @return ROVER_EVT_FORWARD if both are positive, ROVER_EVT_REVERSE if both are
 *         negative, ROVER_EVT_STOP if both are 0, and ROVER_EVT_FORBIDDEN
 *         otherwise (opposite signs, or only one wheel non-zero).
 */
LOCAL rover_event_t determine_event(INT left, INT right)
{
	if (left > 0 && right > 0) {
		return ROVER_EVT_FORWARD;
	}
	if (left == 0 && right == 0) {
		return ROVER_EVT_STOP;
	}
	if (left < 0 && right < 0) {
		return ROVER_EVT_REVERSE;
	}
	return ROVER_EVT_FORBIDDEN;
}

/**
 * @brief Lock the state machine mutex (waits forever).
 */
LOCAL void lock_sm(void)
{
	(void)tk_loc_mtx(idMtx, TMO_FEVR);
}

/**
 * @brief Unlock the state machine mutex.
 */
LOCAL void unlock_sm(void)
{
	(void)tk_unl_mtx(idMtx);
}

/**
 * @brief Enter the STOP state, brake the motors, and show the square.
 * @note The caller must hold the state machine mutex.
 */
LOCAL void do_stop_locked(void)
{
	current_state = ROVER_ST_STOP;
	current_left = 0;
	current_right = 0;
	remaining_sec = 0;
	rover_motor_set(0, 0);
	rover_led_show_stop();
}

/**
 * @brief Create the state machine mutex and start in the STOP state.
 * @return E_OK on success, or the error code of tk_cre_mtx().
 * @note Call after rover_motor_init().
 */
EXPORT ER rover_sm_init(void)
{
	T_CMTX cmtx;

	cmtx.exinf = 0;
	cmtx.mtxatr = TA_INHERIT;
	cmtx.ceilpri = 0;
	idMtx = tk_cre_mtx(&cmtx);
	if (idMtx < E_OK) {
		return idMtx;
	}

	current_state = ROVER_ST_STOP;
	current_left = 0;
	current_right = 0;
	remaining_sec = 0;
	rover_motor_set(0, 0);
	return E_OK;
}

/**
 * @brief Name of a driving state, used in the state reply.
 * @param st Driving state
 * @return "STOP", "FORWARD", or "REVERSE". Out-of-range values return "STOP".
 */
EXPORT const char *rover_sm_state_name(rover_state_t st)
{
	if ((UINT)st > (UINT)ROVER_ST_REVERSE) {
		return "STOP";
	}
	return state_name[st];
}

/**
 * @brief Stop driving (stop command).
 */
EXPORT void rover_sm_stop(void)
{
	lock_sm();
	do_stop_locked();
	unlock_sm();
}

/**
 * @brief Stop driving because the BLE connection was lost, and show the cross
 *        mark.
 */
EXPORT void rover_sm_on_disconnect(void)
{
	lock_sm();
	do_stop_locked();
	rover_led_show_no();
	unlock_sm();
}

/**
 * @brief Check a driving command and apply it only if it is acceptable.
 * @param left Left drive value (-100..100)
 * @param right Right drive value (-100..100)
 * @param duration Driving time in seconds. 0 means no automatic stop.
 * @note The following commands are ignored and the current state is kept:
 *       opposite signs or only one wheel non-zero, and reverse while moving
 *       forward (and vice versa). When both values are 0, the Rover stops.
 */
EXPORT void rover_sm_apply_drive(INT left, INT right, INT duration)
{
	rover_event_t ev;

	lock_sm();
	ev = determine_event(left, right);

	if (ev == ROVER_EVT_FORBIDDEN) {
		unlock_sm();
		return;
	}

	if (current_state == ROVER_ST_FORWARD && ev == ROVER_EVT_REVERSE) {
		unlock_sm();
		return;
	}
	if (current_state == ROVER_ST_REVERSE && ev == ROVER_EVT_FORWARD) {
		unlock_sm();
		return;
	}

	if (ev == ROVER_EVT_STOP) {
		do_stop_locked();
		unlock_sm();
		return;
	}

	current_left = left;
	current_right = right;
	remaining_sec = duration;

	if (ev == ROVER_EVT_FORWARD) {
		current_state = ROVER_ST_FORWARD;
	} else {
		current_state = ROVER_ST_REVERSE;
	}
	rover_led_show_drive(left, right);
	rover_motor_set(left, right);
	unlock_sm();
}

/**
 * @brief Count down the remaining driving time by one second, and stop when it
 *        reaches 0.
 * @note Called once per second by the timer task. Does nothing while the
 *       remaining time is 0.
 */
EXPORT void rover_sm_tick_1s(void)
{
	lock_sm();
	if (remaining_sec > 0) {
		remaining_sec--;
		if (remaining_sec == 0) {
			do_stop_locked();
		}
	}
	unlock_sm();
}

/**
 * @brief Copy the current state, drive values, and remaining time.
 * @param snap Receives the snapshot
 */
EXPORT void rover_sm_get_snapshot(rover_snapshot_t *snap)
{
	lock_sm();
	snap->state = current_state;
	snap->left = current_left;
	snap->right = current_right;
	snap->remaining = remaining_sec;
	unlock_sm();
}
