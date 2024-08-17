/* ----------- Definitions for custom chars ----------
 * License: 2-Clause BSD License
 * Copyright (c) 2024 codingABI
 *
 * created with https://maxpromer.github.io/LCD-Character-Creator/
 */

const byte c_customChar_Signal[8] = { // Signal
  B11111,
  B10001,
  B10001,
  B01010,
  B00100,
  B00100,
  B00000,
  B00000
};

const byte c_customChar_DegreeCelsius[8] = { // Degree celsius
  B01000,
  B10100,
  B01000,
  B00011,
  B00100,
  B00100,
  B00011,
  B00000
};

const byte c_customChar_Rotation[8] = { // Rotation
  B00000,
  B01100,
  B01101,
  B10001,
  B10001,
  B10110,
  B00110,
  B00000,
};

const byte c_customChar_LastItem[8] = { // Last item
  B00000,
  B00000,
  B10001,
  B11001,
  B11101,
  B11001,
  B10001,
  B00000
};

const byte c_customChar_emptyBattery[8] = { // Empty battery
  B01100,
  B11110,
  B10010,
  B10010,
  B10010,
  B10010,
  B11110,
  B00000
};

const byte c_customChar_Max[8] = { // Max
  B11111,
  B00000,
  B00100,
  B01110,
  B11111,
  B00100,
  B00100,
  B00000
};

const byte c_customChar_Min[8] = { // Min
  B00100,
  B00100,
  B11111,
  B01110,
  B00100,
  B00000,
  B11111,
  B00000
};

const byte c_customChar_dots[8] = { // Selection dots
  B00000,
  B00000,
  B00000,
  B00000,
  B00000,
  B00000,
  B10010,
  B10010
};

const byte c_customChar_Last[8] = { // Last data
  B00000,
  B00000,
  B10000,
  B11000,
  B11100,
  B11000,
  B10000,
  B00000
};

const byte c_customChar_Timer[8] = { // timer
  B01110,
  B00100,
  B01110,
  B10101,
  B11101,
  B10001,
  B01110,
  B00000,
};

const byte c_customChar_Runner[8] = { // Runner
  B01100,
  B01110,
  B01100,
  B00111,
  B00100,
  B00100,
  B01011,
  B10000
};
