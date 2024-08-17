/* ----------- Dashboards  ----------
 * License: 2-Clause BSD License
 * Copyright (c) 2024 codingABI
 */

// Show a HH:MM:SS formatted time on the display at position
void printTime(byte x, byte y, time_t time) {
  #define MAXSTRDATALENGTH 16
  char strData[MAXSTRDATALENGTH+1];
  snprintf(strData,MAXSTRDATALENGTH+1,"%02i:%02i:%02i",hour(time),minute(time),second(time));
  g_lcd.setCursor(x,y);
  g_lcd.print(strData);
}

// Show a shortened DD.MM formatted date on the display at position
void printDate(byte x, byte y, time_t time) {
  #define MAXSTRDATALENGTH 16
  char strData[MAXSTRDATALENGTH+1];
  snprintf(strData,MAXSTRDATALENGTH+1,"%02i.%02i",day(time),month(time));
  g_lcd.setCursor(x,y);
  g_lcd.print(strData);
}

// Show a temperature on the display at position
void printTemperature(byte x, byte y, int temperature) {
  #define MAXSTRDATALENGTH 16
  char strData[MAXSTRDATALENGTH+1];
  if (abs(temperature) == TEMPERATUREOFFSET) { // Invalid/outed data
    snprintf(strData,MAXSTRDATALENGTH+1,"    -");
  } else {
    if ((temperature <= -100) || (temperature >= 1000)) {
      snprintf(strData,MAXSTRDATALENGTH+1,"%5i",round(temperature/10.0f));
    } else {
      snprintf(strData,MAXSTRDATALENGTH+1,"%3i.%01i",temperature/10,abs(temperature%10));
    }
  }
  g_lcd.setCursor(x,y);
  g_lcd.print(strData);
  g_lcd.write(CHAR_DEGREEC);
}

// Show time and last time sync state
void showTime(time_t displayTime, bool showLastSync, bool force) {
  static time_t lastTime = 0;
  if ((lastTime != displayTime) || force) {
    printTime(0,0,displayTime);
    // Last sync
    if (showLastSync) {
      g_lcd.setCursor(9,0);
      if (g_weakTime) g_lcd.print(" ");
      else g_lcd.write(CHAR_SIGNAL);
    }
    lastTime = displayTime;
  }
}

// Show date
void showDate(time_t displayTime, bool force) {
  static time_t lastDate = 0;
  if ((lastDate != displayTime) || force) {
    printDate(11,0,displayTime);
    lastDate = displayTime;
  }
}

// Show full date in DD.MM.YYYY format
void showFullDate(time_t displayTime, bool force) {
  static time_t lastDate = 0;
  #define MAXSTRDATALENGTH 16
  char strData[MAXSTRDATALENGTH+1];
  if ((lastDate != displayTime) || force) {
    snprintf(strData,MAXSTRDATALENGTH+1,"%02i.%02i.%04i",day(displayTime),month(displayTime),year(displayTime));
    g_lcd.setCursor(0,1);
    g_lcd.print(strData);
    lastDate = displayTime;
  }
}

