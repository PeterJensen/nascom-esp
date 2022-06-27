// Author: Peter Jensen
//
// NASCOM-2 Simulator running on an ESP32S
//

#include <Arduino.h>
#include "ESP32Lib.h"
#include "devdrivers/keyboard.h"
#include "NascomFont.h"

//pin configuration
class Pins {
public:
  static const gpio_num_t red      = GPIO_NUM_14;
  static const gpio_num_t green    = GPIO_NUM_19;
  static const gpio_num_t blue     = GPIO_NUM_27;
  static const gpio_num_t hsync    = GPIO_NUM_32;
  static const gpio_num_t vsync    = GPIO_NUM_33;
	static const gpio_num_t kbdClock = GPIO_NUM_25;
	static const gpio_num_t kbdData  = GPIO_NUM_26;
};

class NascomDisplay {
	VGA3Bit vga;
public:
  static const uint32_t width      = 48;
	static const uint32_t height     = 16;
	static const uint32_t leftMargin = 1;
	static const uint32_t topMargin  = 1;
	uint32_t cx = 0;
	uint32_t cy = 0;
  void init() {
		vga.init(vga.MODE400x300, Pins::red, Pins::green, Pins::blue, Pins::hsync, Pins::vsync);
		vga.setFont(NascomFont);
	}
	void drawCharAt(uint32_t x, uint32_t y, uint8_t ch) {
		vga.drawChar((x + leftMargin)*NascomFont.charWidth, (y + topMargin)*NascomFont.charHeight, ch);
	}
	void putChar(uint8_t ch) {
		drawCharAt(cx, cy, ch);
		cx += 1;
		if (cx >= width) {
			cx = 0;
			cy += 1;
			if (cy >= height) {
				cy = 0;
			}
		}
	}
};

class NascomKeyboard {
	fabgl::Keyboard keyboard;
	NascomDisplay  &display;
	static NascomKeyboard *self;
	static void handleVirtualKey(fabgl::VirtualKey *vk, bool down) {
		if (down) {
			int asc = self->keyboard.virtualKeyToASCII(*vk);
			if (asc != -1) {
				self->display.putChar(asc);
			}
		}
	}
public:
  NascomKeyboard(NascomDisplay &display) : display(display) {
    self = this;
	}
  void init() {
	  keyboard.begin(Pins::kbdClock, Pins::kbdData, true, false);
  	keyboard.onVirtualKey = handleVirtualKey;
	}
};
NascomKeyboard *NascomKeyboard::self = nullptr;

NascomDisplay   nascomDisplay;
NascomKeyboard  nascomKeyboard(nascomDisplay);

void setup() {
	Serial.begin(115200);
	nascomDisplay.init();
	nascomKeyboard.init();
}

void loop() {
	vTaskSuspend(NULL);
}
