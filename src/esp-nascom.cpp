// Author: Peter Jensen
//
// NASCOM-2 Simulator running on an ESP32-S2
//

#include <Arduino.h>
#include <ESP32Lib.h>
#include "NascomFont.h"

//pin configuration
class Pins {
public:
  static const int red   = 14;
  static const int green = 19;
  static const int blue  = 27;
  static const int hsync = 32;
  static const int vsync = 33;
};

class NascomDisplay {
	VGA3Bit vga;
public:
  static const uint32_t width  = 48;
	static const uint32_t height = 16;
	static const uint32_t leftMargin = 1;
	static const uint32_t topMargin  = 1;
  void init() {
  	//initializing vga at the specified pins
		vga.init(vga.MODE400x300, Pins::red, Pins::green, Pins::blue, Pins::hsync, Pins::vsync);
		vga.setFont(NascomFont);
	}
	void drawCharAt(uint32_t x, uint32_t y, uint8_t ch) {
		vga.drawChar((x + leftMargin)*NascomFont.charWidth, (y + topMargin)*NascomFont.charHeight, ch);
	}
};

NascomDisplay nascomDisplay;

void setup() {
	Serial.begin(115200);
	nascomDisplay.init();
	int ci = 0;
	for (uint32_t cy = 0; cy < NascomDisplay::height; cy++) {
		for (uint32_t cx = 0; cx < NascomDisplay::width; cx++) {
			nascomDisplay.drawCharAt(cx, cy, (char)ci);
			ci = (ci + 1) % 256;
		}
	}
}

void loop() {
}