/* Show calculated remaining runtimes (capacitor discharge)
 *
 * Measurement formulas:
 * - I =-(C*3,3)*LN(U1/U0)/t
 * - t = =-(C*3,3)*LN(U1/U0)/I
 * - U1 = U0*(EXP(-t/(C*(3,3/I))))
 * - C = t/(-(3,3/I)*LN(U1/U0))
 * - Online https://www.digikey.de/de/resources/conversion-calculators/conversion-calculator-capacitor-safety-discharge
 *
 * Measurement1:
 * Voltage on supercap when
 * - Display is off
 * - Dashboard DISPLAYOFF
 * - Temperature sensor is not used
 * - DCF77 sync is not used
 * 20240330
 * 00:00 4.85V = U0
 * 01:18 4.66V
 * 11:00 3.80V
 * 17:47 3.34V
 * 19:24 3.22V = U1
 * => I = 19uA
 *
 * Measurement2:
 * Voltage on supercap when
 * - Display is on
 * - Dashboard LASTDATA (with additional lastUserInputS = seconds();)
 * - Temperature sensor is used once per second
 * - DCF77 sync is not used
 * 20240330
 * 00:00 4.92V = U0
 * 00:20 4.37V
 * 00:30 4.10V
 * 00:47 3.68V
 * 01:00 3.36V
 * 01:15 2.98V = U1
 * => I = 360uA
 *
 * Measurement3:
 * Voltage on supercap when
 * - Display is on
 * - Dashboard POWER
 * - Temperature sensor is not used
 * - DCF77 sync is not used
 * 20240330
 * 00:00 4.94V = U0
 * 00:30 4.65V
 * 00:45 4.50V
 * 01:12 4.27V
 * 01:33 4.01V
 * 02:00 3.86V = U1
 * => I = 113uA
 *
 * Measurement4:
 * Voltage on supercap when
 * - Display is off
 * - Dashboard DISPLAYOFF
 * - Temperature sensor is used once per hour
 * - DCF77 sync is not used
 * 20240331
 * 00:00 4.92V = U0
 * 00:57 4.80V
 * 06:27 4.25V = U1
 * => I = 20uA
 *
 * Measurement5:
 * Voltage on supercap when
 * - Display is off
 * - Dashboard DISPLAYOFF
 * - Temperature sensor is used once per hour
 * - DCF77 sync is used one time (duration 600s)
 * 20240331
 * 00:00 4.92V = U0
 * 00:57 4.80V
 * 06:27 4.25V
 *   |   600s DCF77 sync
 * 07:33 3.59V
 * 09:24 3.46V
 * 10:43 3.37V
 * 18:40 2.87V = U1
 * => I = 26uA
 *
 * Measurement6:
 * Voltage on supercap when
 * - Display is off
 * - Dashboard DISPLAYOFF
 * - Temperature sensor is used once per hour
 * - DCF77 sync is used one time (duration 450s[=50% of timeout])
 * 20240402
 * 00:00 4.84V = U0
 * 01:40 4,67V
 *   |   450s DCF77 sync
 * 01:48 4.17V
 * 12:23 3.42V
 * 16:36 3.13V
 * 18:16 3.02V
 * 20:10 2.88V = U1
 * => I = 24uA
 *
 * USB trigger mode: add 2.6mA for one sec every 30 secs
 */
void showRemainingRuntime(int Vcc, bool force) {
  #define MAXSTRDATALENGTH 16
  char strData[MAXSTRDATALENGTH+1];
  #define MINVOLTAGE_V 2.86f
  float current = 0.000019f; // Minimum current (based on Measurement1)
  static int lastRuntime = 0;

  // With DCF77 sync (estimated on Measurement1)
  if ((!g_DCF77disabled) &&
    ((Vcc >= FULLSOLAR10MV_4V5) ||
    ((Vcc >= FULLBAT10MV_4V2) &&
    (g_powerSource != SOLARPOWER))))
    current = 0.000024f;

  // When temperature measurement is used once per hour (based on Measurement6)
  if (g_recordMode == AT0x00) current = 0.000024f;

  // When using USB powerbank trigger mode
  if ((g_powerSource == USBPOWERBANK) && (Vcc > FULLBAT10MV_4V2)) {
    // ~2.5mA@3.3V for one sec every 30 secs when Vcc > 4.2V
    // = 86uA until Vcc is below 4.2V
    current += 0.000086f;
  }

  // Calculate remaining runtime
  int runtime = round((-(3.3f/current)*log(MINVOLTAGE_V/(Vcc/100.0f)))/60.0f);

  if ((lastRuntime != runtime)
    || force) {
    if (force) {
      g_lcd.createChar(CHAR_EMPTYBATTERY, c_customChar_emptyBattery);
      g_lcd.setCursor(12,0);
      g_lcd.write(CHAR_EMPTYBATTERY);
    }
    // Runtime until critical voltage
    if (runtime < 100) {
      snprintf(strData,MAXSTRDATALENGTH+1,"%2im",runtime);
    } else {
      snprintf(strData,MAXSTRDATALENGTH+1,"%2ih",round(runtime/60.0f));
    }
    g_lcd.setCursor(13,0);
    g_lcd.print(strData);

    lastRuntime = runtime;
  }
}

// Show supply voltage
void showVcc(int Vcc, bool force) {
  static int lastVcc = -1;
  #define MAXSTRDATALENGTH 16
  char strData[MAXSTRDATALENGTH+1];
  if ((lastVcc != Vcc) || force) {
    snprintf(strData,MAXSTRDATALENGTH+1,"Vcap%2i.%02iV",Vcc/100,Vcc%100);
    g_lcd.setCursor(0,0);
    g_lcd.print(strData);
    lastVcc = Vcc;
  }
}

