/**
 * @file rover_motor.c
 * @author koh aiaida (koh@aiaida.jp)
 * @brief 左右モータ駆動 (PWM と方向ピン)
 * @date 2026-08-27
 *
 * @copyright Copyright (c) 2026 koh aiaida
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Left  : P14 dir (P0.01), P13 PWM (P0.17)
 * Right : P16 dir (P1.02), P15 PWM (P0.13)
 * dir 0 = 前進, 1 = 後退
 * drive -100..100 → PWM 0..1023
 *
 * PWM ピン (P13/P15) は GPIO output にしない。Periph 接続時は disconnect。
 * 停止時は PWM を止め、4 ピンすべて GPIO LOW でブレーキする。
 */
#include <tk/tkernel.h>
#include "ledmatrix.h"
#include "rover.h"

#define PWM0_BASE		0x4001C000UL
#define PWM_TASKS_STOP		(PWM0_BASE + 0x004)
#define PWM_TASKS_SEQSTART0	(PWM0_BASE + 0x008)
#define PWM_ENABLE		(PWM0_BASE + 0x500)
#define PWM_MODE		(PWM0_BASE + 0x504)
#define PWM_COUNTERTOP		(PWM0_BASE + 0x508)
#define PWM_PRESCALER		(PWM0_BASE + 0x50C)
#define PWM_DECODER		(PWM0_BASE + 0x510)
#define PWM_LOOP		(PWM0_BASE + 0x514)
#define PWM_SEQ0_PTR		(PWM0_BASE + 0x520)
#define PWM_SEQ0_CNT		(PWM0_BASE + 0x524)
#define PWM_SEQ0_REFRESH	(PWM0_BASE + 0x528)
#define PWM_SEQ0_ENDDELAY	(PWM0_BASE + 0x52C)
#define PWM_PSEL_OUT0		(PWM0_BASE + 0x560)
#define PWM_PSEL_OUT1		(PWM0_BASE + 0x564)
#define PWM_PSEL_OUT2		(PWM0_BASE + 0x568)
#define PWM_PSEL_OUT3		(PWM0_BASE + 0x56C)

#define PWM_PIN_DISCONNECTED	(1u << 31)
#define PWM_DECODER_LOAD_INDIVIDUAL	(2u)	/* 1 word / channel */
#define PWM_COUNTERTOP_VAL	1023u		/* 10 bit (0..1023) */
#define PWM_PRESCALER_DIV16	4u		/* 16 MHz / 16 = 1 MHz → 約 976 Hz */
#define GPIO_PIN_CNF_DISCONNECT	2u		/* input disconnect → peripheral 可 */

/* micro:bit V2 edge connector → nRF52833 */
#define MB_P13_PORT	0
#define MB_P13_PIN	17	/* PWM left  */
#define MB_P14_PORT	0
#define MB_P14_PIN	1	/* dir left  */
#define MB_P15_PORT	0
#define MB_P15_PIN	13	/* PWM right */
#define MB_P16_PORT	1
#define MB_P16_PIN	2	/* dir right */

/* EasyDMA シーケンスは RAM 上・偶数アラインであること */
LOCAL UH pwm_seq[4] __attribute__((aligned(4)));
LOCAL BOOL pwm_running;

/**
 * @brief Clamp a drive value to ROVER_DRIVE_MIN..ROVER_DRIVE_MAX.
 * @param v Drive value
 * @return Clamped drive value
 */
LOCAL INT clamp_drive(INT v)
{
	if (v < ROVER_DRIVE_MIN) {
		return ROVER_DRIVE_MIN;
	}
	if (v > ROVER_DRIVE_MAX) {
		return ROVER_DRIVE_MAX;
	}
	return v;
}

/**
 * @brief Convert the magnitude of a drive value (0..100) to a PWM duty
 *        (0..PWM_COUNTERTOP_VAL).
 * @param v Drive value (the sign is ignored)
 * @return PWM duty
 */
LOCAL UH duty_from_drive(INT v)
{
	INT mag;

	if (v < 0) {
		mag = -v;
	} else {
		mag = v;
	}
	return (UH)((mag * (INT)PWM_COUNTERTOP_VAL) / 100);
}

/**
 * @brief Disconnect the input buffer of a GPIO pin so that the PWM peripheral
 *        can drive the pin.
 * @param port GPIO port (0 or 1)
 * @param pin Pin number within the port
 */
LOCAL void cfg_gpio_disconnect(UW port, UW pin)
{
	if (port == 1) {
		out_w(GPIO(P1, PIN_CNF(pin)), GPIO_PIN_CNF_DISCONNECT);
	} else {
		out_w(GPIO(P0, PIN_CNF(pin)), GPIO_PIN_CNF_DISCONNECT);
	}
}

/**
 * @brief Configure all four motor pins as GPIO outputs and drive them low.
 */
LOCAL void motor_pins_gpio_low(void)
{
	cfg_gpio_output(MB_P14_PORT, MB_P14_PIN);
	cfg_gpio_output(MB_P16_PORT, MB_P16_PIN);
	cfg_gpio_output(MB_P13_PORT, MB_P13_PIN);
	cfg_gpio_output(MB_P15_PORT, MB_P15_PIN);
	out_gpio_pin(MB_P14_PORT, MB_P14_PIN, 0);
	out_gpio_pin(MB_P16_PORT, MB_P16_PIN, 0);
	out_gpio_pin(MB_P13_PORT, MB_P13_PIN, 0);
	out_gpio_pin(MB_P15_PORT, MB_P15_PIN, 0);
}

