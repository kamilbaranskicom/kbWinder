// --- COMMAND INTERPRETER ---

void processCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();  // so presets will be case-insensitive ;)

  if (cmd.startsWith(F("STOP"))) {
    emergencyStop(true);
  } else if (cmd.startsWith(F("START"))) {
    parseStartCommand(cmd.substring(6));
  } else if (cmd.startsWith(F("PAUSE"))) {
    pauseTask();
    //state = PAUSED;
    //digitalWrite(EN, HIGH);
    //Serial.println(F("MSG: Paused (Offline)"));
  } else if (cmd.startsWith(F("RESUME"))) {
    resumeTask();
    //resumeWinding();
  } else if (cmd.startsWith(F("GOTO "))) {
    handleGotoCommand(cmd);
  } else if (cmd.startsWith(F("SEEK ZERO"))) {
    startSeekingZero(cmd);
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
    handleSet(cmd);
  } else if (cmd.startsWith(F("GET"))) {
    handleGet(cmd);
  } else if (cmd.startsWith(F("SAVE "))) {
    savePreset(cmd.substring(5));
  } else if (cmd.startsWith(F("LOAD "))) {
    loadPresetByName(cmd.substring(5));
  } else if (cmd.startsWith(F("DELETE "))) {
    deletePreset(cmd.substring(7));
  } else if (cmd.startsWith(F("EXPORT"))) {
    exportCSV();
  } else if (cmd.startsWith(F("STATUS"))) {
    printStatus();
  } else if (cmd.startsWith(F("HELP"))) {
    printHelp();
  } else if (cmd.startsWith(F("LONGHELP"))) {
    printLongHelp();
  } else if (cmd.startsWith(F("SETHELP"))) {
    printSetHelp();
  }  // (... handle more commands...)
  else if (cmd.startsWith("T ") || cmd.startsWith("W ")) {
    moveManual(cmd);
  }
}

void printHelp() {
  Serial.println(F("Movement: W, T, GOTO T, SEEK ZERO, HOME\n"
                   "Control: START, STOP, PAUSE, RESUME\n"
                   "Presets: SAVE, LOAD, DELETE, EXPORT, IMPORT\n"
                   "Settings: SET ..., GET ...\n"
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
      "W <distance> [speed]: move Winder to relative position\n"
      "T <distance> [speed]: move Traverse to relative position\n"
      "GOTO <position> [speed]: move Traverse to absolute position\n"
      "GOTO HOME [speed]: goes to HOME position\n"
      "SAVE <name> [<wire-diameter> <coil-length> <turns>]: saves preset "
      "<name>\n"
      "LOAD <name>: loads preset <name>\n"
      "DELETE <name>: deletes preset <name>\n"
      "EXPORT: prints presets in CSV format\n"
      "IMPORT: imports presets in CSV format (ends with empty line)\n"
      "STATUS: prints status\n"
      "SET ... : sets parameter(s)\n"
      "GET ... : gets parameter(s)\n"
      "SETHELP: parameters list\n"
      "HELP: short help\n"
      "LONGHELP: this help"));
}

void printSetHelp() {
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
  Task *t = getCurrentTask();
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
      // Jeśli zadanie się zaczęło i targetSteps to 0, to znaczy że jesteśmy u celu
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

  /*
  // Simplified status output
  Serial.print(F("STATUS STATE="));
  Serial.print(state);
  Serial.print(F(" TURNS="));
  Serial.print(active.totalTurns);
  Serial.println();
*/

  /*
  // old status format
  long turnsDone = currentStepsW / cfg.stepsPerRevW;
  long turnsLeft = prst.totalTurns - turnsDone;
  int currentLayer = (currentStepsW / stepsPerLayer) + 1;
  int etaMin = (currentRPM > 0) ? (turnsLeft / currentRPM) : 0;

  Serial.print(F("STAT|T:"));
  Serial.print(turnsDone);
  Serial.print(F("/"));
  Serial.print(prst.totalTurns);
  Serial.print(F("|L:"));
  Serial.print(currentLayer);
  Serial.print(F("|RPM:"));
  Serial.print(currentRPM);
  Serial.print(F("|ETA:"));
  Serial.print(etaMin);
  Serial.println(F("m"));
*/
}