// Show first DCF77 sync time and time to next sync
void showDCFSyncTime(bool force) {
  static long lastNextSync = -1;
  #define NOSYNC 2147483647
  long nextSync = NOSYNC;
  #define MAXSTRDATALENGTH 16
  char strData[MAXSTRDATALENGTH+1];

  if (!g_DCF77disabled) {
    nextSync = g_nextDCF77SyncUTC - getCurrentTimeUTC();
  }
  if ((lastNextSync != nextSync) || force) {
    if (force) {
      g_lcd.createChar(CHAR_ROTATION, c_customChar_Rotation);
      g_lcd.createChar(CHAR_TIMER, c_customChar_Timer);
      g_lcd.setCursor(0,0);
      g_lcd.write(CHAR_ROTATION);
      g_lcd.setCursor(10,1);
      g_lcd.write(CHAR_TIMER);
    }
    if (g_firstDCF77SyncUTC == 0) {
      if (force) {
        g_lcd.setCursor(0,0);
        g_lcd.print(STR_DCF77MISSING);
      }
    } else {
      if (force) {
        printTime(1,0,UTCtoLocalTime(g_firstDCF77SyncUTC));
        printDate(11,0,UTCtoLocalTime(g_firstDCF77SyncUTC));
      }
    }

    if (nextSync==NOSYNC) {
      snprintf(strData,MAXSTRDATALENGTH+1,"-");
    } else {
      if (nextSync/SECS_PER_HOUR > 1) {
        snprintf(strData,MAXSTRDATALENGTH+1,"%4ih",round((float)nextSync/SECS_PER_HOUR));
      } else {
        snprintf(strData,MAXSTRDATALENGTH+1,"%4im",round((float)nextSync/SECS_PER_MIN));
      }
    }
    g_lcd.setCursor(11,1);
    g_lcd.print(strData);
    lastNextSync = nextSync;
  }
}

// Show up time for the device
void showUpTime(bool force) {
  static long lastUpTime = -1;
  long upTime;
  #define MAXSTRDATALENGTH 16
  char strData[MAXSTRDATALENGTH+1];

  upTime = seconds();

  if ((lastUpTime != upTime) || force) {
    if (force) {
      g_lcd.createChar(CHAR_RUNNER, c_customChar_Runner);
      g_lcd.setCursor(10,1);
      g_lcd.write(CHAR_RUNNER);
    }
    if (upTime/SECS_PER_HOUR > 48) {
      snprintf(strData,MAXSTRDATALENGTH+1,"%4id",round((float) upTime/SECS_PER_HOUR/24));
    } else {
      snprintf(strData,MAXSTRDATALENGTH+1,"%4ih",round((float) upTime/SECS_PER_HOUR));
    }
    g_lcd.setCursor(11,1);
    g_lcd.print(strData);
    lastUpTime = upTime;
  }
}

// Show temperature
void showTemperature(int temperature, bool force) {
  static int lastTemperature = -300;
  if ((lastTemperature != temperature) || force) {
    printTemperature(10,1, temperature);
    lastTemperature = temperature;
  }
}

// Show lastest dataset
void showLastestData(bool force) {
  static t_dataSet lastData = { -1, -1 };
  if ((lastData.time != g_lastData.time) || (lastData.data != g_lastData.data) || force) {
    printTime(0,0,UTCtoLocalTime(g_lastData.time));
    printDate(11,0,UTCtoLocalTime(g_lastData.time));
    printTemperature(10,1, g_lastData.data-TEMPERATUREOFFSET);
    if (force) {
      g_lcd.createChar(CHAR_LAST, c_customChar_Last);
      g_lcd.setCursor(9,1);
      g_lcd.write(CHAR_LAST);
    }
    lastData = g_lastData;
  }
}

// Show min dataset
void showMinData(bool force) {
  static t_dataSet lastData = { -1, -1 };
  if ((lastData.time != g_minData.time) || (lastData.data != g_minData.data) || force) {
    printTime(0,0,UTCtoLocalTime(g_minData.time));
    printDate(11,0,UTCtoLocalTime(g_minData.time));
    printTemperature(10,1, g_minData.data-TEMPERATUREOFFSET);
    if (force) {
      g_lcd.createChar(CHAR_MIN, c_customChar_Min);
      g_lcd.setCursor(9,1);
      g_lcd.write(CHAR_MIN);
    }
    lastData = g_minData;
  }
}

// Show max dataset
void showMaxData(bool force) {
  static t_dataSet lastData = { -1, -1 };
  if ((lastData.time != g_maxData.time) || (lastData.data != g_maxData.data) || force) {
    printTime(0,0,UTCtoLocalTime(g_maxData.time));
    printDate(11,0,UTCtoLocalTime(g_maxData.time));
    printTemperature(10,1, g_maxData.data-TEMPERATUREOFFSET);
    if (force) {
      g_lcd.createChar(CHAR_MAX, c_customChar_Max);
      g_lcd.setCursor(9,1);
      g_lcd.write(CHAR_MAX);
    }
    lastData = g_maxData;
  }
}

