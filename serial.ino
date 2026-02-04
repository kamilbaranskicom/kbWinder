
// --- COMMAND INTERPRETER ---

void processCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();  // so presets will be case-insensitive ;)
  Serial.println("Received: " + cmd);

  if (cmd.startsWith(F("STOP"))) {
    emergencyStop(true);
  } else if (cmd.startsWith(F("JOG PING"))) {
    getCurrentTask()->taskLastPinged = millis();
    // Serial.print("RPM=");
    // Serial.println(getCurrentTask()->currentRPM);
  } else if (cmd.startsWith(F("STATUS"))) {
    printStatus();
  } else if (cmd.startsWith(F("PAUSE"))) {
    pauseTask();
  } else if (cmd.startsWith(F("START"))) {
    parseStartCommand(cmd.substring(6));
  } else if (cmd.startsWith(F("RESUME"))) {
    resumeTask();
  } else if (cmd.startsWith(F("GOTO "))) {
    handleGotoCommand(cmd);
  } else if (cmd.startsWith(F("SEEK ZERO"))) {
    parseSeekZeroCommand(cmd);
  } else if (cmd == F("SET ZERO")) {
    absPos = 0;
    isHomed = true;
    Serial.println(
      F("MSG: Machine absolute ZERO established at current position"));
  } else if (cmd == F("SET HOME")) {
    // HOME w naszym nazewnictwie to startOffset w strukturze presetu
    active.startOffset = absPos;

    Serial.print(F("MSG: Preset START OFFSET set to "));
    Serial.print((float)absPos / stepsPerMM, 3);
    Serial.println(F(" mm (Remember to SAVE if you want to keep it!)"));
  } else if (cmd.startsWith(F("SET "))) {
    handleSet(cmd.substring(4));
  } else if (cmd.startsWith(F("GET TASK"))) {
    printTaskDebug(getCurrentTask());
  } else if (cmd.startsWith(F("GET MEMORY"))) {
    printFreeMemory();
  } else if (cmd.startsWith(F("GET"))) {
    handleGet(cmd.substring(3));
  } else if (cmd.startsWith(F("SAVE"))) {
    savePreset(cmd.substring(4));
  } else if (cmd.startsWith(F("LOAD "))) {
    loadPresetByName(cmd.substring(5));
  } else if (cmd.startsWith(F("DELETE "))) {
    deletePreset(cmd.substring(7));
  } else if (cmd.startsWith(F("FORMAT"))) {
    formatPresets();
  } else if (cmd.startsWith(F("EXPORT"))) {
    exportCSV();
  } else if (cmd.startsWith(F("HELP"))) {
    printHelp();
  } else if (cmd.startsWith(F("LONGHELP"))) {
    printLongHelp();
  } else if (cmd.startsWith(F("SETHELP"))) {
    printSetHelp();
  } else if (cmd.startsWith(F("FACTORY"))) {
    loadFallbackConfiguration();

  } else if (cmd.startsWith(F("JOG "))) {
    moveManual(cmd.substring(4), true);
  }  // (... handle more commands...)
  else if (cmd.startsWith("T ") || cmd.startsWith("W ")) {
    moveManual(cmd);
  }
}

void printHelp() {
  Serial.println(
    F("Movement: W [revs] [speed], T [dist] [speed],\n"
      "          GOTO [ZERO|BACKOFF|START|<absPos>], SEEK ZERO\n"
      "Control: START [values], STOP, PAUSE, RESUME\n"
      "Presets: SAVE [name], LOAD [name], DELETE [name], FORMAT, EXPORT\n"
      "Settings: GET [MACHINE|PRESET|RUNTIME|MEMORY|<val>], SET ..., FACTORY\n"
      "Info: STATUS, HELP, LONGHELP, SETHELP"));
}

