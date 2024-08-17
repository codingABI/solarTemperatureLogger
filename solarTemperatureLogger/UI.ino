/* ----------- Common user interface stuff ----------
 * License: 2-Clause BSD License
 * Copyright (c) 2024 codingABI
 */

// Show info message
void showInfo(const char *info) {
  #define MAXSTRDATALENGTH 16
  char strData[MAXSTRDATALENGTH+1];
  #define USERINFOTIMEOUT 2
  bool exitLoop = false;
  int lastRemainingTime = 0;
  unsigned long lastUserInputS = 0;

  g_lcd.clear();
  if (info == NULL) return;

  snprintf(strData,MAXSTRDATALENGTH+1,"%s",info);
  g_lcd.setCursor(8-strlen(strData)/2,0); // Center
  g_lcd.print(strData);

  lastUserInputS = seconds();

  do { // Loop until user input or timeout expires
    wdt_reset();
    checkUSBChargerTrigger();

    byte remainingTime = (USERINFOTIMEOUT - abs(seconds() - lastUserInputS) +19) / 20;
    // Update temperatur from sensor, when requested
    updateTemperature();

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

    // Exit by rotary encoder
    byte rotation = g_rotaryEncoder.getAndResetLastRotation();
    switch (rotation) {
      case KY040::CLOCKWISE:
      case KY040::COUNTERCLOCKWISE:
        exitLoop = true;
        break;
    }

    // Exit by button
    switch (g_switchButton.getButton()) {
      case SWITCHBUTTON::LONGPRESSED:
      case SWITCHBUTTON::LONGPRESSEDRELEASED:
      case SWITCHBUTTON::MISSED:
          lastUserInputS = seconds(); // reset user timeout
        break;
      case SWITCHBUTTON::SHORTPRESSED:
        exitLoop = true;
        break;
    }

    // Exit on timeout expiration
    if (seconds() - lastUserInputS > USERINFOTIMEOUT) {
      // Abort input
      exitLoop = true;
    }
  } while (!exitLoop);

  g_lcd.clear();
}

// View all datasets stored in persistent buffer/EEPROM
void viewEEPROM() {
  #define MAXSTRDATALENGTH 16
  char strData[MAXSTRDATALENGTH+1];

  word index;
  word oldIndex = 0xffff;
  word indexSize = getEEPROMNextIndex();
  bool exitLoop = false;
  t_dataSet dataSet;
  int lastRemainingTime = 0;
  word value;
  unsigned long lastUserInputS = 0;

  index = indexSize -1;
  lastUserInputS = seconds();

  if (indexSize < 1) { // No data
    showInfo(STR_BUFFEREMPTY);
    return;
  }
  do { // Loop until User input or timeout expires
    wdt_reset();
    checkUSBChargerTrigger();

    byte remainingTime = (USERTIMEOUT - abs(seconds() - lastUserInputS) +19) / 20;

    if (index != oldIndex) { // Selection changed?

      // Show index
      g_lcd.clear();
      snprintf(strData,MAXSTRDATALENGTH+1,"%i/%i",index+1,indexSize);
      g_lcd.setCursor(0,1);
      g_lcd.print(strData);

      // Show dataset
      if (getEEPROMDatasetAtIndex(index,dataSet)) {
        printTime(0,0,UTCtoLocalTime(dataSet.time));
        printDate(11,0,UTCtoLocalTime(dataSet.time));
        int temperature = dataSet.data-TEMPERATUREOFFSET;
        if ((temperature <= -100) || (temperature >= 1000)) {
          snprintf(strData,MAXSTRDATALENGTH+1,"%5i",round(temperature/10.0f));
        } else {
          snprintf(strData,MAXSTRDATALENGTH+1,"%3i.%01i",temperature/10,abs(temperature%10));
        }
        g_lcd.setCursor(8,1);
        g_lcd.print(strData);
        g_lcd.write(CHAR_DEGREEC);
      }

      // Show cancel and cursor
      g_lcd.blink();
      g_lcd.setCursor(15,1);
      g_lcd.write(CHAR_CANCEL);
      g_lcd.setCursor(15,1);

      oldIndex = index;
    }
    // Go to sleep, when ready
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
    // Change item by rotary encoder
    switch (g_rotaryEncoder.getAndResetLastRotation()) {
      case KY040::CLOCKWISE:
        if (index < indexSize - 1) index++; else index = 0;
        lastUserInputS = seconds();
        break;
      case KY040::COUNTERCLOCKWISE:
        if (index > 0) index--; else index = indexSize -1;
        lastUserInputS = seconds();
        break;
    }

    // Exit by button
    switch (g_switchButton.getButton()) {
      case SWITCHBUTTON::SHORTPRESSED:
      case SWITCHBUTTON::LONGPRESSED:
        exitLoop = true;
        break;
    }

    if (seconds() - lastUserInputS > USERTIMEOUT) { // Exit when timeout expires
      // Abort input
      exitLoop = true;
    }
  } while (!exitLoop);

  g_lcd.noBlink();
  g_lcd.clear();
}
