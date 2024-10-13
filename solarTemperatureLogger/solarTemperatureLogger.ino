/*
 * Project: solarTemperatureLogger (Ding23)
 *
 * Description:
 * The device is a logger for temperature and time (DCF77 time signal driven). It is powered by a small solar panel (Battery or USB powerbank would also work).
 * - A dataset consists of a temperature value and the corresponding timestamp
 * - Datasets can be created manually with a knob or automatically by schedule
 * - Up to 167 datasets can be store in a persistent buffer (EEPROM)
 * - Datasets from the persistent buffer can be sent via serial to a computer
 *
 * License: 2-Clause BSD License
 * Copyright (c) 2024 codingABI
 * For details see: License.txt
 *
 * created by codingABI https://github.com/codingABI/solarTemperatureLogger
 *
 * External code:
 * I use external code in this project in form of libraries and two small
 * code piece called summertime_EU and getBandgap, but does not provide these code.
 *
 * If you want to compile my project, you should be able to download the needed libraries
 * - DCF77 (by Thijs Elenbaas)
 * - Time (by Michael Margolis/Paul Stoffregen)
 * - DallasTemperature (by Miles Burton)
 * with the Arduino IDE Library Manager and the libraries
 * - SWITCHBUTTON (https://github.com/codingABI/SWITCHBUTTON by codingABI)
 * - KY040 (https://github.com/codingABI/KY040 by codingABI)
 * - arduino_ST7032 (https://github.com/tomozh/arduino_ST7032 by tomozh@gmail.com)
 * from GitHub.
 *
 * For details to get the small code pieces for
 * - summertime_EU "European Daylight Savings Time calculation by "jurs" for German Arduino Forum"
 * - getBandgap by "Coding Badly" and "Retrolefty" from the Arduino forum
 * see externalCode.ino.
 *
 * Hardware:
 * - Microcontroller ATmega328P (In 8 MHz-RC mode. Board manager: "ATmega328 on a breadboard (8 MHz internal clock)" )
 * - 32768 kHz clock crystal for timer2
 * - DCF-3850M-800 DCF77 time signal receiver
 * - WINSTAR WO1602G ST7032 LCD I2C display
 * - DS18B20 temperature sensor
 * - KY-040 rotary encoder
 * - 6V/150mA solar panel
 * - 1F/5.5V supercap
 * - HT7333A 3.3V voltage regulator
 * - HT7350A 5.0V voltage regulator (Voltage protection for the supercap)
 * - 1N4007 diode to prevent current from supercap back to Vin
 * - 2n2222-Transistor to trigger load, when powered by a USB powerbank
 *
 * Red LED status/duration:
 * - 50ms = Vcap is below 3.35V (too less for enabling the display)
 * - 8s   = A Watchdog timeout expiration was detected and therefore a reset is pending
 *
 * History:
 * 20240422, Initial version
 */
#include <avr/sleep.h>
#include <avr/power.h>
#include <ST7032.h>
#include <TimeLib.h>
#include <avr/wdt.h>
#include <Wire.h>
#include <DCF77.h>
#include <KY040.h>
#include <SWITCHBUTTON.h>
#include <EEPROM.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "customChars.h"
// Set display language to DE or EN
#define DISPLAYLANGUAGE_DE
//#define DISPLAYLANGUAGE_EN
#include "displayLanguage.h"

// Pin definitions
#define USBTRIGGER_PIN 6
#define LED_PIN 13 // SPI SCK
#define I2CSDA_PIN A4
#define I2CSCL_PIN A5
#define VBAT_PIN A3
#define VBAT_ENABLE_PIN A0
#define DS18B20_DATA_PIN 8
#define DS18B20_VCC_PIN 7
#define DCF77_VCC_PIN A1
#define DCF77_OUT_PIN 2
#define LCDVCC_PIN A2
#define ROTARY_DT_PIN 4 // Rotary DT
#define ROTARY_CLK_PIN 5 // Rotary CLK
#define ROTARY_SW_PIN 3 // Rotary SW button

// Voltage levels
#define MINVOLTAGE10MV_3V0 300 // Minium level for automatic measurements
#define LOWVOLTAGE10MV_3V35 335 // Minimum level for display
#define FULLSOLAR10MV_4V5 450 // Minimum level for automatic DCF77 sync when powered by a solar panel
#define FULLBAT10MV_4V2 420 // Minimum level for automatic DCF77 sync when powered by a battery or USB charger

#define DCF77SYNCHOUR 14 // Hour (local time) for daily DCF77 time sync
#define TEMPERATUREOFFSET 2730 // Offset word<->int in 0.1°C
#define SERIALBAUD 9600 // Serial baud when sending datasets
#define USERTIMEOUT 60 // Set timeout expiration in millis for user inputs
#define MAXDATAAGE 30 // Maximum age in seconds for an valid dataset
#define SENSORDELAY 2 // We need 2 seconds to power up the remperature sensor and get the first value

