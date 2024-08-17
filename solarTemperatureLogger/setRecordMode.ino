/* ----------- User interface for changing the automatic record mode ----------
 * License: 2-Clause BSD License
 * Copyright (c) 2024 codingABI
 */

// Schedule next record
void scheduleNextRecord() {
  unsigned long currentTimeUTC, localTime;

  // Get current time
  currentTimeUTC = getCurrentTimeUTC()+SENSORDELAY;

  switch(g_recordMode) {
    case AT0x00: // Every hour
      g_nextRecordUTC = tmConvert_t(
        year(currentTimeUTC),
        month(currentTimeUTC),
        day(currentTimeUTC),
        hour(currentTimeUTC), 0,0)+SECS_PER_HOUR;
      g_nextRecordUTC -=SENSORDELAY;
      break;
    default: 
      if (g_recordMode >= AT0000) { // Single hour selection
        localTime = UTCtoLocalTime(currentTimeUTC);
        if (hour(localTime)>=g_recordMode-AT0000) localTime+=SECS_PER_DAY;
        g_nextRecordUTC = localTimeToUTC(tmConvert_t(
          year(localTime),
          month(localTime),
          day(localTime),
          g_recordMode-AT0000,0,0));
        g_nextRecordUTC -=SENSORDELAY;        
      }
  }
}

// Print power source on the display
void showRecordMode(recordModes recordMode) {
  #define MAXSTRDATALENGTH 16
  char strData[MAXSTRDATALENGTH+1];

  strData[0]='\0'; // Empty string
  switch(recordMode) {
    case NORECORDING:
      snprintf(strData,MAXSTRDATALENGTH+1,STR_NORECORDING);
      break;
    case AT0x00:
      snprintf(strData,MAXSTRDATALENGTH+1,STR_AT0x00);
      break;
    default:
      // Single hour selection
      if (recordMode >= AT0000) snprintf(strData,MAXSTRDATALENGTH+1,"%02i:00:00",recordMode-AT0000);
  }

  // Fill rest of string with spaces (Max 15 chars to leave space for the "Cancel-Button")
  strData[15]='\0';
  for (int i=strlen(strData);i<15;i++) strData[i] = ' ';

  // Print string
  g_lcd.setCursor(0,1);
  g_lcd.print(strData);
}

// Change the automatic record mode for sensor data
bool setRecordMode() {
  // Display modes
  enum mode { INIT, SELECT, EDIT };
  // Selection items
  enum selectItems { SELECTRECORDMODE, SELECTCANCEL, SELECTOK, MAXSELECTITEMS };
  mode lastMode=INIT;
  mode currentMode=INIT;
  int currentSelectedItem = SELECTRECORDMODE;
  int currentValue = 0;
  int currentMaxOptions = MAXSELECTITEMS; // Encoder limit
  // Display cursor position of a selection items
  byte cursorPositions[MAXSELECTITEMS][2] = { {0,1},{15,1},{15,0} };
  unsigned long lastUserInputS = 0;
  bool exitLoop = false;
  recordModes recordMode;

  recordMode = g_recordMode;
  // Set initial encoder value
  currentValue = currentSelectedItem;
  lastUserInputS = seconds();

  do { // Loop until User select OK or cancel or input timeout expiration
    wdt_reset();
    checkUSBChargerTrigger();

    if (currentMode == INIT) { // Display start mode
      currentMode = SELECT;
      g_lcd.blink();
    }
    // Display time
    if (currentMode != lastMode) { // If display mode changed
      switch(currentMode) {
        case SELECT: // Select item mode
          g_lcd.clear();
          g_lcd.setCursor(15,0);
          g_lcd.write(CHAR_OK);
          g_lcd.setCursor(15,1);
          g_lcd.write(CHAR_CANCEL);
          g_lcd.setCursor(0,0);
          g_lcd.print(STR_RECORDMODE);
          g_lcd.print(":");
          showRecordMode(recordMode);
          break;
        case EDIT: // Edit mode
          g_lcd.clear();
          g_lcd.setCursor(0,0);
          g_lcd.print(STR_RECORDMODE);
          g_lcd.print(":");
          showRecordMode(recordMode);
          break;
      }
    } else {
      showRecordMode(recordMode);
    }
    lastMode=currentMode;

    // Set cursor on display
    g_lcd.setCursor(cursorPositions[currentSelectedItem][0],cursorPositions[currentSelectedItem][1]);

    if (g_rotaryEncoder.readyForSleep()) {
      if (g_switchButton.readyForSleep()) {
        // Now we need no millis & Co. in this loop => Sleep mode SLEEP_MODE_PWR_SAVE is possible
        set_sleep_mode(SLEEP_MODE_PWR_SAVE);
      } else {
        // We need still millis => Only sleep mode SLEEP_MODE_IDLE is possible
        set_sleep_mode(SLEEP_MODE_IDLE);
      }
      power_spi_disable();
      power_usart0_disable();
      sleep_mode();
    }
    switch (g_rotaryEncoder.getAndResetLastRotation()) {
      case KY040::CLOCKWISE:
        if (currentValue < currentMaxOptions - 1) currentValue++; else currentValue = 0;
        lastUserInputS = seconds();
        break;
      case KY040::COUNTERCLOCKWISE:
        if (currentValue > 0) currentValue--; else currentValue = currentMaxOptions -1;
        lastUserInputS = seconds();
        break;
    }

    // Use encoder value for item selection or value changes
    switch (currentMode) {
      case SELECT: // Item selection
        currentSelectedItem = currentValue;
        break;
      case EDIT: // Value changes
        switch (currentSelectedItem) {
          case SELECTRECORDMODE:
            recordMode = currentValue;
            break;
        }
        break;
    }

    switch (g_switchButton.getButton()) {
      case SWITCHBUTTON::SHORTPRESSED:
      case SWITCHBUTTON::LONGPRESSED:
        lastUserInputS = seconds();
        switch (currentMode) {
          case SELECT:
            switch(currentSelectedItem) {
              case SELECTRECORDMODE: // Change mode to select power source
                currentMode = EDIT;
                currentMaxOptions = MAXRECORDMODES;
                currentValue=recordMode;
                g_lcd.noBlink();
                g_lcd.cursor();
                break;
              case SELECTOK: // Ok was selected => Set values
                g_recordMode = recordMode;
                setEEPROMRecordMode(g_recordMode);
                scheduleNextRecord();
                exitLoop = true;
                break;
              case SELECTCANCEL: // Cancel was selected => Exit
                exitLoop = true;
                break;
            }
            break;
          case EDIT: // Change mode to select items
            currentMaxOptions = MAXSELECTITEMS;
            currentValue = currentSelectedItem;
            currentMode = SELECT;
            g_lcd.noCursor();
            g_lcd.blink();
            break;
        }
        break;
    }

    if (seconds() - lastUserInputS > USERTIMEOUT) { // Exit on timeout expiration
      switch (currentMode) {
        case SELECT:
          // Abort selection
          exitLoop = true;
          break;
        case EDIT:
          // Go back to selection
          currentMaxOptions = MAXSELECTITEMS;
          currentValue = currentSelectedItem;
          currentMode = SELECT;
          g_lcd.noCursor();
          g_lcd.blink();
          lastUserInputS = seconds();
          break;
      }
    }
  } while (!exitLoop);

  g_lcd.clear();
  g_lcd.noBlink();

  if (currentSelectedItem == SELECTOK) return true;
  return false;
}
