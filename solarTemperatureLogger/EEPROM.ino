/* ----------- Stuff for EEPROM ----------
 * License: 2-Clause BSD License
 * Copyright (c) 2024 codingABI
 */

// EEPROM signature, version and base address
#define EEPROMSIGNATURE 0x16
#define EEPROMVERSION 2
#define EEPROMBASEADDRESS 0
#define EEPROMDATASETLENGTH 6
#define EEPROMHEADERLENGTH 18

// Check, if EEPROM is initiated
bool checkEEPROMHeader() {
  int addr = EEPROMBASEADDRESS+2;
  if (EEPROM.read(addr++) != EEPROMSIGNATURE) return false;
  if (EEPROM.read(addr++) != EEPROMVERSION) return false;
  return true;
}

// Initiate EEPROM
void writeEEPROMHeader() {
  int addr = EEPROMBASEADDRESS;
  for (int i=0;i<2;i++) EEPROM.update(addr++, 0); // Two byte for index position
  EEPROM.update(addr++, EEPROMSIGNATURE);
  EEPROM.update(addr++, EEPROMVERSION);
  EEPROM.update(addr++, SOLARPOWERINIT & 0xff);
  EEPROM.update(addr++, RECORDMODEINIT & 0xff);
  for (int i=0;i<4;i++) EEPROM.update(addr++, 0); // UTC init
  EEPROM.update(addr++, (DATAMININIT >> 8) & 0xff);
  EEPROM.update(addr++, DATAMININIT & 0xff);
  for (int i=0;i<4;i++) EEPROM.update(addr++, 0); // UTC init
  EEPROM.update(addr++, (DATAMAXINIT >> 8) & 0xff);
  EEPROM.update(addr++, DATAMAXINIT & 0xff);
  g_eepromNextIndex = 0;
}

// Clear every cell in EEPROM
void clearEEPROM() {
  for (int i = 0 ; i < EEPROM.length() ; i++) {
    EEPROM.update(i, 0);
  }
}

// Reset all datasets in EEPROM
void resetEEPROMDatasets() {
  int addr = EEPROMBASEADDRESS;
  for (int i=0;i<2;i++) EEPROM.update(addr++, 0); // Two byte for index position
  g_eepromNextIndex = 0;
}

// Return fill state for EEPROM
byte getEEPROMpercent() {
  return 100*g_eepromNextIndex/getEEPROMMaxDataSets();
}

// Check, if EEPROM is full
bool isEEPROMFull() {
  if (g_eepromNextIndex >= ((EEPROM.length()-EEPROMHEADERLENGTH-EEPROMBASEADDRESS)/EEPROMDATASETLENGTH)) return true; else return false;
}

#define POWERSOURCEADDR EEPROMBASEADDRESS+4
// Get power source from EEPROM
powerSources getEEPROMPowerSource() {
  powerSources powerSource;
  powerSource =  EEPROM.read(POWERSOURCEADDR);
  if (powerSource >= MAXPOWERSOURCES) powerSource = SOLARPOWERINIT;
  return powerSource;
}

// Set power source in EEPROM
void setEEPROMPowerSource(powerSources powerSource) {
  EEPROM.update(POWERSOURCEADDR,powerSource);
}

#define RECORDMODEADDR EEPROMBASEADDRESS+5
// Get record mode from EEPROM
recordModes getEEPROMRecordMode() {
  recordModes recordMode;
  recordMode =  EEPROM.read(RECORDMODEADDR);
  if (recordMode >= MAXRECORDMODES) recordMode = RECORDMODEINIT;
  return recordMode;
}

// Set record mode in EEPROM
void setEEPROMRecordMode(recordModes recordMode) {
  EEPROM.update(RECORDMODEADDR,recordMode);
}

#define MINADDR EEPROMBASEADDRESS+6
// Get min dataset from EEPROM
void getEEPROMMinData(t_dataSet &dataSet) {
  getEEPROMDataSet(dataSet,MINADDR);
}

// Set min dataset in EEPROM
void setEEPROMMinData(t_dataSet dataSet) {
  setEEPROMDataSet(dataSet,MINADDR);
}