// Special chars
#define CHAR_CANCEL 247
#define CHAR_OK 254
// Custom chars
#define CHAR_SIGNAL 0
#define CHAR_DEGREEC 1
#define CHAR_DOTS 2
#define CHAR_SELECTION 3
#define CHAR_ROTATION 7
#define CHAR_LAST 7
#define CHAR_EMPTYBATTERY 6
#define CHAR_MAX 7
#define CHAR_MIN 7
#define CHAR_LAST 7
#define CHAR_RUNNER 7
#define CHAR_TIMER 6

// -------- Global variables ------------

// COC-Display WO1602G with ST7032-Controller by I2C
ST7032 g_lcd;
// DCF77 Receiver
DCF77 DCF = DCF77(DCF77_OUT_PIN,digitalPinToInterrupt(DCF77_OUT_PIN));
// Rotary encoder
KY040 g_rotaryEncoder(ROTARY_CLK_PIN,ROTARY_DT_PIN);
SWITCHBUTTON g_switchButton(ROTARY_SW_PIN);

word g_eepromNextIndex = 0; // Next index for dataset in EEPROM (= Dataset entries)

// System clock (One second per tick)
typedef struct {
  unsigned long ticks = 0;
  unsigned long offset = 0;
} t_clock;
volatile t_clock v_clock;

bool g_displayEnabled = false; // True, if display is enabled
bool g_weakTime = true; // True, if first DCF77 is still penting or last sync was not sucessful
unsigned long g_nextRecordUTC = 0; // Next planed sensor measurement
unsigned long g_nextDCF77SyncUTC = 0; // Next planed DCF77 sync
unsigned long g_firstDCF77SyncUTC = 0; // First DCF77 sync (0, if never synced)
#define DCF77DISABLEDINIT false // Set to true, if you do not want scheduled DCF77 syncs
bool g_DCF77disabled = DCF77DISABLEDINIT; // True, when schedules DCF77 syncs are disabled
byte g_lastMCUSR = 0; // Last device reset reasons

// Power sources
enum powerSources { SOLARPOWER, BATTERYPOWER, USBPOWERBANK, MAXPOWERSOURCES };
#define SOLARPOWERINIT SOLARPOWER
powerSources g_powerSource = SOLARPOWERINIT;

// Record modes
enum recordModes { 
  NORECORDING, 
  AT0x00, 
  AT0000, 
  AT0100, 
  AT0200, 
  AT0300, 
  AT0400, 
  AT0500, 
  AT0600, 
  AT0700, 
  AT0800, 
  AT0900, 
  AT1000, 
  AT1100, 
  AT1200, 
  AT1300, 
  AT1400, 
  AT1500, 
  AT1600, 
  AT1700, 
  AT1800,
  AT1900, 
  AT2000, 
  AT2100, 
  AT2200, 
  AT2300,  
  MAXRECORDMODES };
#define RECORDMODEINIT NORECORDING
recordModes g_recordMode = RECORDMODEINIT;

// Request bits for sensor measurements
enum requestBits { NOREQUESTS = 0b000, REQUESTLIVE = 0b001, REQUESTMANUAL = 0b010, REQUESTSCHEDULED = 0b100 };
requestBits g_requestTemperatureBits = NOREQUESTS;

// Dasboards (selection dots will work only, if Dashboard1 to last dashboard are even)
enum DASHBOARDS {
  DISPLAYOFF, // Dishboard0 "Display powered off"
  LATESTDATA, // Dashboard1 Latest
  MINDATA, // Dashboard2 Minimum
  MAXDATA, // Dashboard3 Maximum
  POWER, // Dashboard4 Power
  DATETIME, // Dashboard5 Time
  DCFSYNCTIME, // Dashboard6 First and next time synchronization
  LATESTRECORDING, // Dashboard7 Latest recording
  MENUTIMESET, // Dashboard8 Time/Date...
  MENUDCF77SYNC, // Dashboard9 Time sync...
  MENUEEPROMVIEW, // Dashboard10 View buffer...
  MENUEEPROMSEND, // Dashboard11 Send buffer...
  MENUEEPROMRESET, // Dashboard12 Reset buffer...
  MENUPOWERSOURCE, // Dashboard13 Power source...
  MENURECORDMODE, // Dashboard14 Record mode... (= last dashboard)
  MAXDASHBOARDS
};

typedef struct { // Dataset: time and data
  unsigned long time;
  word data;
} t_dataSet;
#define DATAMININIT TEMPERATUREOFFSET+TEMPERATUREOFFSET
t_dataSet g_minData; // Dataset with lowest data value
#define DATAMAXINIT 0
t_dataSet g_maxData; // Dataset with highest data value
t_dataSet g_lastData = { 0, 0 }; // Latest dataset

// oneWire
OneWire oneWire(DS18B20_DATA_PIN);

// Dallas temperature sensor
DallasTemperature sensors(&oneWire);

// ISR to handle pin change interrupt for D0 to D7
ISR (PCINT2_vect) {
  byte pins = PIND;
  g_rotaryEncoder.setState((pins & 0b00110000)>>4);
  g_rotaryEncoder.checkRotation();
}

// ISR for Timer2 overflow
ISR(TIMER2_OVF_vect) {
  v_clock.ticks++;
}

