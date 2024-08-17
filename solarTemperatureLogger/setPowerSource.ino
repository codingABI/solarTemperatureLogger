/* ----------- User interface for changing the used power source ----------
 * License: 2-Clause BSD License
 * Copyright (c) 2024 codingABI
 */

// Print power source on the display
void showPowerSource(powerSources powerSource) {
  #define MAXSTRDATALENGTH 16
  char strData[MAXSTRDATALENGTH+1];

  strData[0]='\0'; // Empty string
  switch(powerSource) {
    case SOLARPOWER:
      snprintf(strData,MAXSTRDATALENGTH+1,STR_SOLARPOWER);
      break;
    case BATTERYPOWER:
      snprintf(strData,MAXSTRDATALENGTH+1,STR_BATTERYPOWER);
      break;
    case USBPOWERBANK:
      snprintf(strData,MAXSTRDATALENGTH+1,STR_USBPOWERBANK);
      break;
  }

  // Fill rest of string with spaces (max 15 chars to leave space for the "Cancel-Button")
  strData[15]='\0';
  for (int i=strlen(strData);i<15;i++) strData[i] = ' ';

  // Print string
  g_lcd.setCursor(0,1);
  g_lcd.print(strData);
}

// Change the power source
bool setPowerSource() {
  // Display modes
  enum mode { INIT, SELECT, EDIT };
  // Selection items
  enum selectItems { SELECTPOWERSOURCE, SELECTCANCEL, SELECTOK, MAXSELECTITEMS };
  mode lastMode=INIT;
  mode currentMode=INIT;
  int currentSelectedItem = SELECTPOWERSOURCE;
  int currentValue = 0;
  int currentMaxOptions = MAXSELECTITEMS; // Encoder limit
  // Display cursor position of a selection items
  byte cursorPositions[MAXSELECTITEMS][2] = { {0,1},{15,1},{15,0} };
  unsigned long lastUserInputS = 0;
  bool exitLoop = false;
  powerSources powerSource = g_powerSource;

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
          g_lcd.print(STR_POWERSOURCE);
          g_lcd.print(":");
          showPowerSource(powerSource);
          break;
        case EDIT: // Edit mode
          g_lcd.clear();
          g_lcd.setCursor(0,0);
          g_lcd.print(STR_POWERSOURCE);
          g_lcd.print(":");
          showPowerSource(powerSource);
          break;
      }
    } else {
      showPowerSource(powerSource);
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
          case SELECTPOWERSOURCE:
            powerSource = currentValue;
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
              case SELECTPOWERSOURCE: // Change mode to select power source
                currentMode = EDIT;
                currentMaxOptions = MAXPOWERSOURCES;
                currentValue=powerSource;
                g_lcd.noBlink();
                g_lcd.cursor();
                break;
              case SELECTOK: // Ok was selected => Set values
                g_powerSource = powerSource;
                setEEPROMPowerSource(g_powerSource);
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
