#include <Arduino.h>
#include <Wire.h>
#include <avr/pgmspace.h>

// ATtiny412: PA1 = SDA (physical pin 4), PA2 = SCL (pin 5).
// SSD1306 128x32 OLED: 0x3C; INA219: 0x40.
constexpr uint8_t OLED_ADDR = 0x3C;
constexpr uint8_t INA_ADDR  = 0x40;
constexpr uint8_t OLED_WIDTH = 128;
constexpr uint8_t CHUNK_CHARS = 4;  // 25 Wire bytes including control byte
constexpr uint8_t VALUE_COL = 60;
constexpr uint8_t VALUE_CHARS = 11;

// Compact 5x7 font: digits, space, decimal, colon, minus, and units.
// Order: 0123456789 .:-AVWhsm (20 glyphs, 100 flash bytes).
const uint8_t font5x7[] PROGMEM = {
  0x3E,0x51,0x49,0x45,0x3E, // 0
  0,0x42,0x7F,0x40,0,       // 1
  0x42,0x61,0x51,0x49,0x46, // 2
  0x21,0x41,0x45,0x4B,0x31, // 3
  0x18,0x14,0x12,0x7F,0x10, // 4
  0x27,0x45,0x45,0x45,0x39, // 5
  0x3C,0x4A,0x49,0x49,0x30, // 6
  1,0x71,9,5,3,             // 7
  0x36,0x49,0x49,0x49,0x36, // 8
  6,0x49,0x49,0x29,0x1E,    // 9
  0,0,0,0,0,                // space
  0,0x60,0x60,0,0,          // .
  0,0x36,0x36,0,0,          // :
  8,8,8,8,8,                // -
  0x7E,0x11,0x11,0x11,0x7E, // A
  0x1F,0x20,0x40,0x20,0x1F, // V
  0x3F,0x40,0x38,0x40,0x3F, // W
  0x7F,8,4,4,0x78,          // h
  0x48,0x54,0x54,0x54,0x20, // s
  0x7C,4,0x18,4,0x78        // m
};
static_assert(sizeof(font5x7) == 20 * 5, "Compact font length error");

uint8_t glyphIndex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  switch (c) {
    case '.': return 11;
    case ':': return 12;
    case '-': return 13;
    case 'A': return 14;
    case 'V': return 15;
    case 'W': return 16;
    case 'h': return 17;
    case 's': return 18;
    case 'm': return 19;
    default: return 10; // space and unsupported characters
  }
}

void oledCmd(uint8_t cmd) {
  Wire.beginTransmission(OLED_ADDR);
  Wire.write((uint8_t)0x00);
  Wire.write(cmd);
  Wire.endTransmission();
}

void oledWindow(uint8_t col, uint8_t page, uint8_t width) {
  Wire.beginTransmission(OLED_ADDR);
  Wire.write((uint8_t)0x00);
  Wire.write((uint8_t)0x21); Wire.write(col); Wire.write((uint8_t)(col + width - 1));
  Wire.write((uint8_t)0x22); Wire.write(page); Wire.write(page);
  Wire.endTransmission();
}

void oledInit() {
  oledCmd(0xAE);              // display off
  oledCmd(0xD5); oledCmd(0x80);
  oledCmd(0xA8); oledCmd(0x1F); // multiplex 32
  oledCmd(0xD3); oledCmd(0x00);
  oledCmd(0x40);              // start line 0
  oledCmd(0x8D); oledCmd(0x14); // charge pump on
  oledCmd(0x20); oledCmd(0x00); // horizontal addressing
  oledCmd(0xA1);              // segment remap
  oledCmd(0xC8);              // COM scan reversed
  oledCmd(0xDA); oledCmd(0x02); // COM pins for 128x32
  oledCmd(0x81); oledCmd(0x7F);
  oledCmd(0xD9); oledCmd(0xF1);
  oledCmd(0xDB); oledCmd(0x40);
  oledCmd(0xA4); oledCmd(0xA6);
  oledCmd(0x2E);              // deactivate scroll
  oledCmd(0xAF);              // display on
}

void oledClear() {
  // A 16-byte data chunk avoids a framebuffer and large Wire transactions.
  for (uint8_t page = 0; page < 4; ++page) {
    for (uint8_t col = 0; col < OLED_WIDTH; col += 16) {
      oledWindow(col, page, 16);
      Wire.beginTransmission(OLED_ADDR);
      Wire.write((uint8_t)0x40);
      for (uint8_t i = 0; i < 16; ++i) Wire.write((uint8_t)0);
      Wire.endTransmission();
    }
  }
}

void oledText(uint8_t col, uint8_t page, const char *s) {
  if (page >= 4 || col > 122) return;
  while (*s && col <= 122) {
    uint8_t n = 0;
    while (n < CHUNK_CHARS && s[n] && (uint16_t)col + (n + 1) * 6 <= OLED_WIDTH) ++n;
    if (!n) break;
    oledWindow(col, page, n * 6);
    Wire.beginTransmission(OLED_ADDR);
    Wire.write((uint8_t)0x40);
    for (uint8_t k = 0; k < n; ++k) {
      char c = s[k];
      uint8_t idx = glyphIndex(c) * 5;
      for (uint8_t i = 0; i < 5; ++i) Wire.write(pgm_read_byte(&font5x7[idx + i]));
      Wire.write((uint8_t)0);
    }
    Wire.endTransmission();
    col += n * 6;
    s += n;
  }
}