// Initialize Timer2 as asynchronous 32768 Hz timing source
void timer2_init(void) {
  TCCR2B = 0; //stop Timer 2
  TIMSK2 = 0; // disable Timer 2 interrupts
  ASSR = (1 << AS2); // select asynchronous operation of Timer2
  TCNT2 = 0; // clear Timer 2 counter
  TCCR2A = 0; //normal count up mode, no port output
  TCCR2B = (1 << CS22) | (1 << CS20); // select prescaler 128 => 1 sec between each overflow

  while (ASSR & ((1 << TCN2UB) | (1 << TCR2BUB))); // wait for TCN2UB and TCR2BUB to be cleared

  TIFR2 = (1 << TOV2); // clear interrupt-flag
  TIMSK2 = (1 << TOIE2); // enable Timer2 overflow interrupt
}

// Enable pin change interrupt
void pciSetup(byte pin) {
  *digitalPinToPCMSK(pin) |= bit (digitalPinToPCMSKbit(pin));  // enable pin
  PCIFR  |= bit (digitalPinToPCICRbit(pin)); // clear any outstanding interrupt
  PCICR  |= bit (digitalPinToPCICRbit(pin)); // enable interrupt for the group
}

// Enable WatchdogTimer
void enableWatchdogTimer() {
  /*
   * From Atmel datasheet: "...WDTCSR – Watchdog Timer Control Register...
   * Bit
   * 7 WDIF
   * 6 WDIE
   * 5 WDP3
   * 4 WDCE
   * 3 WDE
   * 2 WDP2
   * 1 WDP1
   * 0 WDP0
   * ...
   * DP3 WDP2 WDP1 WDP0 Number of WDT Oscillator Cycles Typical Time-out at VCC = 5.0V
   * 0 0 0 0 2K (2048) cycles 16ms
   * 0 0 0 1 4K (4096) cycles 32ms
   * 0 0 1 0 8K (8192) cycles 64ms
   * 0 0 1 1 16K (16384) cycles 0.125s
   * 0 1 0 0 32K (32768) cycles 0.25s
   * 0 1 0 1 64K (65536) cycles 0.5s
   * 0 1 1 0 128K (131072) cycles 1.0s
   * 0 1 1 1 256K (262144) cycles 2.0s
   * 1 0 0 0 512K (524288) cycles 4.0s
   * 1 0 0 1 1024K (1048576) cycles 8.0s"
   */
  cli();
  // Set bit 3+4 (WDE+WDCE bits)
  // From Atmel datasheet: "...Within the next four clock cycles, write the WDE and
  // watchdog prescaler bits (WDP) as desired, but with the WDCE bit cleared.
  // This must be done in one operation..."
  WDTCSR = WDTCSR | B00011000;
  // Set Watchdog-Timer duration to 8 seconds
  WDTCSR = B00100001;
  // Enable Watchdog interrupt by WDIE bit and enable device reset via 1 in WDE bit.
  // From Atmel datasheet: "...The third mode, Interrupt and system reset mode, combines the other two modes by first giving an interrupt and then switch to system reset mode. This mode will for instance allow a safe shutdown by saving critical parameters before a system reset..."
  WDTCSR = WDTCSR | B01001000;
  sei();
}

// ISR for the watchdog timer
ISR(WDT_vect) {
  // Enalbe led to show the pending WDT reset
  ledOn();

  // In 8 seconds device will reset
  while(true);
}

// Enable and power on the display
void enableDisplay() {
  if (!g_displayEnabled) {
    // Enable Vcc
    pinMode(LCDVCC_PIN,OUTPUT);
    digitalWrite(LCDVCC_PIN,HIGH);

    g_lcd.begin(16,2); // Has a builtin 240ms delay and starts Wire.begin()
    g_lcd.setContrast(30);
    g_lcd.createChar(CHAR_SIGNAL, c_customChar_Signal);
    g_lcd.createChar(CHAR_DEGREEC, c_customChar_DegreeCelsius);
    g_lcd.createChar(CHAR_DOTS, c_customChar_dots);
    g_lcd.createChar(CHAR_SELECTION, c_customChar_dots);
    g_lcd.display();
    g_displayEnabled = true;
  }
}

// Disable and power off the display
void disableDisplay(bool force = false) {
  if (g_displayEnabled || force) {
    if (g_displayEnabled) g_lcd.noDisplay();
    // Disable Vcc
    digitalWrite(LCDVCC_PIN,LOW);
    pinMode(LCDVCC_PIN,INPUT);
    Wire.end(); // I2C

    // Change I2C pins to input mode
    pinMode(A4,INPUT);
    pinMode(A5,INPUT);
    g_displayEnabled = false;
  }
}

// Power on led
void ledOn() {
  pinMode(LED_PIN,OUTPUT);
  digitalWrite(LED_PIN,HIGH);
}

// Power off led
void ledOff() {
  digitalWrite(LED_PIN,LOW);
  pinMode(LED_PIN,INPUT);
}

