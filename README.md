
# kbWinder

This is my approach to pickup coil (or spool of thread ;P ) winder firmware. Dedicated to Piotr Stożek at [Woodys Backline](https://woodysbackline.com/) for helping me with my Hammond Organs/Leslies/Rhodes/Wurlitzer Pianos and other stuff.

I used [original sandy9159 idea](https://github.com/sandy9159/DIY-Arduino-based-Guitar-Pickup-Coil-Winder), but created better (and - most of all - **working**) software.

The software pushes the Arduino Nano to its edges - both on RAM, and speed on my TMC2209 (as it has 1600 steps per revolution), so if you can use A4988 with 400/800 steps/rev DO IT. The ESP with its WiFi/AsyncWebServer/Websockets is also a bit pushed.

## Features:
- Presets for winding (with load/save/delete/export)
- Tasks
- Configuration (with motor start/max/accel rpm, screw width and more)
- get rid of Nextion 1990-style controller ;)
- Serial interface with lots of commands
- ESP8266-powered WWW/JS interface with most of them
- Jog both stepper motors
- Precise seek zero with limit switch 
- Driving motors with no external libraries
- Acceleration and deceleration for both motors
- Start point for presets
- (...etc...)

## Screenshots:

<img src="https://raw.githubusercontent.com/kamilbaranskicom/kbWinder/refs/heads/main/imgs/kbWinderScreenshot1.jpg" width="48%">
<img src="https://raw.githubusercontent.com/kamilbaranskicom/kbWinder/refs/heads/main/imgs/kbWinderScreenshot2.jpg" width="48%">

## Needed:
- [Original hardware](original/README.sandy9159.md)
- ESP8266 for WWW access (optional; you can still use manual text commands)
- TXB0104 or similar voltage converter for Nano-ESP8266 connection (**THIS IS IMPORTANT**: ESP8266 uses 3.3V logic, while Arduino Nano uses 5V. You need to convert voltage levels or you will burn the ESP! Alternatively you can use connect Nano and ESP to your computer and use <code>UARTBridge/bridge.py</code> (remember to set the COM ports).
- Limit switch (optional) - connected to D4 and GND on the PCB board

## ESP configuration
The setup.html page on the ESP is probably unusable (I've borrow most of the ESP code from another my project), so just set WiFi settings in <code>kbWinderWWW/data/configuration.json</code>. mDNS should allow to access WWW interface on http://kbwinder.local/ , the original IP for AP mode is 192.168.4.1 (but it works in STA mode). Check the <code>kbWinderWWW/configuration.h</code> for more info about www interface settings.

## Commands:
<pre>Movement: W [revs] [speed], T [dist] [speed],
          GOTO [ZERO|BACKOFF|START|&lt;absPos&gt;], SEEK ZERO
Control: START [values], STOP, PAUSE, RESUME
Presets: SAVE [name], LOAD [name], DELETE [name], EXPORT
Settings: GET [MACHINE|PRESET|RUNTIME|&lt;val&gt;], SET ..., FACTORY
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

RUNTIME SETTINGS (readonly):
[RUNTIME] POSITION: 0
[RUNTIME] OS VERSION: 1.0
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
