// Author: Peter Jensen
//
// NASCOM-2 Simulator running on an ESP32S
//

#include <Arduino.h>
#include <LittleFS.h>
#include "ESP32Lib.h"
#include "devdrivers/keyboard.h"
#include "NascomFont.h"

// Pin configuration
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

// Nascom display
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

// Nascom keyboard map.  Used to provide simulated input from keyboard
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

// Nascom keyboard
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

bool nasFileLoad(const char *fileName) {
	uint16_t a;
	uint8_t  b0, b1, b2, b3, b4, b5, b6, b7;
	File     file = LittleFS.open(fileName, "r");
	uint32_t numBytes = 0;

	if (!file) {
		DEBUG_PRINTF("Cannot open: %s\n", fileName);
		return false;
	}

	DEBUG_PRINTF("Loading %s\n", fileName);

  const size_t bufSize = 100;
  char buffer[bufSize];
	while (file.available()) {
    size_t l = file.readBytesUntil('\n', buffer, bufSize-1);
		buffer[l] = 0;
		if (buffer[0] == '.') {
			break;
		}
		char *p = buffer;
		a  = strtoul(p, &p, 16);
		b0 = strtoul(p, &p, 16);
		b1 = strtoul(p, &p, 16);
		b2 = strtoul(p, &p, 16);
		b3 = strtoul(p, &p, 16);
		b4 = strtoul(p, &p, 16);
		b5 = strtoul(p, &p, 16);
		b6 = strtoul(p, &p, 16);
		b7 = strtoul(p, &p, 16);
		//DEBUG_PRINTF("%04x %02x %02x %02x %02x %02x %02x %02x %02x\n", a, b0, b1, b2, b3, b4, b5, b6, b7);
		numBytes += 8;
	}

	file.close();

	DEBUG_PRINTF("%d (%04x) bytes loaded\n", numBytes, numBytes);
	return true;
}


NascomDisplay   nascomDisplay;
NascomKeyboard  nascomKeyboard(nascomDisplay);

void setup() {
	Serial.begin(115200);
  DEBUG_PRINTF("Mount LittleFS\n");
  if (!LittleFS.begin()) {
    DEBUG_PRINTF("LittleFS mount failed\n");
    return;
  }
	File dir = LittleFS.open("/");
	while (File file = dir.openNextFile()) {
		DEBUG_PRINTF("Name: %s, Size: %d\n", file.name(), file.size());
	}	
	nascomDisplay.init();
	nascomKeyboard.init();
	nasFileLoad("/nassys3.nal");
}

void loop() {
	vTaskSuspend(NULL);
}