// Schedule next DCF77 sync
void scheduleNextDCF77Sync(unsigned long currentTimeUTC) {
  unsigned long localTime;
  if (g_firstDCF77SyncUTC == 0) { // If time was never set/synced
    // Schedule next DCF77 sync in 4h
    g_nextDCF77SyncUTC = currentTimeUTC + 4*SECS_PER_HOUR;
  } else {
    localTime = UTCtoLocalTime(currentTimeUTC);
    g_nextDCF77SyncUTC = localTimeToUTC(tmConvert_t( // Next day
      year(localTime),
      month(localTime),
      day(localTime),
      DCF77SYNCHOUR,0,0)+SECS_PER_DAY);
  }
}

// Get time from DCF77 module
void setTimeFromDCF77() {
  #define MAXSTRDATALENGTH 16
  char strData[MAXSTRDATALENGTH+1];
  #define DCF77TIMEOUT 900
  int secondsCounter=0;
  unsigned long lastDisplayRefreshMS;
  unsigned long lastSignalMS = 0;
  bool exitLoop = false;
  bool delayedSignalDisplay = false;
  byte lastPinState = 0;

  // DCF77 needs millis() and millis() works only in IDLE sleep
  // ATmega328 seems to need 50% power in IDLE mode
  // in comparison with normal operational mode
  set_sleep_mode(SLEEP_MODE_IDLE);
  // Save ADC status
  int oldADCSRA = ADCSRA; // Backup current ADC
  // Disable ADC, because we don't need analog ports during the DCF77 sync
  ADCSRA = 0;

  // Enable Vcc for the DCF77 module
  pinMode(DCF77_VCC_PIN,OUTPUT);
  digitalWrite(DCF77_VCC_PIN,HIGH);
  // Enable interrupt for DCF77 out pin
  DCF.Start();
  g_weakTime = true;

  // Reinit display after previous powering off
  enableDisplay();

  g_lcd.clear();
  g_lcd.setCursor(0,0);
  g_lcd.print(STR_DCF77SYNC);
  g_lcd.blink();
  g_lcd.setCursor(15,1);
  g_lcd.write(CHAR_CANCEL);
  g_lcd.setCursor(15,1);

  lastDisplayRefreshMS = millis();
  do {
    wdt_reset();
    checkUSBChargerTrigger();

    // Display DCF77 signal as char on display
    byte output = digitalRead(DCF77_OUT_PIN);
    if ((output != lastPinState) && (output == HIGH)) { // Rising
      if ((millis()-lastSignalMS > 900) // normal 1s interval
        && (millis()-lastSignalMS < 1100)) {
        g_lcd.setCursor(15,0);
        g_lcd.write(CHAR_SIGNAL);
      } else {
        if ((millis()-lastSignalMS > 1900) // 2s pause
          && (millis()-lastSignalMS < 2100)) {
          g_lcd.setCursor(15,0);
          g_lcd.write(CHAR_SIGNAL);
        } else {
          g_lcd.setCursor(15,0);
          g_lcd.print(" ");
        }
      }
      g_lcd.setCursor(15,1);
      lastSignalMS = millis();
    }
    lastPinState = output;

    // Show countdown
    if (millis()- lastDisplayRefreshMS > 1000) {
      secondsCounter++;
      g_lcd.setCursor(0,1);
      snprintf(strData,MAXSTRDATALENGTH+1,"%3i/%is",secondsCounter,DCF77TIMEOUT);
      g_lcd.print(strData);
      g_lcd.setCursor(15,1);
      lastDisplayRefreshMS = millis();
    }

    time_t DCFtime = DCF.getTime(); // Check if DCF77 time was received
    if (DCFtime!=0) {
      // Set received time
      setCurrentTimeUTC(localTimeToUTC(DCFtime));
      // Store time of first DCF77 sync
      if (g_firstDCF77SyncUTC == 0) {
        g_firstDCF77SyncUTC = localTimeToUTC(DCFtime);
      }
      // Schedule next data recording
      scheduleNextRecord();
      g_weakTime = false;
      exitLoop = true;
    }

    // User input timeout expired?
    if (secondsCounter >= DCF77TIMEOUT) exitLoop = true;

    switch (g_switchButton.getButton()) {
      case SWITCHBUTTON::SHORTPRESSED:
      case SWITCHBUTTON::LONGPRESSED:
        exitLoop = true;
        break;
    }
    if (!exitLoop) {
      // Go to IDLE sleep
      power_spi_disable(); // Disable SPI
      power_usart0_disable(); // Disable the USART 0 module
      sleep_mode();
    }
  } while (!exitLoop);

  g_lcd.clear();
  g_lcd.noBlink();

  DCF.Stop();
  // Power off Vcc for DCF77 module
  digitalWrite(DCF77_VCC_PIN,LOW);
  pinMode(DCF77_VCC_PIN,INPUT);

  // Restore ADC
  ADCSRA = oldADCSRA;
}

// Set clock to seconds since 1.1.1970
void setCurrentTimeUTC(unsigned long clock) {
  cli();
  v_clock.offset = clock - v_clock.ticks;
  sei();
}

// Get clock in seconds since 1.1.1970
unsigned long getCurrentTimeUTC() {
  cli();
  unsigned long clock = v_clock.ticks + v_clock.offset;
  sei();
  return clock;
}

