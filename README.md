
# kbWinder

This is my approach to pickup coil (or spool of thread ;P ) winder firmware.

I used [original sandy9159 hardware](https://github.com/sandy9159/DIY-Arduino-based-Guitar-Pickup-Coil-Winder), but with semi-pro firmware.

## Features:
- get rid of Nextion 1990-style controller ;)
- Presets for winding (with load/save/delete/export)
- Tasks
- Configuration (with motor start/max/accel rpm, screw width and more)
- Serial interface with lots of commands
- WWW/JS interface with most of them
- Jog both stepper motors
- Precise seek zero with limit switch 
- Driving motors with no external libraries
- Acceleration and deceleration for both motors
- Start point for presets
- (...etc...)



## Commands:
<pre>Movement: W, T, GOTO, SEEK ZERO
Control: START, STOP, PAUSE, RESUME
Presets: SAVE, LOAD, DELETE, EXPORT, IMPORT
Settings: GET MACHINE/PRESET/RUNTIME/..., SET ..., FACTORY
Info: STATUS, HELP, LONGHELP, SETHELP</pre>

## Status:

<pre>--- MACHINE STATUS ---
Current Task: WINDING (1 in queue)
Progress: 2.2%
Current RPM: 120.00
Abs Position: 2.21 mm
----------------------</pre>

## Settings:
<pre>MACHINE SETTINGS:
[MACHINE] SCREW PITCH: 1.000
[MACHINE] WINDER STEPS PER REV: 1600
[MACHINE] TRAVERSE STEPS PER REV: 1600
[MACHINE] WINDER MAX SPEED: 120
[MACHINE] TRAVERSE MAX SPEED: 150
[MACHINE] WINDER START SPEED: 40
[MACHINE] TRAVERSE START SPEED: 40
[MACHINE] WINDER DEFAULT RAMP: 30
[MACHINE] TRAVERSE DEFAULT RAMP: 30
[MACHINE] WINDER DIRECTION: BACKWARD
[MACHINE] TRAVERSE DIRECTION: BACKWARD
[MACHINE] LIMIT SWITCH: ON
[MACHINE] HOME BEFORE START: OFF
[MACHINE] USE START OFFSET: ON
[MACHINE] BACKOFF DISTANCE: 2.000

PRESET SETTINGS:
[PRESET]  NAME: INIT
[PRESET]  WIRE: 0.100
[PRESET]  COIL LENGTH: 10.000
[PRESET]  TURNS: 1000
[PRESET]  TARGET RPM: 500
[PRESET]  RAMP: 10
[PRESET]  START OFFSET: 0.000

RUNTIME SETTINGS:
[RUNTIME] POSITION: 0
[RUNTIME] OS VERSION: 0.1
[RUNTIME] IS PAUSE REQUESTED: OFF
[RUNTIME] STEPS PER MM: 1600.000
[RUNTIME] IS HOMED: OFF
[RUNTIME] HOMING PHASE: 0
[RUNTIME] LAST STEP MICROS: 23052065
[RUNTIME] TRAVERSE ACCUMULATOR: 0.000
[RUNTIME] CURRENT LAYER STEPS: 0
[RUNTIME] LAYER DIRECTION: 1
[RUNTIME] BACKOFF DISTANCE MM: 1.000</pre>

## Task dump:

<pre>--- TASK DUMP START ---
STATE: 1
PREV_STATE: 0
MOTOR: S
RELATIVE: 1
TARGET_POS: 1600001
TARGET_STEPS: 1600000
DIR: 1
CUR_STEPS: 30966
ACCEL_DIST: 796
START_RPM: 40.00
TARGET_RPM: 120.00
CUR_RPM: 120.00
ACCEL_RATE: 10
CACHED_DELAY: 312
LAST_RAMP_UP: 178658
IS_STARTED: 1
IS_DECEL: 0
IS_COMPLETE: 0
IS_JOG: 0
TASK_STARTED: 166282
LAST_PING: 166282
--- TASK DUMP END ---</pre>
