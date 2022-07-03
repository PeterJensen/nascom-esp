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

class NascomKeyboardMap {
	static const uint32_t mapSize = 8;
	uint8_t map[mapSize];
	static constexpr char encoding[mapSize][9] = {
		/* 0 */  "_\t@__-\r\010",
		/* 1 */  "__TXF5BH",
		/* 2 */  "__YZD6NJ",
		/* 3 */  "__USE7MK",
		/* 4 */  "__IAW8,L",
		/* 5 */  "__OQ39.;",
		/* 6 */  "_[P120/:",
		/* 7 */  "_]R C4VG",
		};

	void set(uint8_t row, uint8_t col, bool down) {
		if (down)
			map[row] |= 1 << col;
		else
			map[row] &= ~(1 << col);
	}

public:
	#define NK_MAKE(row, col) ((row) << 3 | (col))
	#define NK_ROW(key)       (((key) & 0x38) >> 3)
	#define NK_COL(key)       ((key) & 0x07)
  #define NK_NONE           0
	#define NK_UP             NK_MAKE(1, 6)
	#define NK_DOWN           NK_MAKE(3, 6)
	#define NK_LEFT           NK_MAKE(2, 6)
	#define NK_RIGHT          NK_MAKE(4, 6)
	#define NK_SHIFT          NK_MAKE(0, 4)

	void reset() {
		memset(map, 0, sizeof(map));
	}

  uint8_t encodeAscii(uint8_t ascChar) {
    return NK_NONE;
	}

	bool setAsciiChar(uint8_t ascChar, bool down) {
		uint8_t ascCharUpper = toupper(ascChar);
		bool    found = false;
		for (uint32_t row = 0; row < mapSize && !found; row++) {
			for (uint32_t col = 0; col < 7 && !found; col++) {
				if (encoding[row][7-col] == ascCharUpper) {
					set(row, col, down);
					found = true;
				}
			}
		}
		if (found && isupper(ascChar)) {
			setKey(NK_SHIFT, down);
		}
		return found;
	}

	void setKey(uint8_t key, bool down) {
    uint8_t row = NK_ROW(key);
		uint8_t col = NK_COL(key);
		set(row, col, down);
	}
  
	void dump() {
		for (uint8_t row = 0; row < mapSize; row++) {
			uint8_t rowValue = map[row];
			char rowValueStr[9];
			for (uint8_t i = 0; i < 8; i++) {
				rowValueStr[i] = (rowValue & (0x80 >> i)) ? '1' : '0';
			}
			rowValueStr[8] = 0;
			DEBUG_PRINTF("%s\n", rowValueStr);
		}
	}
};
constexpr char NascomKeyboardMap::encoding[mapSize][9];

class NascomKeyboard {
	fabgl::Keyboard        keyboard;
	NascomDisplay         &display;
	NascomKeyboardMap      map;
	static NascomKeyboard *self;
	static void handleVirtualKey(fabgl::VirtualKey *vk, bool down) {
		bool mapUpdated = false;
    switch (*vk) {
			case fabgl::VK_UP:
			  self->map.setKey(NK_UP, down);
				mapUpdated = true;
				break;
			case fabgl::VK_DOWN:
			  self->map.setKey(NK_DOWN, down);
				mapUpdated = true;
				break;
			case fabgl::VK_LEFT:
			  self->map.setKey(NK_LEFT, down);
				mapUpdated = true;
				break;
			case fabgl::VK_RIGHT:
			  self->map.setKey(NK_RIGHT, down);
				mapUpdated = true;
				break;
			default:
			  break;
		}
		if (!mapUpdated) {
			int asc = self->keyboard.virtualKeyToASCII(*vk);
			if (down) {
				if (asc != -1) {
					self->display.putChar(asc);
				}
			}
  		mapUpdated = self->map.setAsciiChar(asc, down);
		}
		if (mapUpdated) {
			DEBUG_PRINTF("Nascom Keyboard Map\n");
			self->map.dump();
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
	fabgl::Keyboard &getKeyboard() {
		return keyboard;
	}
};
NascomKeyboard *NascomKeyboard::self = nullptr;

NascomDisplay   nascomDisplay;
NascomKeyboard  nascomKeyboard(nascomDisplay);

static uint8_t codes[0x85] = {0};
void dumpOrderedScanCodes() {
	for (uint8_t sci = 0; sci < 86; sci++) {
		if ((sci > 0) && (sci % 8 == 0))
			Serial.printf("\n");
		uint8_t sc = fabgl::USLayout.scancodeToVK[sci].scancode;
		Serial.printf("%02X ", sc);
		codes[sc] = sci;
	}
	Serial.printf("Used scan codes:\n");
	for (uint32_t i = 0; i < 0x85; i++) {
		if ((i > 0) && (i % 8 == 0))
			Serial.printf("\n");
		Serial.printf("%02X ", codes[i]);
	}
}

void setup() {
	Serial.begin(115200);
	nascomDisplay.init();
	nascomKeyboard.init();
//	dumpOrderedScanCodes();
}

void loop() {
#if 0	
  static uint32_t clen = 1;
	fabgl::Keyboard &keyboard = nascomKeyboard.getKeyboard();
  if (keyboard.scancodeAvailable()) {
    int scode = nascomKeyboard.getKeyboard().getNextScancode();
    Serial.printf("%02X ", scode);
    if (scode == 0xF0 || scode == 0xE0) ++clen;
    --clen;
    if (clen == 0) {
      clen = 1;
      Serial.printf("%s\n", keyboard.virtualKeyToString(keyboard.scancodeToVK(scode, false)));
    }
  }
#endif
	vTaskSuspend(NULL);
}