#define MAXADDR EEPROMBASEADDRESS+12
// Get max dataset from EEPROM
void getEEPROMMaxData(t_dataSet &dataSet) {
  getEEPROMDataSet(dataSet,MAXADDR);
}

// Set max dataset in EEPROM
void setEEPROMMaxData(t_dataSet dataSet) {
  setEEPROMDataSet(dataSet,MAXADDR);
}

// Get number for datasets that can be stored in EEPROM
byte getEEPROMMaxDataSets() {
  return ((EEPROM.length()-EEPROMHEADERLENGTH-EEPROMBASEADDRESS)/EEPROMDATASETLENGTH);
}

// Get dataset from address
void getEEPROMDataSet(t_dataSet &dataSet,word addr) {
  dataSet.time = ((unsigned long) EEPROM.read(addr++) << 24) +
    ((unsigned long) EEPROM.read(addr++) << 16) +
    ((unsigned long) EEPROM.read(addr++) << 8) +
    (unsigned long) EEPROM.read(addr++);
  dataSet.data = ((word)EEPROM.read(addr++)<<8)+EEPROM.read(addr++);
}

// Set dataset at address
void setEEPROMDataSet(t_dataSet dataSet,word addr) {
  EEPROM.update(addr++,(dataSet.time >> 24) & 0xff);
  EEPROM.update(addr++,(dataSet.time >> 16) & 0xff);
  EEPROM.update(addr++,(dataSet.time >> 8) & 0xff);
  EEPROM.update(addr++,dataSet.time & 0xff);
  EEPROM.update(addr++,(dataSet.data>>8) & 0xff);
  EEPROM.update(addr++,dataSet.data & 0xff);
}

// Get index of next dataset in EEPROM
word getEEPROMNextIndex() {
  word addr = EEPROMBASEADDRESS;
  return ((EEPROM.read(addr++)<<8)+EEPROM.read(addr++));
}

// Get dataset from EEPROM at index
bool getEEPROMDatasetAtIndex(word index, t_dataSet &dataSet) {
  if (index >= (EEPROM.length()-EEPROMHEADERLENGTH-EEPROMBASEADDRESS)/EEPROMDATASETLENGTH) return false;

  int addr = EEPROMBASEADDRESS+EEPROMHEADERLENGTH+(index)*EEPROMDATASETLENGTH;
  getEEPROMDataSet(dataSet,addr);
  return(true);
}

// Add dataset to EEPROM
bool addEEPROMDataset(t_dataSet &dataSet) {
  if (isEEPROMFull()) return false;

  int addr = EEPROMBASEADDRESS+EEPROMHEADERLENGTH+g_eepromNextIndex*EEPROMDATASETLENGTH;

  setEEPROMDataSet(dataSet,addr);

  g_eepromNextIndex++;
  addr = EEPROMBASEADDRESS;
  EEPROM.update(addr++, (g_eepromNextIndex>>8) & 0xff);
  EEPROM.update(addr++, g_eepromNextIndex & 0xff);

  return true;
}

// Send all datasets to serial
void sendEEPROMDatasetsToSerial() {
  #define MAXSTRDATALENGTH 40
  char strData[MAXSTRDATALENGTH+1];
  unsigned long timeUTC;
  word value;
  t_dataSet dataSet;

  // Enable usart0 to get Serial working
  power_usart0_enable();

  Serial.begin(SERIALBAUD);

  Serial.println("BEGIN");
  // Send header
  Serial.println("UTC time;Degree celsius");
  // Send all datasets found in EEPROM
  for (int i=0;i<g_eepromNextIndex;i++) {
    wdt_reset();

    getEEPROMDatasetAtIndex(i, dataSet);
    int temperature=dataSet.data-TEMPERATUREOFFSET;
    snprintf(strData,MAXSTRDATALENGTH+1,"%04d-%02d-%02d %02d:%02d:%02d;%i,%01i",
      year(dataSet.time),
      month(dataSet.time),
      day(dataSet.time),
      hour(dataSet.time),
      minute(dataSet.time),
      second(dataSet.time),
      temperature/10,abs(temperature%10));
    Serial.println(strData);
  }
  Serial.println("END");
  Serial.flush();
  Serial.end();

  // Disable usart0
  delay(1000);
  power_usart0_disable();
}
