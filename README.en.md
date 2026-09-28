# tenomi-rover

[日本語](README.md) | **English**

The application layer of the Rover (a micro:bit v2-based crawler that drives according to hand gestures), one of the components of TENOMI (an AI robot that needs no words).

The Rover is implemented as **a BLE Peripheral running on μT-Kernel 3.0**. It receives driving commands over the BLE Nordic UART Service (NUS) and drives the left and right motors.

## Where it fits in TENOMI

```
[STM32N6570-DK]  μT-Kernel 3.0 + NPU      … gesture recognition
      │  USB serial
      ▼
[Bridge]  Raspberry Pi                     … relays USB serial ⇄ BLE
      │  BLE (Nordic UART Service): one line of JSON
      ▼
[Rover]  micro:bit v2 + μT-Kernel 3.0      … driving (this repository)
      forward / reverse / turn right / turn left / stop
```

The Rover does not care what it is connected to. It drives by interpreting **only newline-delimited JSON** that arrives over NUS.

## Software components

The Rover firmware is built by combining the following three components.

| Component | Provided by | Role |
|-----------|-------------|------|
| μT-Kernel 3.0 (micro:bit version) | Personal Media Corporation (hereafter PMC) | Real-time OS and device infrastructure |
| [benus](https://github.com/koh523/benus) | Separate repository | Driver that makes BLE NUS available through the μT-Kernel device API |
| app_rover | This repository | JSON parsing, driving state management, motors, LED display |

This repository contains only app_rover. **Cloning it alone is not enough to build.** Apply benus to PMC's μT-Kernel 3.0, copy the sources of this repository into it, and then build (see [Build and flash](#build-and-flash)).

BLE is handled through benus with the standard μT-Kernel device API.

```c
dd = tk_opn_dev("blua", TD_UPDATE);   /* start SoftDevice and advertising */
tk_srea_dev(dd, 0, &data, 1, &asize); /* receive over BLE */
tk_swri_dev(dd, 0, buf, n, &asize);   /* send over BLE (Notify) */
```

## How it works

Driving control is built by dividing three processes of different nature into μT-Kernel tasks.

| Task | Priority | Role |
|------|----------|------|
| Receive (`task_rx`) | High | Assembles the bytes received over BLE up to a newline and parses the JSON |
| Connection monitor (`task_conn`) | Middle | Watches the BLE connection state and stops immediately when a disconnection is detected |
| Timer (`task_timer`) | Low | Decrements the remaining driving time every second and stops automatically when it runs out |

- There are three driving states: STOP / FORWARD / REVERSE
- The driving state is protected by a mutex, so it stays consistent even when the three tasks operate on it at the same time
- Because connection monitoring is a separate task, stopping on disconnection is not delayed even if receive processing is stalled
- The current state is shown on the 5×5 LED matrix (see [Checking operation](#checking-operation))

## Repository contents

| File | Contents |
|------|----------|
| `app_rover/app_main.c` | Entry point. Opens the BLE device, creates the three tasks, and implements the receive, connection monitor, and timer processing |
| `app_rover/rover.h` | Types, constants, and function declarations shared within app_rover |
| `app_rover/rover_json.c` | Parsing of newline-delimited JSON and generation of the JSON that reports the state |
| `app_rover/rover_sm.c` | Driving state management (STOP / FORWARD / REVERSE) and driving for a specified time |
| `app_rover/rover_motor.c` | Left and right motor drive (PWM and direction pins, braking on stop) |
| `app_rover/rover_led.c` | Definition and display of the state icons |
| `app_rover/ledmatrix.c` / `app_rover/ledmatrix.h` | GPIO setup and row scanning of the 5×5 LED matrix |

## Requirements

### Hardware

| Item | Notes |
|------|-------|
| BBC micro:bit **v2** (nRF52833) | **Does not work on v1** |
| Crawler (chassis with left and right motors) | Wire it as shown in the table below |
| Power supply (battery box, etc.) | Powers the micro:bit and the motors |
| USB cable | Connects the PC and the micro:bit to flash the firmware |

### Motor wiring (micro:bit v2 edge connector)

| Side | DIR (direction) | PWM (speed) |
|------|-----------------|-------------|
| Left | P14 | P13 |
| Right | P16 | P15 |

- DIR `0` means forward, `1` means reverse
- The driving command value `-100` to `100` is mapped to PWM `0` to `1023`
- On stop, PWM is stopped and all four pins are set LOW to brake

### Software for building and flashing

| Item | Purpose |
|------|---------|
| GNU Arm Embedded Toolchain | Build |
| make (on Windows, e.g. xPack Windows Build Tools) | Build |
| Python 3.8 or later | benus patch script |
| pyocd | Flashing the micro:bit |

For installation, follow [“micro:bitでμT-Kernel 3.0を動かそう” (Let's run μT-Kernel 3.0 on micro:bit), Part 2 (preparing the development tools and compiling)](https://www.t-engine4u.com/info/mbit/2.html) (the page is in Japanese). The steps below assume a Git Bash environment.

## Build and flash

Apply benus to PMC's μT-Kernel 3.0, add app_rover to it, and build.

```
  PMC's μT-Kernel 3.0 (mtkernel_3)          ← root of the build
            +
  benus (BLE NUS driver, SoftDevice)       ← patched in to live alongside
            +
  app_rover (this repository)
```

Choose an empty directory as the working directory.

### Step 1 — Extract μT-Kernel 3.0

Obtain `362_mbit_mtk3.zip` from the [“micro:bitでμT-Kernel 3.0を動かそう”](https://www.t-engine4u.com/info/mbit/2.html) page and extract it.

Check that `kernel/` `lib/` `device/` `include/` `config/` `etc/` `app_sample/` `build_make/` exist under the extracted `mtkernel_3`. Below, this directory is written as `<mtkernel_3>`.

### Step 2 — Get and apply benus

Clone benus **outside** `mtkernel_3` (do not clone it inside `mtkernel_3`).

```bash
git clone https://github.com/koh523/benus.git
python benus/patch/apply.py --mtk3 <mtkernel_3>
```

This places the BLE driver, SoftDevice, and linker script, and patches the kernel side. For details, see the benus README.

### Step 3 — Place app_rover

Clone this repository and copy the sources in `app_rover/` of the cloned directory to `<mtkernel_3>/app_rover/`.

```bash
git clone https://github.com/tenomi-akkabane/tenomi-rover.git

MTK3=<mtkernel_3>
APP_SRC=tenomi-rover/app_rover   # app_rover under the cloned directory

mkdir -p "$MTK3/app_rover"
cp "$APP_SRC"/*.c "$APP_SRC"/*.h "$MTK3/app_rover/"
```

Switch the build target to app_rover. Set the application in `<mtkernel_3>/build_make/makefile` as follows.

```
APP = app_rover
```

The first time, reuse the build file of `app_sample` placed by benus.

```bash
mkdir -p "$MTK3/build_make/mtkernel_3/app_rover"
cp "$MTK3/build_make/mtkernel_3/app_sample/subdir.mk" \
   "$MTK3/build_make/mtkernel_3/app_rover/subdir.mk"
sed -i 's/app_sample/app_rover/g' \
   "$MTK3/build_make/mtkernel_3/app_rover/subdir.mk"
```

### Step 4 — Build

```bash
cd <mtkernel_3>/build_make
make
```

On success, `mtkernel_3.elf` is generated in the same directory.

### Step 5 — Flash the micro:bit

Connect the micro:bit v2 to the PC with a USB cable. If a program made with MakeCode or similar remains on the micro:bit, erase it first.

```bash
pyocd erase --mass
```

Then flash.

```bash
pyocd load -t nrf52 mtkernel_3.elf
```

When flashing succeeds, the "asleep" icon appears on the 5×5 LED matrix.

> If you modify the sources, **copy them again** from `tenomi-rover/app_rover/` to `<mtkernel_3>/app_rover/` before running `make`. Symbolic links do not keep them in sync.

## Checking operation

### Power-on order

**Power on the Rover first, then start the Bridge.**

1. Power on the Rover → the LED shows the "asleep" icon (startup complete, BLE advertising started)
2. Start the Bridge → the Bridge finds the Rover and connects automatically
3. When the connection is established, the LED changes to a check mark

The Rover advertises with the name `micro:bit2_UART`. No pairing is required. Only one device can be connected at a time.

### LED indicators

The Rover shows its current state on the 5×5 LED matrix.

**Connection state**

| Display | Meaning |
|---------|---------|
| Asleep face | Just after startup. BLE not connected (advertising) |
| ✓ (check mark) | BLE connection established. Ready to accept driving commands |
| ✕ | BLE disconnection detected. The motors are stopped at the same time |

**Driving state**

| Display | Meaning |
|---------|---------|
| □ | Stopped |
| ↑ | Forward (both sides at the same speed) |
| ↓ | Reverse (both sides at the same speed) |
| ↖ | Turning right while moving forward (left wheel faster) |
| ↗ | Turning left while moving forward (right wheel faster) |
| ↙ | Turning while reversing (left wheel faster) |
| ↘ | Turning while reversing (right wheel faster) |

The diagonal arrows while turning are shown **as seen by an operator facing the Rover**. When the Rover itself turns right, it looks like a left-pointing arrow (↖) to an operator standing in front of it.

## Troubleshooting

| Symptom | What to check |
|---------|---------------|
| The build fails | Whether benus has been applied (Step 2). Whether `APP = app_rover` is set. Whether `subdir.mk` has been prepared (Step 3) |
| Flashing fails | Erase with `pyocd erase --mass`, then run `pyocd load` |
| Nothing appears on the LED | Whether the power is on. Whether flashing has completed |
| The LED stays on the "asleep" icon | Whether the Bridge is running. Whether the connection target name set on the Bridge matches `micro:bit2_UART` |
| The LED shows ✕ | BLE has been disconnected. Check the distance between the Rover and the Bridge, and the battery level |
| Connected but does not drive | Check the battery level and the motor wiring. If the LED changes to an arrow, the driving command has arrived |
| Does not switch directly from forward to reverse | This is by design. To protect the gears, a stop is required between forward and reverse |

## License

The source code in this repository is provided under the [Apache License 2.0](LICENSE).

μT-Kernel 3.0 (micro:bit version) and benus, which are used for the build, are not included in this repository. Follow the terms of their respective providers. For details, see [NOTICE.en](NOTICE.en).

## Related links

- benus (BLE NUS driver): https://github.com/koh523/benus
- “micro:bitでμT-Kernel 3.0を動かそう” (PMC, in Japanese): https://www.t-engine4u.com/info/mbit/2.html