// Show overview for latest stored dataset
void showLatestRecording(bool force) {
  #define MAXSETSLENGTH 7
  char strSets[MAXSETSLENGTH+1];
  #define MAXSTRDATALENGTH 16
  char strData[MAXSTRDATALENGTH+1];
  static t_dataSet lastData = { -1, -1 };
  t_dataSet data = { 0, 0 };
  if (g_eepromNextIndex > 0) getEEPROMDatasetAtIndex(g_eepromNextIndex-1, data);
  if ((lastData.time != data.time) || (lastData.data != data.data) || force) {
    if (g_eepromNextIndex > 0) {
      printTime(1,0,UTCtoLocalTime(data.time));
      printDate(11,0,UTCtoLocalTime(data.time));
      // Fill space between time and date
      g_lcd.setCursor(9,0);
      g_lcd.print("  ");
    } else {
      g_lcd.setCursor(1,0);
      g_lcd.print(STR_BUFFEREMPTY);
    }
    if (force) {
      g_lcd.createChar(CHAR_LAST, c_customChar_LastItem);
      g_lcd.setCursor(0,0);
      g_lcd.write(CHAR_LAST);
    }
    snprintf(strSets,MAXSETSLENGTH+1,"%i/%i",g_eepromNextIndex,getEEPROMMaxDataSets());
    snprintf(strData,MAXSTRDATALENGTH+1,"%7s",strSets);
    g_lcd.setCursor(9,1);
    g_lcd.print(strData);
    lastData = data;
  }
}

// Show reset reason WDT, Brownout, External or Power on
void showResetReason(bool force) {
  #define MAXSTRDATALENGTH 16
  char strData[MAXSTRDATALENGTH+1];

  if (force) {
    g_lcd.createChar(CHAR_ROTATION, c_customChar_Rotation);
    g_lcd.setCursor(11,1);
    g_lcd.write(CHAR_ROTATION);

    snprintf(strData,MAXSTRDATALENGTH+1,"----");

    if (((g_lastMCUSR>>0) & 1) == 1) strData[3] = 'P';
    if (((g_lastMCUSR>>1) & 1) == 1) strData[2] = 'E';
    if (((g_lastMCUSR>>2) & 1) == 1) strData[1] = 'B';
    if (((g_lastMCUSR>>3) & 1) == 1) strData[0] = 'W';

    g_lcd.print(strData);
  }
}


// Create special char for selected dashboard item
void createSelectedChar(int i, int remainingTime) {
  byte customChar_dotsSelected[8];

  // Copy default char
  for (int j=0;j<8;j++) customChar_dotsSelected[j] = c_customChar_dots[j];
  // Add pixels depending on remaing time before timeout expiration
  for (int j=0;j<remainingTime;j++) {
    if ((i & 1)==0) {
      customChar_dotsSelected[5-j] = B10000;
    } else {
      customChar_dotsSelected[5-j] = B00010;
    }
  }
  g_lcd.createChar(CHAR_SELECTION, customChar_dotsSelected);
}

// Create navigation dots for the dashboard
void showDots(byte dashboard, int remainingTime, bool force) {
  static byte lastDashboard = MAXDASHBOARDS;
  static byte lastRemainingTime = 0;
  byte maxItemsTrimmed;
  #define MAXSTRDATALENGTH 16
  char strData[MAXSTRDATALENGTH+1];
  // Menu items must be odd!!
  if (remainingTime > 3) remainingTime = 3;
  if (remainingTime < 0) remainingTime = 0;
  if ((lastDashboard != dashboard) || (lastRemainingTime != remainingTime) || force) {
    // Show overview dots
    g_lcd.setCursor(0,1);
    // Prevent string/display overrun
    maxItemsTrimmed = (MAXDASHBOARDS-1)/2; // Decrease items by one because in dashboard 0 there will be no display
    if (maxItemsTrimmed-1 > MAXSTRDATALENGTH) maxItemsTrimmed = MAXSTRDATALENGTH+1;
    for (int i=0;i<maxItemsTrimmed;i++) {
      if (i == (dashboard-1)/2) {
        strData[i] = CHAR_SELECTION; // Mark current item
      } else {
        strData[i] = CHAR_DOTS;
      }
    }
    strData[maxItemsTrimmed]='\0';
    createSelectedChar(dashboard+1,remainingTime);
    g_lcd.setCursor(0,1);
    g_lcd.print(strData);

    lastDashboard = dashboard;
    lastRemainingTime = remainingTime;
  }
}