// Get seconds since device startup
unsigned long seconds() {
  cli();
  unsigned long ticks = v_clock.ticks;
  sei();
  return ticks;
}

// Create time_t from time components
time_t tmConvert_t(int YYYY, byte MM, byte DD, byte hh, byte mm, byte ss)
{
  tmElements_t tmSet;
  tmSet.Year = YYYY - 1970;
  tmSet.Month = MM;
  tmSet.Day = DD;
  tmSet.Hour = hh;
  tmSet.Minute = mm;
  tmSet.Second = ss;
  return makeTime(tmSet);
}

// Convert Germany local time to UTC
time_t localTimeToUTC(time_t localTime) {
  if (summertime_EU(year(localTime),month(localTime),day(localTime),hour(localTime),1)) {
    return localTime-7200UL; // Summer time
  } else {
    return localTime-3600UL; // Winter time
  }
}

// Convert UTC to Germany local time
time_t UTCtoLocalTime(time_t UTC) {
  if (summertime_EU(year(UTC),month(UTC),day(UTC),hour(UTC),0)) {
    return UTC+7200UL; // Summer time
  } else {
    return UTC+3600UL; // Winter time
  }
}

// Update latest dataset from sensor if requested
void updateTemperature() {
  static bool sensorEnabled = false;
  static bool requestPending = false;
  static unsigned long lastMeasurement;
  #define CONVERSIONMAXDELAY 3 // Timeout expiration in seconds for isConversionComplete

  // Power off sensor, if no requests are set
  if (g_requestTemperatureBits == NOREQUESTS) {
    // Disable Vcc for sensor
    digitalWrite(DS18B20_VCC_PIN,LOW);
    pinMode(DS18B20_VCC_PIN,INPUT);
    sensorEnabled = false;
    requestPending = false;
  }

  // Data ready from previous requests?
  if (requestPending && (sensors.isConversionComplete()
    || (seconds() - lastMeasurement >= CONVERSIONMAXDELAY))) {
    bool responseOK = true;
    float response = sensors.getTempCByIndex(0);
    if (response == DEVICE_DISCONNECTED_C) responseOK = false;
    if (response == 85.0f) responseOK = false;
    if (responseOK) {
      g_lastData.time = getCurrentTimeUTC();
      g_lastData.data = round(response*10)+TEMPERATUREOFFSET;
      // Check for minimum or maximum temperature
      if (g_firstDCF77SyncUTC != 0) {
        if (g_lastData.data < g_minData.data) {
          g_minData = g_lastData;
          setEEPROMMinData(g_minData);
        }
        if (g_lastData.data > g_maxData.data) {
          g_maxData = g_lastData;
          setEEPROMMaxData(g_maxData);
        }
      }
    }
    requestPending = false;
  }

  // New requests
  if (!requestPending && (g_requestTemperatureBits != NOREQUESTS)
    && (getBandgap() >= MINVOLTAGE10MV_3V0)) {
    if (!sensorEnabled) {
      pinMode(DS18B20_VCC_PIN,OUTPUT);
      digitalWrite(DS18B20_VCC_PIN,HIGH);
      sensors.begin();
      sensors.setWaitForConversion(false); // Async mode
      sensorEnabled = true;
    }
    sensors.requestTemperatures(); // Send the command to get temperature sensor
    requestPending = true;
    lastMeasurement = seconds();
  }
}

/* Get supercap voltage Vcap in 10mV (build average from previous values)
 * We can not meassure voltages below 3.3V here
 * because analogRead depends on the microcontroller Vcc
 * and the voltage decreases also when the supercap
 * is below ~3.4V.
 */
int getVcap() {
  int value;
  #define MAXSAMPLES 5
  #define INVALIDVOLTAGE 0
  static int lastValues[MAXSAMPLES] = {
    INVALIDVOLTAGE,
    INVALIDVOLTAGE,
    INVALIDVOLTAGE,
    INVALIDVOLTAGE,
    INVALIDVOLTAGE
  };
  static byte valuePosition = 0;

  // Enable Vcap measurement by enabling ground for the Vcap voltage divider
  pinMode(VBAT_ENABLE_PIN,OUTPUT);
  digitalWrite(VBAT_ENABLE_PIN,LOW);
  value = analogRead(VBAT_PIN);
  // Disable Vcc measurement by disabling ground
  pinMode(VBAT_ENABLE_PIN,INPUT);

  // Store value in list
  lastValues[valuePosition] = value;
  if (valuePosition < MAXSAMPLES-1) valuePosition++; else valuePosition = 0;
  // Build average with previous values
  int long sum = 0;
  byte samples = 0;
  for (byte i=0; i<MAXSAMPLES;i++) {
    if (lastValues[i] != INVALIDVOLTAGE) {
      sum +=lastValues[i];
      samples++;
    }
  }
  if (samples != 0) return map(sum/samples,658,981,330,500); else return INVALIDVOLTAGE;
}