/**
 * @brief Stop the PWM, release the PWM pins, and drive all four motor pins low
 *        to brake.
 */
LOCAL void motor_brake(void)
{
	out_w(PWM_TASKS_STOP, 1);
	out_w(PWM_ENABLE, 0);
	out_w(PWM_PSEL_OUT0, PWM_PIN_DISCONNECTED);
	out_w(PWM_PSEL_OUT1, PWM_PIN_DISCONNECTED);
	pwm_running = FALSE;
	motor_pins_gpio_low();
}

/**
 * @brief Set the direction pin and the PWM duty of one motor.
 * @param drive Drive value. Positive is forward (DIR 0), negative is reverse
 *              (DIR 1), and 0 gives duty 0.
 * @param dir_port GPIO port of the direction pin
 * @param dir_pin Pin number of the direction pin
 * @param ch PWM channel (0: left, 1: right)
 * @note Only updates pwm_seq. The caller restarts the PWM sequence.
 */
LOCAL void apply_one(INT drive, UW dir_port, UW dir_pin, UW ch)
{
	UH duty;

	if (drive > 0) {
		out_gpio_pin(dir_port, dir_pin, 0);
		duty = duty_from_drive(drive);
	} else if (drive < 0) {
		out_gpio_pin(dir_port, dir_pin, 1);
		duty = duty_from_drive(drive);
	} else {
		out_gpio_pin(dir_port, dir_pin, 0);
		duty = 0;
	}
	pwm_seq[ch] = duty;
}

/**
 * @brief Initialize PWM0: up counter, 10-bit resolution (about 976 Hz), and an
 *        individual duty for each channel. The outputs stay disconnected.
 */
LOCAL void motor_pwm_hw_init(void)
{
	pwm_seq[0] = 0;
	pwm_seq[1] = 0;
	pwm_seq[2] = 0;
	pwm_seq[3] = 0;

	out_w(PWM_ENABLE, 0);
	out_w(PWM_TASKS_STOP, 1);

	out_w(PWM_PSEL_OUT0, PWM_PIN_DISCONNECTED);
	out_w(PWM_PSEL_OUT1, PWM_PIN_DISCONNECTED);
	out_w(PWM_PSEL_OUT2, PWM_PIN_DISCONNECTED);
	out_w(PWM_PSEL_OUT3, PWM_PIN_DISCONNECTED);

	out_w(PWM_MODE, 0);			/* Up */
	out_w(PWM_COUNTERTOP, PWM_COUNTERTOP_VAL);
	out_w(PWM_PRESCALER, PWM_PRESCALER_DIV16);
	out_w(PWM_DECODER, PWM_DECODER_LOAD_INDIVIDUAL);
	out_w(PWM_LOOP, 0);
	out_w(PWM_SEQ0_PTR, (UW)pwm_seq);
	out_w(PWM_SEQ0_CNT, 4);
	out_w(PWM_SEQ0_REFRESH, 0);
	out_w(PWM_SEQ0_ENDDELAY, 0);
}

/**
 * @brief Connect the PWM outputs to P13 (left) and P15 (right) and enable PWM0.
 */
LOCAL void motor_pwm_start(void)
{
	cfg_gpio_output(MB_P14_PORT, MB_P14_PIN);
	cfg_gpio_output(MB_P16_PORT, MB_P16_PIN);
	cfg_gpio_disconnect(MB_P13_PORT, MB_P13_PIN);
	cfg_gpio_disconnect(MB_P15_PORT, MB_P15_PIN);

	out_w(PWM_PSEL_OUT0, GPIO_PIN_MAP(MB_P13_PORT, MB_P13_PIN));
	out_w(PWM_PSEL_OUT1, GPIO_PIN_MAP(MB_P15_PORT, MB_P15_PIN));
	out_w(PWM_ENABLE, 1);
	pwm_running = TRUE;
}

/**
 * @brief Initialize the motors and leave them braked.
 */
EXPORT void rover_motor_init(void)
{
	pwm_running = FALSE;
	motor_pwm_hw_init();
	motor_brake();
}

/**
 * @brief Drive the left and right motors.
 * @param left Left drive value (-100..100; values outside are clamped)
 * @param right Right drive value (-100..100; values outside are clamped)
 * @note When both values are 0, the motors are braked.
 */
EXPORT void rover_motor_set(INT left, INT right)
{
	left = clamp_drive(left);
	right = clamp_drive(right);

	if (left == 0 && right == 0) {
		motor_brake();
		return;
	}

	if (!pwm_running) {
		motor_pwm_start();
	}

	apply_one(left, MB_P14_PORT, MB_P14_PIN, 0);
	apply_one(right, MB_P16_PORT, MB_P16_PIN, 1);
	pwm_seq[2] = 0;
	pwm_seq[3] = 0;

	out_w(PWM_TASKS_STOP, 1);
	out_w(PWM_SEQ0_PTR, (UW)pwm_seq);
	out_w(PWM_TASKS_SEQSTART0, 1);
}
