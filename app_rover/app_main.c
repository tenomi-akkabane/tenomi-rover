/**
 * @file app_main.c
 * @author koh aiaida (koh@aiaida.jp)
 * @brief Rover usermain — BLE NUS (blua) で JSON 駆動する uT-Kernel アプリ
 * @date 2026-08-27
 *
 * @copyright Copyright (c) 2026 koh aiaida
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * @details
 * 受信・接続監視・タイマーの 3 タスクで、blenus API (tk_opn_dev / TDN_BLENUS_*) 経由の
 * query / stop / drive を処理する。
 */
#include <tk/tkernel.h>
#include <tk/device.h>
#include <tm/tmonitor.h>
#include "rover.h"

#define BLUA_DEVNM	((const UB *)"blua")
#define BLUA_WAIT_TICK	(10)
#define RX_LINE_MAX	ROVER_JSON_MAX

LOCAL ID ddBlua;
LOCAL ID tidRx;
LOCAL ID tidConn;
LOCAL ID tidTimer;

LOCAL void task_rx(INT stacd, void *exinf);
LOCAL void task_conn(INT stacd, void *exinf);
LOCAL void task_timer(INT stacd, void *exinf);

LOCAL T_CTSK ctsk_rx = {
	.itskpri = 10,
	.stksz = 2048,
	.task = task_rx,
	.tskatr = TA_HLNG | TA_RNG0,
};

LOCAL T_CTSK ctsk_conn = {
	.itskpri = 12,
	.stksz = 2048,
	.task = task_conn,
	.tskatr = TA_HLNG | TA_RNG0,
};

LOCAL T_CTSK ctsk_timer = {
	.itskpri = 15,
	.stksz = 2048,
	.task = task_timer,
	.tskatr = TA_HLNG | TA_RNG0,
};

/**
 * @brief Read a boolean attribute of the blua device.
 * @param dd Device descriptor
 * @param atr TDN_BLENUS_CONN or TDN_BLENUS_COMM
 * @return TRUE if the attribute is true. FALSE if it is false or the read fails.
 */
LOCAL BOOL blua_get_bool(ID dd, INT atr)
{
	BOOL val = FALSE;
	SZ asize;
	ER err;

	err = tk_srea_dev(dd, atr, &val, sizeof(val), &asize);
	if (err < E_OK) {
		return FALSE;
	}
	return val ? TRUE : FALSE;
}

/**
 * @brief Check whether a central is connected and NUS Notify is enabled.
 * @param dd Device descriptor
 * @return TRUE if data can be sent to the central.
 */
LOCAL BOOL blua_ready(ID dd)
{
	return blua_get_bool(dd, TDN_BLENUS_CONN)
	    && blua_get_bool(dd, TDN_BLENUS_COMM);
}

/**
 * @brief Send data to the central over NUS (Notify).
 * @param dd Device descriptor
 * @param buf Data to send
 * @param len Number of bytes to send
 * @return Result of tk_swri_dev().
 */
LOCAL ER blua_write(ID dd, const void *buf, UINT len)
{
	SZ asize;

	return tk_swri_dev(dd, 0, buf, (SZ)len, &asize);
}

/**
 * @brief Send the current driving state to the central as a state JSON line.
 * @note Does nothing if the central is not ready to receive (see blua_ready()).
 */
LOCAL void send_state_response(void)
{
	rover_snapshot_t snap;
	char buf[ROVER_STATE_JSON_MAX];
	INT n;

	if (!blua_ready(ddBlua)) {
		return;
	}
	rover_sm_get_snapshot(&snap);
	n = rover_json_format_state(buf, (INT)sizeof(buf), &snap);
	if (n > 0) {
		(void)blua_write(ddBlua, buf, (UINT)n);
#if USE_TMONITOR
		tm_putstring((UB *)buf);
#endif
	}
}

/**
 * @brief Parse one received JSON line and execute the command.
 * @param line NUL-terminated line without the trailing newline
 * @details
 * - stop: stops driving and replies with the state.
 * - drive: applies the driving command and replies with the state.
 *   A non-zero drive command is ignored while not connected.
 * - query: only replies with the state.
 * Lines that cannot be parsed are discarded.
 */
LOCAL void handle_line(const char *line)
{
	rover_command_t cmd;
	ER err;

	err = rover_json_parse(line, &cmd);
	if (err < E_OK) {
#if USE_TMONITOR
		tm_putstring((UB *)"JSON parse fail\n");
#endif
		return;
	}
	if (cmd.msg == ROVER_MSG_STOP) {
#if USE_TMONITOR
		tm_putstring((UB *)"CMD stop\n");
#endif
		rover_sm_stop();
		send_state_response();
	} else if (cmd.msg == ROVER_MSG_DRIVE) {
#if USE_TMONITOR
		tm_putstring((UB *)"CMD drive\n");
#endif
		if (!blua_get_bool(ddBlua, TDN_BLENUS_CONN) &&
		    (cmd.left != 0 || cmd.right != 0)) {
			return;
		}
		rover_sm_apply_drive(cmd.left, cmd.right, cmd.duration);
		send_state_response();
	} else if (cmd.msg == ROVER_MSG_QUERY) {
#if USE_TMONITOR
		tm_putstring((UB *)"CMD query\n");
#endif
		send_state_response();
	}
}

