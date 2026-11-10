#include <Arduino.h>
#include <Wire.h>
#include <avr/pgmspace.h>
// ATtiny1614 baseline: migrated from working ATtiny412 B2 centred-value display.
// 5 MHz CPU; TWI 400 kHz; alternate pins PA1 SDA (SOIC pin 11), PA2 SCL (pin 12).
// INA219 0x40, SSD1306 0x3C. No additional features or changed measurement logic.
#define OLED 0x3C
#define INA  0x40

// 0..9, blank, decimal point, minus, V, A, W: 16 glyphs, 80 bytes.
const uint8_t font[] PROGMEM = {
  0x3E,0x51,0x49,0x45,0x3E, 0,0x42,0x7F,0x40,0,
  0x42,0x61,0x51,0x49,0x46, 0x21,0x41,0x45,0x4B,0x31,
  0x18,0x14,0x12,0x7F,0x10, 0x27,0x45,0x45,0x45,0x39,
  0x3C,0x4A,0x49,0x49,0x30, 1,0x71,9,5,3,
  0x36,0x49,0x49,0x49,0x36, 6,0x49,0x49,0x29,0x1E,
  0,0,0,0,0, 0,0x60,0x60,0,0, 8,8,8,8,8,
  0x1F,0x20,0x40,0x20,0x1F, 0x7E,0x11,0x11,0x11,0x7E,
  0x3F,0x40,0x38,0x40,0x3F
};
static_assert(sizeof(font)==80,"Font size");
uint8_t glyph(char c) {
  if(c>='0' && c<='9') return c-'0';
  switch(c) {case '.':return 11;case '-':return 12;case 'V':return 13;case 'A':return 14;case 'W':return 15;default:return 10;}
}
// Proven OLED diagnostic: issue each command in its own I2C transaction.
void oledCommand(uint8_t c) {
  Wire.beginTransmission(OLED);
  Wire.write((uint8_t)0x00);
  Wire.write(c);
  Wire.endTransmission();
}
void window(uint8_t x,uint8_t p,uint8_t w) {
  oledCommand(0x21); oledCommand(x); oledCommand((uint8_t)(x+w-1));
  oledCommand(0x22); oledCommand(p); oledCommand(p);
}
void beginData(uint8_t x,uint8_t p,uint8_t w) {
  window(x,p,w);Wire.beginTransmission(OLED);Wire.write((uint8_t)0x40);
}
void oledCmd(uint8_t c) {
  Wire.beginTransmission(OLED);
  Wire.write((uint8_t)0x00);
  Wire.write(c);
  Wire.endTransmission();
}
void initOLED() {
  oledCmd(0xAE);        // Display OFF
  oledCmd(0xD5); oledCmd(0x80);
  oledCmd(0xA8); oledCmd(0x1F); // 32 rows
  oledCmd(0xD3); oledCmd(0x00);
  oledCmd(0x40);        // Start line 0
  oledCmd(0x8D); oledCmd(0x14); // Charge pump ON
  oledCmd(0x20); oledCmd(0x00); // Horizontal addressing
  oledCmd(0xA1);        // Segment remap
  oledCmd(0xC8);        // COM scan direction
  oledCmd(0xDA); oledCmd(0x02); // 128×32
  oledCmd(0x81); oledCmd(0x7F);
  oledCmd(0xD9); oledCmd(0xF1);
  oledCmd(0xDB); oledCmd(0x40);
  oledCmd(0xA4);        // Display follows RAM
  oledCmd(0xA6);        // Normal polarity
  oledCmd(0x2E);        // Disable scrolling
  oledCmd(0xAF);        // Display ON
}
void clearOLED() {
  for(uint8_t p=0;p<4;p++) for(uint8_t x=0;x<128;x+=16) {
    beginData(x,p,16);for(uint8_t j=0;j<16;j++)Wire.write((uint8_t)0);Wire.endTransmission();
  }
}
void smallChar(uint8_t x,uint8_t page,char c) {
  uint8_t off=glyph(c)*5;
  beginData(x,page,6);
  for(uint8_t i=0;i<5;i++) Wire.write(pgm_read_byte(&font[off+i]));
  Wire.write((uint8_t)0);Wire.endTransmission();
}
// Double-height 5x7 digits, 6 pixels wide, drawn into pages 0 and 1.
const uint8_t expand4[16] PROGMEM = {
  0x00,0x03,0x0C,0x0F,0x30,0x33,0x3C,0x3F,
  0xC0,0xC3,0xCC,0xCF,0xF0,0xF3,0xFC,0xFF
};
void bigChar(uint8_t x,char c) {
  uint8_t off=glyph(c)*5;
  for(uint8_t p=0;p<2;p++) {
    beginData(x,p,6);
    for(uint8_t i=0;i<5;i++) {
      uint8_t bits=pgm_read_byte(&font[off+i]);
      Wire.write(pgm_read_byte(&expand4[(bits>>(p*4))&15]));
    }
    Wire.write((uint8_t)0);Wire.endTransmission();
  }
}
// Each 30-pixel field is visually centred in its own column.
// Units at x=12, 50, 88 share those same column centres.
// When a value grows/shrinks, erase its previous glyph positions first.
char old[3][5];
uint8_t oldStart[3] = {255,255,255};
void field(uint8_t f,uint8_t x,const char *s) {
  uint8_t skip=0;
  while(skip<4 && s[skip]==' ')skip++;
  uint8_t start=(uint8_t)((skip*6)/2); // centre visible glyphs in 30 px
  if(start!=oldStart[f]) {
    if(oldStart[f]!=255) {
      for(uint8_t i=0;i<5;i++)
        if(old[f][i]!=' ' && old[f][i]!=0)
          bigChar(x+oldStart[f]+6*i,' ');
    }
    for(uint8_t i=0;i<5;i++)old[f][i]=0;
    oldStart[f]=start;
  }
  for(uint8_t i=0;i<5-skip;i++) {
    char c=s[i+skip];
    if(old[f][i]!=c) {
      bigChar(x+start+6*i,c);
      old[f][i]=c;
    }
  }
}
bool readINA(uint8_t reg,uint16_t &v) {
  Wire.beginTransmission(INA);Wire.write(reg);
  if(Wire.endTransmission(false))return false;
  if(Wire.requestFrom((uint8_t)INA,(uint8_t)2)!=2)return false;
  v=(uint16_t)Wire.read()<<8;v|=Wire.read();return true;
}
bool initINA() {
  Wire.beginTransmission(INA);Wire.write((uint8_t)0);Wire.write((uint8_t)0x19);
  Wire.write((uint8_t)0x9F);if(Wire.endTransmission())return false;
  uint16_t v;return readINA(0,v)&&v==0x199F;
}
// Fixed width ' 12.0' or '120.0'. Input is tenths of the unit.
void format5(char s[5],uint16_t t) {
  if(t>9999){for(uint8_t i=0;i<5;i++)s[i]='-';return;}
  s[0]=t>=1000 ? '0'+t/1000 : ' ';
  s[1]=t>=100 ? '0'+(t/100)%10 : ' ';
  s[2]='0'+(t/10)%10;
  s[3]='.';s[4]='0'+t%10;
}
uint8_t oldBars=255;
void battery(uint16_t mv) {
  // Voltage bands under load; NOT a state-of-charge estimate.
  uint8_t bars=mv>=20500?4:mv>=19500?3:mv>=18500?2:mv>=17500?1:0;
  if(bars==oldBars)return;
  oldBars=bars;
  for(uint8_t p=0;p<4;p++) {
    beginData(116,p,8);
    for(uint8_t x=0;x<8;x++) {
      uint8_t b=(x==0||x==7)?0xFF:((p==0||p==3)?0x81:0);
      if(x>=2&&x<=5&&p>=4-bars)b=0xFF;
      Wire.write(b);
    }
    Wire.endTransmission();
  }
}
void update() {
  uint16_t b,s;
  char out[5];
  if(!readINA(2,b)||!readINA(1,s)) {
    for(uint8_t j=0;j<5;j++)out[j]='-';
    field(0,0,out);field(1,38,out);field(2,76,out);return;
  }
  int32_t uv=(int32_t)(int16_t)s*10;
  int32_t mv=(int32_t)(b>>3)*4+uv/1000; // IN+ supply voltage
  int32_t ma=uv*100L/1727L; // 17.27 milliohm effective shunt
  if(mv<0||mv>26000||ma<0||ma>9000) {
    for(uint8_t j=0;j<5;j++)out[j]='-';
    field(0,0,out);field(1,38,out);field(2,76,out);return;
  }
  int32_t mw=mv*ma/1000L;
  format5(out,(uint16_t)((mv+50)/100));field(0,0,out);
  format5(out,(uint16_t)((ma+50)/100));field(1,38,out);
  format5(out,(uint16_t)((mw+50)/100));field(2,76,out);
  battery((uint16_t)mv);
}
void setup() {
  delay(500);
  Wire.begin();Wire.setClock(400000);
  initOLED();clearOLED();
  smallChar(12,3,'V');smallChar(50,3,'A');smallChar(88,3,'W');
  if(!initINA())return; // display remains static if INA is absent
  update();
}
void loop(){update();delay(500);}
