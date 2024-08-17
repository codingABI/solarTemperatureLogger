/* ----------- User interface for changing the time and data ----------
 * License: 2-Clause BSD License
 * Copyright (c) 2024 codingABI
 */

// Set time and date manually
bool setManualTime(time_t localTime) {
  // Display modes
  enum mode { INIT, SELECT, EDIT };
  // Selection items
  enum selectItems { SELECTHOUR, SELECTMINUTE, SELECTSECOND, SELECTOK, SELECTDAY, SELECTMONTH, SELECTYEAR, SELECTCANCEL, MAXSELECTITEMS };
  mode lastMode=INIT;
  mode currentMode=INIT;
  int currentSelectedItem = SELECTHOUR;
  int currentValue = 0;
  int currentMaxOptions = MAXSELECTITEMS; // Encoder limit
  // Display cursor position of a selection items
  byte cursorPositions[MAXSELECTITEMS][2] = { {1,0},{4,0},{7,0},{15,0},{1,1},{4,1},{9,1},{15,1} };
  // Days of all month
  byte monthLengthList[]={31,28,31,30,31,30,31,31,30,31,30,31};
  byte monthLength;
  unsigned long lastUserInputS = 0;
  bool exitLoop = false;
  bool forceDisplayRefresh;
  unsigned long currentTimeUTC;

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
          forceDisplayRefresh = true;
          break;
        case EDIT: // Edit mode
          g_lcd.clear();
          forceDisplayRefresh = true;
          break;
      }
    } else forceDisplayRefresh = false;
    // Display time and date
    showTime(localTime,false,forceDisplayRefresh);
    showFullDate(localTime,forceDisplayRefresh);
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
          case SELECTHOUR:
            localTime = tmConvert_t(year(localTime), month(localTime), day(localTime), currentValue, minute(localTime), second(localTime));
            break;
          case SELECTMINUTE:
            localTime = tmConvert_t(year(localTime), month(localTime), day(localTime), hour(localTime), currentValue, second(localTime));
            break;
          case SELECTSECOND:
            localTime = tmConvert_t(year(localTime), month(localTime), day(localTime), hour(localTime), minute(localTime), currentValue);
            break;
          case SELECTDAY:
            localTime = tmConvert_t(year(localTime), month(localTime), currentValue+1, hour(localTime), minute(localTime), second(localTime));
            break;
          case SELECTMONTH:
            localTime = tmConvert_t(year(localTime), currentValue+1, day(localTime), hour(localTime), minute(localTime), second(localTime));
            break;
          case SELECTYEAR:
            localTime = tmConvert_t(currentValue+1970, month(localTime), day(localTime), hour(localTime), minute(localTime), second(localTime));
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
              case SELECTHOUR: // Change mode to edit hour
                currentMode = EDIT;
                currentMaxOptions = 24;
                currentValue=hour(localTime);
                g_lcd.noBlink();
                g_lcd.cursor();
                break;
              case SELECTMINUTE: // Change mode to edit minute
                currentMode = EDIT;
                currentMaxOptions = 60;
                currentValue=minute(localTime);
                g_lcd.noBlink();
                g_lcd.cursor();
                break;
              case SELECTSECOND: // Change mode to edit second
                currentMode = EDIT;
                currentMaxOptions = 60;
                currentValue=second(localTime);
                g_lcd.noBlink();
                g_lcd.cursor();
                break;                                                                                                          break;
              case SELECTDAY: // Change mode to edit day
                monthLength =  monthLengthList[month(localTime)-1];
                if (year(localTime) % 4 == 0) monthLength++; // Leap year
                currentMode = EDIT;
                currentMaxOptions = monthLength;
                currentValue = day(localTime)-1;
                g_lcd.noBlink();
                g_lcd.cursor();
                break;
              case SELECTMONTH: // Change mode to edit month
                currentMode = EDIT;
                currentMaxOptions = 12;
                currentValue = month(localTime)-1;
                g_lcd.noBlink();
                g_lcd.cursor();
                break;
              case SELECTYEAR: // Change mode to edit year
                currentMode = EDIT;
                currentMaxOptions = 65;
                currentValue = year(localTime)-1970;
                g_lcd.noBlink();
                g_lcd.cursor();
                break;
              case SELECTOK: // Ok was selected => Set time and exit
                setCurrentTimeUTC(localTimeToUTC(localTime));
                g_weakTime = true; // Do not 100% trust manual time
                lastUserInputS = seconds();
                currentTimeUTC = getCurrentTimeUTC();
                if (g_firstDCF77SyncUTC==0) g_firstDCF77SyncUTC = currentTimeUTC;
                // Schedule next DCF77 sync for next day
                g_nextDCF77SyncUTC = tmConvert_t(year(currentTimeUTC), month(currentTimeUTC), day(currentTimeUTC), DCF77SYNCHOUR, 0,0)+SECS_PER_DAY;
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
