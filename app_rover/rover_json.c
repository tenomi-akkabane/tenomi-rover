/**
 * @file rover_json.c
 * @author koh aiaida (koh@aiaida.jp)
 * @brief BLE UART 改行区切り JSON の簡易パーサ / 状態応答生成
 * @date 2026-08-27
 *
 * @copyright Copyright (c) 2026 koh aiaida
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include "rover.h"

/**
 * @brief Skip spaces and tabs.
 * @param s String
 * @return Pointer to the first character that is neither a space nor a tab.
 */
LOCAL const char *skip_ws(const char *s)
{
	while (*s == ' ' || *s == '\t') {
		s++;
	}
	return s;
}

/**
 * @brief Length of a NUL-terminated string.
 * @param s String
 * @return Number of characters, excluding the terminating NUL.
 */
LOCAL UINT cstr_len(const char *s)
{
	UINT n = 0;

	while (s[n] != '\0') {
		n++;
	}
	return n;
}

/**
 * @brief Find a key in a JSON object and return where its value starts.
 * @param json JSON text (one line)
 * @param key Key name without quotes
 * @return Pointer to the first character of the value (after the colon and any
 *         whitespace), or 0 if the key is not found.
 * @note A simple search that does not handle nesting or escapes. It is meant for
 *       the flat, one-line messages that the Rover receives.
 */
LOCAL const char *json_after_colon(const char *json, const char *key)
{
	const char *p = json;
	UINT klen = cstr_len(key);

	while (*p != '\0') {
		if (*p == '"') {
			UINT i;

			p++;
			for (i = 0; i < klen; i++) {
				if (p[i] != key[i]) {
					break;
				}
			}
			if (i == klen && p[klen] == '"') {
				p = skip_ws(p + klen + 1);
				if (*p == ':') {
					return skip_ws(p + 1);
				}
			}
		} else {
			p++;
		}
	}
	return 0;
}

/**
 * @brief Parse a decimal integer with an optional sign.
 * @param s String that starts with the number (leading whitespace is skipped)
 * @param out Receives the value on success
 * @return 1 on success, 0 if no digit is found.
 */
LOCAL INT json_parse_int(const char *s, INT *out)
{
	INT sign = 1;
	INT v = 0;
	INT digits = 0;

	s = skip_ws(s);
	if (*s == '-') {
		sign = -1;
		s++;
	} else if (*s == '+') {
		s++;
	}
	while (*s >= '0' && *s <= '9') {
		v = v * 10 + (*s - '0');
		s++;
		digits++;
	}
	if (digits == 0) {
		return 0;
	}
	*out = sign * v;
	return 1;
}

/**
 * @brief Parse a JSON string value (escapes are not handled).
 * @param s String that starts with the opening double quote (leading whitespace
 *          is skipped)
 * @param dst Buffer that receives the value without quotes
 * @param dstsz Size of dst. Longer values are truncated.
 * @return 1 on success, 0 if s is not a properly closed string.
 */
LOCAL INT json_parse_str(const char *s, char *dst, UINT dstsz)
{
	UINT n = 0;

	s = skip_ws(s);
	if (*s != '"') {
		return 0;
	}
	s++;
	while (*s != '\0' && *s != '"') {
		if (n + 1 < dstsz) {
			dst[n] = *s;
			n++;
		}
		s++;
	}
	if (*s != '"') {
		return 0;
	}
	dst[n] = '\0';
	return 1;
}

/**
 * @brief Compare two strings for equality.
 * @param a String
 * @param b String
 * @return 1 if equal, 0 otherwise.
 */
LOCAL INT str_eq(const char *a, const char *b)
{
	while (*a != '\0' && *b != '\0') {
		if (*a != *b) {
			return 0;
		}
		a++;
		b++;
	}
	return (*a == '\0' && *b == '\0') ? 1 : 0;
}

/**
 * @brief Parse one JSON command line received from the central.
 * @param line NUL-terminated JSON line
 * @param cmd Receives the parsed command. Fields missing from the line are set
 *            to 0.
 * @return E_OK on success. E_PAR if the "type" key is missing or its value is
 *         unknown.
 * @details Supported types are "drive" (with "left", "right", and "duration"),
 *          "query", and "stop". A negative "duration" is treated as 0.
 */
EXPORT ER rover_json_parse(const char *line, rover_command_t *cmd)
{
	const char *p;
	char type[12];
	INT have;

	cmd->msg = ROVER_MSG_UNKNOWN;
	cmd->left = 0;
	cmd->right = 0;
	cmd->duration = 0;

	p = json_after_colon(line, "type");
	if (p == 0) {
		return E_PAR;
	}
	if (!json_parse_str(p, type, sizeof(type))) {
		return E_PAR;
	}

	if (str_eq(type, "drive")) {
		cmd->msg = ROVER_MSG_DRIVE;
		p = json_after_colon(line, "left");
		if (p != 0) {
			(void)json_parse_int(p, &cmd->left);
		}
		p = json_after_colon(line, "right");
		if (p != 0) {
			(void)json_parse_int(p, &cmd->right);
		}
		p = json_after_colon(line, "duration");
		if (p != 0) {
			have = json_parse_int(p, &cmd->duration);
			if (have && cmd->duration < 0) {
				cmd->duration = 0;
			}
		}
		return E_OK;
	}
	if (str_eq(type, "query")) {
		cmd->msg = ROVER_MSG_QUERY;
		return E_OK;
	}
	if (str_eq(type, "stop")) {
		cmd->msg = ROVER_MSG_STOP;
		return E_OK;
	}
	return E_PAR;
}

/**
 * @brief Format the state reply as one JSON line with a trailing newline,
 *        for example {"type":"state","state":"STOP","left":0,"right":0,"remaining":0}.
 * @param buf Output buffer
 * @param buflen Size of buf
 * @param snap State to report
 * @return Number of characters written (excluding NUL), or 0 if buf is too
 *         small.
 */
EXPORT INT rover_json_format_state(char *buf, INT buflen, const rover_snapshot_t *snap)
{
	INT n;

	if (buflen < 8) {
		return 0;
	}
	n = tm_sprintf((UB *)buf,
		(const UB *)"{\"type\":\"state\",\"state\":\"%s\",\"left\":%d,\"right\":%d,\"remaining\":%d}\n",
		rover_sm_state_name(snap->state),
		snap->left,
		snap->right,
		snap->remaining);
	if (n < 0 || n >= buflen) {
		buf[buflen - 1] = '\0';
		return 0;
	}
	return n;
}