bool inaWrite(uint8_t reg, uint16_t value) {
  Wire.beginTransmission(INA_ADDR);
  Wire.write(reg);
  Wire.write((uint8_t)(value >> 8));
  Wire.write((uint8_t)value);
  return Wire.endTransmission() == 0;
}

bool inaRead(uint8_t reg, uint16_t &value) {
  Wire.beginTransmission(INA_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)INA_ADDR, (uint8_t)2) != 2) return false;
  uint8_t hi = Wire.read();
  value = ((uint16_t)hi << 8) | Wire.read();
  return true;
}

bool inaInit() {
  // BRNG=32 V, PGA=+/-160 mV, both ADCs 12-bit, continuous mode.
  if (!inaWrite(0x00, 0x199F)) return false;
  uint16_t config;
  return inaRead(0x00, config) && config == 0x199F;
}

bool inaMeasure(uint16_t &voltage_mV, int16_t &current_mA, int32_t &power_mW) {
  uint16_t rawBus, rawShunt;
  if (!inaRead(0x02, rawBus) || !inaRead(0x01, rawShunt)) return false;
  // Continuous mode: the most recent conversions are valid after startup.
  // Avoid requiring CNVR=1 on every poll; it is not a new-data handshake.
  int32_t bus_mV = (int32_t)(rawBus >> 3) * 4;
  int32_t shunt_uV = (int32_t)(int16_t)rawShunt * 10;
  int32_t input_mV = bus_mV + shunt_uV / 1000; // IN+ voltage
  int32_t current = shunt_uV * 100L / 1727L;   // 17.27 milliohms
  if (input_mV < 0 || input_mV > 26000 || current < -9000 || current > 9000) return false;
  voltage_mV = (uint16_t)input_mV;
  current_mA = (int16_t)current;
  power_mW = (int32_t)((input_mV * current) / 1000);
  return true;
}

// Render a signed fixed-point value into an 11-character field, including unit.
// 'scale' is units per displayed whole unit (1000 for mV->V, mA->A, mW->W).
// 'places' controls displayed fractional digits (2 for volts/amps, 1 for watts).
void makeField(char out[12], int32_t value, uint16_t scale, uint8_t places, char unit) {
  for (uint8_t i = 0; i < 11; ++i) out[i] = ' ';
  out[11] = 0;
  out[10] = unit;
  bool neg = value < 0;
  uint32_t magnitude = neg ? (uint32_t)(-value) : (uint32_t)value;
  uint32_t factor = (places == 2) ? 100UL : (places == 1 ? 10UL : 1UL);
  uint32_t rounded = (magnitude * factor + scale / 2) / scale;
  char reversed[11];
  uint8_t len = 0;
  for (uint8_t j = 0; j < places; ++j) {
    reversed[len++] = (char)('0' + rounded % 10);
    rounded /= 10;
  }
  if (places) reversed[len++] = '.';
  do {
    reversed[len++] = (char)('0' + rounded % 10);
    rounded /= 10;
  } while (rounded && len < sizeof(reversed));
  if (neg && len < sizeof(reversed)) reversed[len++] = '-';
  // Right-align number in columns 0..8, leaving column 9 as a gap.
  uint8_t start = (len <= 9) ? (9 - len) : 0;
  for (uint8_t i = 0; i < len && i < 9; ++i) out[start + i] = reversed[len - i - 1];
}

// Character-diff cache, updated only after rendering; adjacent changes are streamed.
char previous[3][12];
void oledField(uint8_t row, const char field[12]) {
  uint8_t i = 0;
  while (i < VALUE_CHARS) {
    if (previous[row][i] == field[i]) { ++i; continue; }
    uint8_t start = i;
    while (i < VALUE_CHARS && previous[row][i] != field[i]) ++i;
    uint8_t length = i - start;
    char run[12];
    for (uint8_t k = 0; k < length; ++k) {
      run[k] = field[start + k];
      previous[row][start + k] = run[k];
    }
    run[length] = 0;
    oledText(VALUE_COL + start * 6, row + 1, run);
  }
}

void updateDisplay() {
  uint16_t v;
  int16_t i;
  int32_t p;
  if (!inaMeasure(v, i, p)) {
    oledText(0, 0, "--");
    return;
  }
  char field[12];
  makeField(field, v, 1000, 2, 'V'); oledField(0, field);
  makeField(field, i, 1000, 2, 'A'); oledField(1, field);
  makeField(field, p, 1000, 1, 'W'); oledField(2, field);
}

void setup() {
  Wire.begin();
  Wire.setClock(400000); // Verified with this OLED and INA219
  oledInit();
  oledClear();
  oledText(0, 0, "V: A: W:");
  if (!inaInit()) {
    oledText(0, 0, "--");
    while (true) delay(1000);
  }
  oledText(0, 1, "V:");
  oledText(0, 2, "A:");
  oledText(0, 3, "W:");
  updateDisplay();
}

void loop() {
  updateDisplay();
  delay(500);
}