// Create a periodically load (~100 mA for 1s) to prevent the USB powerbank from going to sleep
void checkUSBChargerTrigger() {
  int Vcap10mV;
  static unsigned long lastTrigger = seconds();
  static bool loadEnabled = false;
  static unsigned long lastCheck = seconds();
  #define TRIGGERINTERVAL 30 // Seconds between loads (time depends on you USB powerbank)

  if (g_powerSource == USBPOWERBANK) { // USB powerbank source mode
    if (seconds() - lastTrigger >= TRIGGERINTERVAL) { // Time for load
      // Get supply voltage
      Vcap10mV = getVcap();
      // Every trigger costs ~2.6mA@3.3V=~9mW energy for one second
      // Trigger only if voltage is high enough.
      if (Vcap10mV > FULLBAT10MV_4V2) {
        digitalWrite(USBTRIGGER_PIN,HIGH); // Enable load by a transistor
        loadEnabled = true;
      }
      lastTrigger = seconds();
    } else {
      if (loadEnabled &&
        (seconds() != lastTrigger)) { // Do not disable load in second 0
        digitalWrite(USBTRIGGER_PIN,LOW); // Disable load
        loadEnabled = false;
      }
    }
  } else { // For all other power sources
    if (loadEnabled) {
      digitalWrite(USBTRIGGER_PIN,LOW); // Disable load
      loadEnabled = false;
    }
  }
}

void setup() {
  g_lastMCUSR = MCUSR; // Store reset reason
  MCUSR=0; // Reset reset reasons
  enableWatchdogTimer(); // Watchdog timer (Start at the begin of setup to prevent a boot loop after a WDT reset)

  // Pin for USB charger trigger
  pinMode(USBTRIGGER_PIN,OUTPUT);
  digitalWrite(USBTRIGGER_PIN,LOW);

  // Set pin change interrupts for rotary encoder
  pciSetup(ROTARY_SW_PIN);
  pciSetup(ROTARY_CLK_PIN);
  pciSetup(ROTARY_DT_PIN);

  // Disable digital input for A3 (=VBAT_PIN) to save power (don't know how much)
  bitSet(DIDR0, ADC3D);

  // Enable timer2 for clock
  timer2_init();

  // Create EEPROM header when needed
  if (!checkEEPROMHeader()) writeEEPROMHeader();
  // Get dataset position in EEPROM
  g_eepromNextIndex = getEEPROMNextIndex();
  // Get values from EEPROM
  getEEPROMMinData(g_minData);
  getEEPROMMaxData(g_maxData);
  g_powerSource = getEEPROMPowerSource();
  g_recordMode = getEEPROMRecordMode();
}