void printLongHelp() {
  Serial.println(
    F("START (<preset>|<wire-diameter> <coil-length> <turns> [rpm] [ramp] "
      "[offset]):\n"
      "  starts winding the coil\n"
      "STOP: stop winding\n"
      "PAUSE: pause winding and put motors in offline; doesn't reset "
      "position\n"
      "RESUME: resume winding after PAUSE\n"
      "SEEK ZERO [speed]: finds ZERO position (by moving Traverse backward\n"
      "  until the limit switch is found or STOP command is sent)\n"
      "W <distance> [speed]: move Winder to relative position (in turns)\n"
      "T <distance> [speed]: move Traverse to relative position (in mm)\n"
      "JOG W/T <distance> [speed]: move to relative position, but stops\n"
      "  if no JOG PING received within 2 secs\n"
      "JOG PING: ping for JOG W/T\n"
      "GOTO <position> [speed]: move Traverse to absolute position\n"
      "GOTO (ZERO|START|BACKOFF) [speed]: move Traverse to (zero|\n"
      "  preset start|backoff distance) position\n"
      "SAVE [<name>|<csvString>]: saves preset\n"
      "LOAD <name>: loads preset <name>\n"
      "DELETE <name>: deletes preset <name>\n"
      "FORMAT: deletes all presets\n"
      "EXPORT: prints presets in CSV format\n"
      "STATUS: prints status\n"
      "FACTORY: loads default machine settings\n"
      "SET ... : sets parameter(s)\n"
      "GET ... : gets parameter(s)\n"
      "SETHELP: parameters list\n"
      "HELP: short help\n"
      "LONGHELP: this help"));
}

void printSetHelp() {
  // todo - it's a bit obsolete
  Serial.println(
    F("SET <wire-diameter> <coil-length> <turns>: sets parameters\n"
      "SET WIRE <wire-diameter>: in mm\n"
      "SET COIL LENGTH <coil-length>: in mm\n"
      "SET TURNS <turns>: how many turns the winder should go\n"
      "SET ZERO: sets ZERO position to current position\n"
      "SET HOME: sets HOME position to current position\n"
      "SET SCREW PITCH <pitch>: sets screw pitch in mm\n"
      "SET WINDER STEPS <steps>: sets winder steps\n"
      "SET TRAVERSE STEPS <steps>: sets traverse steps\n"
      "SET WINDER SPEED <rpm>: sets winder max speed\n"
      "SET TRAVERSE SPEED <rpm>: sets traverse max speed\n"
      "SET WINDER DIR [FORWARD|BACKWARD]: sets winder direction\n"
      "SET TRAVERSE DIR [FORWARD|BACKWARD]: sets traverse direction\n"
      "SET LIMIT SWITCH [ON|OFF]: if off, seeks for ZERO slower (to allow\n"
      "  for manual STOP command)\n"
      "SET ZERO BEFORE HOME [ON|OFF]: if on, finds zero when going HOME\n"
      "SET HOME BEFORE START [ON|OFF]: if on, goes HOME before winding.\n"
      "GET [parameter]: prints current value of <parameter> (or all "
      "parameters if not specified)"));
}

void printStatus() {
  Task* t = getCurrentTask();
  Serial.println(F("--- MACHINE STATUS ---"));
  if (t == NULL) {
    Serial.println(F("State: IDLE"));
  } else {
    Serial.print(F("Current Task: "));
    Serial.print(getTaskStateStr(t->state));
    Serial.print(F(" ("));
    Serial.print(taskCount);
    Serial.println(F(" in queue)"));

    float progress = 0;
    if (t->targetSteps > 0) {
      progress = (float)t->currentSteps / t->targetSteps * 100.0;
    } else if (t->isStarted) {
      // Jeśli zadanie się zaczęło i targetSteps to 0, to znaczy że jesteśmy u
      // celu
      progress = 100.0;
    }

    Serial.print(F("Progress: "));
    Serial.print(progress, 1);
    Serial.println(F("%"));
    Serial.print(F("Current RPM: "));
    Serial.println(t->currentRPM);
  }
  Serial.print(F("Abs Position: "));
  Serial.print((float)absPos / stepsPerMM);
  Serial.println(F(" mm"));
  Serial.println(F("----------------------"));

  return;
}


#ifdef __arm__
// Dla procesorów ARM (np. Due)
extern "C" char* sbrk(int incr);
int freeMemory() {
  char top;
  return &top - reinterpret_cast<char*>(sbrk(0));
}
#elif defined(AVR)
// Dla Arduino Nano/Uno (AVR)
extern unsigned int __heap_start;
extern void *__brkval;

int freeMemory() {
  int free_memory;
  if ((int)__brkval == 0)
    free_memory = ((int)&free_memory) - ((int)&__heap_start);
  else
    free_memory = ((int)&free_memory) - ((int)__brkval);

  return free_memory;
}
#endif


void printFreeMemory() {
  int freeRAM = freeMemory();
  Serial.print(F("SYSTEM: Free RAM: "));
  Serial.print(freeRAM);
  Serial.println(F(" bytes"));

  if (freeRAM < 200) {
    Serial.println(F("WARNING: Memory critical! Fragmentation possible."));
  }
}