/**
 * @brief Receive task. Assembles the bytes received over BLE into lines and
 *        passes each line to handle_line().
 * @param stacd Task start code (unused)
 * @param exinf Extended information (unused)
 * @note CR is ignored and LF ends a line. If a line reaches RX_LINE_MAX bytes,
 *       the bytes received so far are discarded. While not connected, the task
 *       only polls the connection state.
 */
LOCAL void task_rx(INT stacd, void *exinf)
{
	char line[RX_LINE_MAX];
	UINT n = 0;
	UB data;
	SZ asize;
	ER err;

	(void)stacd;
	(void)exinf;

	while (1) {
		if (!blua_get_bool(ddBlua, TDN_BLENUS_CONN)) {
			tk_dly_tsk(BLUA_WAIT_TICK);
			continue;
		}
		err = tk_srea_dev(ddBlua, 0, &data, 1, &asize);
		if (err < E_OK || asize == 0) {
			tk_dly_tsk(10);
			continue;
		}
		if (data == '\r') {
			continue;
		}
		if (data == '\n') {
			if (n > 0) {
				line[n] = '\0';
				handle_line(line);
				n = 0;
			}
			continue;
		}
		if (n + 1 < RX_LINE_MAX) {
			line[n] = (char)data;
			n++;
		} else {
			n = 0;
		}
	}
}

/**
 * @brief Connection monitor task. Polls the BLE connection state every
 *        BLUA_WAIT_TICK ms.
 * @param stacd Task start code (unused)
 * @param exinf Extended information (unused)
 * @details Shows the check mark when a connection is established. When the
 *          connection is lost, calls rover_sm_on_disconnect() to stop the motors
 *          and show the cross mark. Because this runs as a separate task, the
 *          stop on disconnection is not delayed by the receive processing.
 */
LOCAL void task_conn(INT stacd, void *exinf)
{
	BOOL prev;
	BOOL now;

	(void)stacd;
	(void)exinf;

	prev = blua_get_bool(ddBlua, TDN_BLENUS_CONN);
	while (1) {
		now = blua_get_bool(ddBlua, TDN_BLENUS_CONN);
		if (now && !prev) {
			rover_led_show_yes();
		} else if (!now && prev) {
			rover_sm_on_disconnect();
		}
		prev = now;
		tk_dly_tsk(BLUA_WAIT_TICK);
	}
}

/**
 * @brief Timer task. Calls rover_sm_tick_1s() once per second to count down the
 *        remaining driving time.
 * @param stacd Task start code (unused)
 * @param exinf Extended information (unused)
 */
LOCAL void task_timer(INT stacd, void *exinf)
{
	(void)stacd;
	(void)exinf;

	while (1) {
		tk_dly_tsk(1000);
		rover_sm_tick_1s();
	}
}

/**
 * @brief Application entry point called by uT-Kernel.
 * @details Starts the LED matrix, initializes the motors and the state machine,
 *          and opens the blua device (which starts SoftDevice and advertising).
 *          Then creates and starts the receive, connection monitor, and timer
 *          tasks, shows the asleep icon, and sleeps forever.
 * @return Does not return on success. On failure, returns the error code after
 *         deleting the tasks created so far and closing the device.
 */
EXPORT INT usermain(void)
{
	T_RVER rver;
	ER err;

#if USE_TMONITOR
	tm_putstring((UB *)"Start Rover.\n");
#endif
	tk_ref_ver(&rver);

	err = rover_led_start();
	if (err < E_OK) {
		goto err_ret;
	}

	rover_motor_init();
	err = rover_sm_init();
	if (err < E_OK) {
		goto err_ret;
	}

	ddBlua = tk_opn_dev(BLUA_DEVNM, TD_UPDATE);
	if (ddBlua < E_OK) {
		err = ddBlua;
		goto err_ret;
	}

	tidRx = tk_cre_tsk(&ctsk_rx);
	if (tidRx < E_OK) {
		err = tidRx;
		goto err_cls;
	}
	tidConn = tk_cre_tsk(&ctsk_conn);
	if (tidConn < E_OK) {
		err = tidConn;
		goto err_del_rx;
	}
	tidTimer = tk_cre_tsk(&ctsk_timer);
	if (tidTimer < E_OK) {
		err = tidTimer;
		goto err_del_conn;
	}

	tk_sta_tsk(tidRx, 0);
	tk_sta_tsk(tidConn, 0);
	tk_sta_tsk(tidTimer, 0);

	rover_led_show_asleep();

	tk_slp_tsk(TMO_FEVR);
	return 0;

err_del_conn:
	tk_del_tsk(tidConn);
err_del_rx:
	tk_del_tsk(tidRx);
err_cls:
	(void)tk_cls_dev(ddBlua, 0);
err_ret:
	return err;
}