void loop() {
  unsigned long currentTimeUTC;
  byte backupADCSRA;
  bool displayReady = false;
  int Vcap10mV;
  static byte dashboard = DISPLAYOFF;
  static byte lastDashboard = MAXDASHBOARDS;
  bool forceDisplayRefresh;
  static unsigned long lastUserInputS = seconds();
  t_dataSet dataSet;
  static bool lowVoltageMode = false;
  static unsigned long lastLEDFlash = 0;

  wdt_reset();

  // Get current time
  currentTimeUTC = getCurrentTimeUTC();
  // Get supply voltage
  Vcap10mV = getVcap();
  // Update temperature dataset from sensor, when requested
  updateTemperature();
  // When using a USB powerbank a power source
  // create a periodically load to prevent the
  // USB powerbank from to going sleep
  checkUSBChargerTrigger();

  // Power off display after user input timeout expiration
  if (seconds() - lastUserInputS > USERTIMEOUT) dashboard = DISPLAYOFF;

  // lowVoltageMode hysteris
  if ((Vcap10mV < LOWVOLTAGE10MV_3V35) && !lowVoltageMode) {
    lowVoltageMode = true;
  }
  if ((Vcap10mV >= LOWVOLTAGE10MV_3V35*1.01f) && lowVoltageMode) {
    lowVoltageMode = false;
  }

  // Change dashboard by rotary encoder
  byte rotation = g_rotaryEncoder.getAndResetLastRotation();
  switch (rotation) {
    case KY040::CLOCKWISE: // Next dashboard
      if (dashboard < MAXDASHBOARDS-1) dashboard ++; else dashboard = DISPLAYOFF;
      lastUserInputS = seconds();
      break;
    case KY040::COUNTERCLOCKWISE: // Previous dashboard
      if (dashboard > DISPLAYOFF) dashboard--; else dashboard = MAXDASHBOARDS-1;
      lastUserInputS = seconds();
      break;
  }

  // Button press
  switch (g_switchButton.getButton()) {
    case SWITCHBUTTON::SHORTPRESSED:
      switch (dashboard) {
        case MENUTIMESET: // Set the time and date manually
          if (!lowVoltageMode) {
            if (!g_displayEnabled) enableDisplay();
            setManualTime(UTCtoLocalTime(getCurrentTimeUTC()));
            lastDashboard = DISPLAYOFF; // Trigger complete display refresh
          }
          break;
        case MENUDCF77SYNC: // Starts a DCF77 time synchronization
          if (!lowVoltageMode) {
            if (!g_displayEnabled) enableDisplay();
            setTimeFromDCF77();
            scheduleNextDCF77Sync(getCurrentTimeUTC());
            // Power off display
            dashboard = DISPLAYOFF;
          }
          break;
        case MENUEEPROMVIEW: // View all datasets stored in persistent buffer/EEPROM
          if (!lowVoltageMode) {
            if (!g_displayEnabled) enableDisplay();
            viewEEPROM();
            lastDashboard = DISPLAYOFF; // Trigger complete display refresh
          }
          break;
        case MENUEEPROMSEND: // Sends all datasets from persistent buffer/EEPROM to serial
          if (!lowVoltageMode) {
            if (!g_displayEnabled) enableDisplay();
            sendEEPROMDatasetsToSerial();
            showInfo(STR_DONE);
            lastDashboard = DISPLAYOFF; // Trigger complete display refresh
          }
          break;
        case MENUEEPROMRESET: // Deletes all datasets from persistent buffer/EEPROM
          if (!lowVoltageMode) {
            if (!g_displayEnabled) enableDisplay();
            resetEEPROMDatasets();
            showInfo(STR_DONE);
            lastDashboard = DISPLAYOFF; // Trigger complete display refresh
          }
          break;
        case MENUPOWERSOURCE: // Select power source
          if (!lowVoltageMode) {
            if (!g_displayEnabled) enableDisplay();
            setPowerSource();
            lastDashboard = DISPLAYOFF; // Trigger complete display refresh
          }
          break;
        case MENURECORDMODE: // Select record mode
          if (!lowVoltageMode) {
            if (!g_displayEnabled) enableDisplay();
            setRecordMode();
            lastDashboard = DISPLAYOFF; // Trigger complete display refresh
          }
          break;
      default:
        // Toggle between latest data dashboard and disabled display
        if (dashboard == DISPLAYOFF) dashboard = LATESTDATA; else dashboard = DISPLAYOFF;
      }
      lastUserInputS = seconds();
      break;
    case SWITCHBUTTON::LONGPRESSED:
      if (!lowVoltageMode) switch (dashboard) {
        case MINDATA: // Reset dataset containing minimum temperature
          g_minData = { 0, DATAMININIT };
          setEEPROMMinData(g_minData);
          g_lastData = { 0,0 };
          break;
        case MAXDATA: // Reset dataset containing maximum temperature
          g_maxData = { 0, DATAMAXINIT };
          setEEPROMMinData(g_maxData);
          g_lastData = { 0,0 };
          break;
        default:
          g_requestTemperatureBits |= REQUESTMANUAL; // Request new data from sensor
      }
      lastUserInputS = seconds();
      break;
  }

  // Enable/disable display usage dependent on dashboard
  if (dashboard == DISPLAYOFF) displayReady = false; else displayReady = true;

  // Display dashboard
  if ((!lowVoltageMode) && (displayReady)) { // Everything ready for display
    if (!g_displayEnabled) enableDisplay(); // Reinit display after previous powering off
    if (lastDashboard != dashboard) { // Dashboard has changed
      g_lcd.clear(); // Clear screen
      forceDisplayRefresh = true; // Force display updates
      // Clear request bit for live requests when changing dashboard and last dashboard was "last dataset"
      if (dashboard != LATESTDATA) g_requestTemperatureBits &= ~REQUESTLIVE;
    } else forceDisplayRefresh = false;

    byte remainingTime = (USERTIMEOUT - abs(seconds() - lastUserInputS) +19) / 20;
    switch(dashboard) {
      case LATESTDATA: // Dashboard1 Latest
        showLastestData(forceDisplayRefresh);
        g_requestTemperatureBits |= REQUESTLIVE; // Set request bit for sensor
        break;
      case MINDATA: // Dashboard2 Minimum
        showMinData(forceDisplayRefresh);
        break;
      case MAXDATA: // Dashboard3 Maximum
        showMaxData(forceDisplayRefresh);
        break;
      case POWER: // Dashboard4 Power
        showVcc(Vcap10mV,forceDisplayRefresh);
        showRemainingRuntime(Vcap10mV,forceDisplayRefresh);
        showUpTime(forceDisplayRefresh);
        lastUserInputS = seconds(); // Prevent automatic display power off in this dashboard
        break;
      case DATETIME: // Dashboard5 Time
        showTime(UTCtoLocalTime(currentTimeUTC),true,forceDisplayRefresh);
        showDate(UTCtoLocalTime(currentTimeUTC),forceDisplayRefresh);
        showResetReason(forceDisplayRefresh);
        break;
      case DCFSYNCTIME: // Dashboard6 First and next time synchronization
        showDCFSyncTime(forceDisplayRefresh);
        break;
      case LATESTRECORDING: // Dashboard7 Latest recording
        showLatestRecording(forceDisplayRefresh);
        break;
      case MENUTIMESET: // Dashboard8 Time/Date...
        if (forceDisplayRefresh) {
          g_lcd.setCursor(0,0);
          g_lcd.print(STR_TIMEDATE);
          g_lcd.print("...");
        }
        break;
      case MENUDCF77SYNC: // Dashboard9 Time sync...
        if (forceDisplayRefresh) {
          g_lcd.setCursor(0,0);
          g_lcd.print(STR_DCF77SYNC);
          g_lcd.print("...");
        }
        break;
      case MENUEEPROMVIEW: // Dashboard10 View buffer...
        if (forceDisplayRefresh) {
          g_lcd.setCursor(0,0);
          g_lcd.print(STR_VIEWBUFFER);
          g_lcd.print("...");
        }
        break;
      case MENUEEPROMSEND: // Dashboard11 Send buffer...
        if (forceDisplayRefresh) {
          g_lcd.setCursor(0,0);
          g_lcd.print(STR_SENDBUFFER);
          g_lcd.print("...");
        }
        break;
      case MENUEEPROMRESET: // Dashboard12 Reset buffer...
        if (forceDisplayRefresh) {
          g_lcd.setCursor(0,0);
          g_lcd.print(STR_RESETBUFFER);
          g_lcd.print("...");
        }
        break;
      case MENUPOWERSOURCE: // Dashboard13 Power source...
        if (forceDisplayRefresh) {
          g_lcd.setCursor(0,0);
          g_lcd.print(STR_POWERSOURCE);
          g_lcd.print("...");
        }
        break;
      case MENURECORDMODE: // Dashboard14 Record mode...
        if (forceDisplayRefresh) {
          g_lcd.setCursor(0,0);
          g_lcd.print(STR_RECORDMODE);
          g_lcd.print("...");
        }
        break;
    }
    showDots(dashboard,remainingTime,forceDisplayRefresh);
    lastDashboard = dashboard;
  } else { // Something disabled the display (Vcc or dashboard)
    // Flash led, when voltage was too low to enable the display
    if (lowVoltageMode && (displayReady)) {
      if (seconds()-lastLEDFlash > 2) { // Prevent too frequent flashes
        ledOn();
        set_sleep_mode(SLEEP_MODE_IDLE);
        unsigned long startMS = millis();
        // Sleep 50ms
        while(millis()-startMS < 50) sleep_mode();
        ledOff();
        lastLEDFlash = seconds();
      }
      dashboard = DISPLAYOFF;
    }
    // Power off display
    disableDisplay();
    g_requestTemperatureBits &= ~REQUESTLIVE; // Clear request bit
    lastDashboard = DISPLAYOFF;
  }

  // Store dataset when scheduled
  if (g_recordMode != NORECORDING) {
    if ((g_firstDCF77SyncUTC != 0) && (currentTimeUTC >= g_nextRecordUTC)) {
      g_requestTemperatureBits |= REQUESTSCHEDULED; // Set request bit for sensor
      if ((currentTimeUTC - g_lastData.time <= MAXDATAAGE)
        && (g_lastData.time >= g_nextRecordUTC + SENSORDELAY)) {
        addEEPROMDataset(g_lastData);
        g_requestTemperatureBits &= ~REQUESTSCHEDULED; // Clear request bit
        scheduleNextRecord();
      }
    }
  }

  // Check for a pending manual data request
  if ((g_firstDCF77SyncUTC != 0) &&
    ((g_requestTemperatureBits & REQUESTMANUAL) == REQUESTMANUAL)) {
    // Store dataset in EEPROM
    dataSet = { 0, 0 };
    if (g_eepromNextIndex > 0) getEEPROMDatasetAtIndex(g_eepromNextIndex-1, dataSet); //Prevents duplicate entries
    // Accept only changed and actual data
    if (((g_lastData.time != dataSet.time)
      || (g_lastData.data != dataSet.data))
      && (currentTimeUTC - g_lastData.time <= MAXDATAAGE)) {
      if (addEEPROMDataset(g_lastData) && !lowVoltageMode) {
        if (!g_displayEnabled) enableDisplay();
        showInfo(STR_SAVED);
        lastDashboard = DISPLAYOFF; // Trigger complete display refresh
      }
      g_requestTemperatureBits &= ~REQUESTMANUAL; // Clear request bit
    }
  }

  // Start DCF77 sync dependent on time and Vcc
  bool FullVcc = false;
  if (Vcap10mV >= FULLBAT10MV_4V2) FullVcc = true;
  // When powered by solar we need a bigger power buffer
  if ((g_powerSource == SOLARPOWER) && (Vcap10mV < FULLSOLAR10MV_4V5)) FullVcc = false;
  if (FullVcc && !g_DCF77disabled && (currentTimeUTC >= g_nextDCF77SyncUTC)) { // Time and Vcc ready?
    setTimeFromDCF77();
    // Power off display
    disableDisplay();
    lastDashboard = DISPLAYOFF;
    dashboard = DISPLAYOFF;
    scheduleNextDCF77Sync(getCurrentTimeUTC());
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
    power_spi_disable(); // Disable SPI
    power_usart0_disable(); // Disable the USART 0 module (disables Serial....)

    // Backup and disable ADC
    backupADCSRA = ADCSRA;
    ADCSRA = 0; // Delta 190uA@3.3V

    sleep_mode();
    // Restore ADC
    ADCSRA = backupADCSRA;
  }
}
