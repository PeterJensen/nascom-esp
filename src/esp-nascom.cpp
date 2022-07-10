// Author: Peter Jensen
//
// NASCOM-2 Simulator running on an ESP32S
//

#include <Arduino.h>
#include <LittleFS.h>
#include "ESP32Lib.h"
#include "devdrivers/keyboard.h"
#include "NascomFont.h"
#include "simz80.h"

namespace z80 {
  // Z80 state
  WORD af[2];                  // accumulator and flags (2 banks)
  int af_sel;                  // bank select for af
  struct ddregs regs[2];       // bc,de,hl
  int regs_sel;                // bank select for ddregs
  WORD ir;                     // other Z80 registers
  WORD ix;
  WORD iy;
  WORD sp;
  WORD pc;
  WORD IFF;
  BYTE ram[MEMSIZE*1024+1];  // The +1 location is for the wraparound GetWord
};

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

// Nascom memory
class NascomMemory {
  uint8_t *mem;
public:
  NascomMemory(uint8_t *mem) : mem(mem) {}
  uint8_t *getMemPtr() {
    return mem;
  }
  bool nasFileLoad(const char *fileName) {
    uint16_t addr;
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
      addr  = strtoul(p, &p, 16);
      // DEBUG_PRINTF("%04x:", addr);
      for (uint8_t b = 0; b < 8; b++) {
        uint8_t byte = strtoul(p, &p, 16);
        mem[addr+b] = byte;
        if (addr + b == 0) {
          mem[0x10000] = mem[0]; // make getWord(0xffff) work correctly
        }
        // DEBUG_PRINTF(" %02x", byte);
      }
      // DEBUG_PRINTF("\n");
      numBytes += 8;
    }
    file.close();
    DEBUG_PRINTF("%d (%04x) bytes loaded\n", numBytes, numBytes);
    return true;
  }
};

// Nascom display
class NascomDisplay {
private:  
  static const uint32_t width      = 48;
  static const uint32_t height     = 16;
  static const uint32_t leftMargin = 1;
  static const uint32_t topMargin  = 1;
  VGA3Bit               vga;
  bool                  cacheInitialized = false;
  bool                  cacheUsed        = true;
  uint8_t               cache[width*height];
  uint32_t              cx = 0;
  uint32_t              cy = 0;
public:
  void init() {
    //vga.setFrameBufferCount(2);
    vga.init(vga.MODE400x300, Pins::red, Pins::green, Pins::blue, Pins::hsync, Pins::vsync);
    vga.setFont(NascomFont);
    vga.setTextColor(vga.RGB(255, 255, 255), vga.RGB(0, 0, 0));
  }
  void show() {
    //vga.show();
  }
  void setCacheUsed(bool used) {
    cacheUsed = used;
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
  void updateFromMemory(NascomMemory &memory) {
    //DEBUG_PRINTF("NascomDisplay::updateFromMemory:\n");
    uint8_t *ram = memory.getMemPtr();
    for (uint8_t *p0 = ram + 0x80A;
        p0 < ram + 0xC00;
        p0 += 64) {
      //DEBUG_PRINTF("%04x:", p0 - ram);
      for (uint8_t *p = p0;
          p < p0 + 48;
          ++p) {
        uint32_t index = p - ram - 0x800;
        uint32_t x     = index % 64 - 10;
        uint32_t y     = index / 64;
        y = (y + 1) % 16; // The last line is the first line
        if (cacheUsed && !cacheInitialized) {
          cache[x + y*width] = *p;
        }
        if (cacheUsed && cacheInitialized) {
          if (cache[x + y*width] != *p) {
            drawCharAt(x, y, *p);
            cache[x + y*width] = *p;
          }
        }
        else {
          drawCharAt(x, y, *p);
        }
        //DEBUG_PRINTF(" %02x", *p);
      }
      //DEBUG_PRINTF("\n");
    }
    cacheInitialized = cacheUsed;
    show();
  }
};

// Nascom keyboard map.  Used to provide simulated input from keyboard
class NascomKeyboardMap {
  static const uint32_t mapSize = 8;
  uint8_t map[mapSize];
  uint8_t mapSnapshot[mapSize];
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
  uint32_t mapIndex = 0;

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
  
  void rewind() {
    mapIndex = 0;
    memcpy(mapSnapshot, map, mapSize);
  }

  void step() {
    mapIndex += 1;
    if (mapIndex == mapSize)
      rewind();
  }

  uint8_t current() {
    return mapSnapshot[mapIndex];
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
          //self->display.putChar(asc);
          //self->display.drawCharAt(0, 0, asc);
        }
      }
      mapUpdated = self->map.setAsciiChar(asc, down);
    }
    if (mapUpdated) {
      //DEBUG_PRINTF("Nascom Keyboard Map\n");
      //self->map.dump();
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
  void mapRewind() {
    map.rewind();
  }
  void mapStep() {
    map.step();
  }
  uint8_t mapCurrent() {
    return map.current();
  }
};
NascomKeyboard *NascomKeyboard::self = nullptr;

// Nascom IO logic
class NascomIo {
  // Port0 Out/In bits
  // -----------------
  //   Bit  Out                        In
  //   7:   ??                         ??
  //   6:   ??                         Keyboard S6
  //   5:   ??                         Keyboard S3
  //   4:   Tape LED                   Keyboard S5
  //   3:   Single step                Keyboard S4
  //   2:   ??                         Keyboard S0
  //   1:   Keyboard index reset       Keyboard S2
  //   0:   Keyboard index increment   Keyboard S1
  #define P0_OUT_KEYBOARD_RESET     1 << 1
  #define P0_OUT_KEYBOARD_INCREMENT 1 << 0
  NascomKeyboard &keyboard;
  uint8_t        p0LastValue;
public:
  NascomIo(NascomKeyboard &keyboard) : keyboard(keyboard), p0LastValue(0) {}
  uint8_t in(uint32_t port) {
    //DEBUG_PRINTF("in(%d) called\n", port);
    switch (port) {
      case 0: {
        uint8_t val = keyboard.mapCurrent();
        return ~keyboard.mapCurrent();
      }
      default:
        return 0;
    }
  }
  void out(uint32_t port, uint8_t value) {
    //DEBUG_PRINTF("out(%d, %d) called \n", port, value);
    switch (port) {
      case 0: {
        uint8_t zero2One = ~p0LastValue & value;
        if ((value & P0_OUT_KEYBOARD_RESET) != 0) {
          keyboard.mapRewind();
        }
        else if ((zero2One & P0_OUT_KEYBOARD_INCREMENT) != 0) {
          //DEBUG_PRINTF("out(0): value: %02x, zero2One: %02x\n", value, zero2One);
          keyboard.mapStep();
        }
        p0LastValue = value;
        break;
      }
      default:
        break;
    }
  }
};

class NascomCpu {
  #define Z80_FREQUENCY              4000000
  #define UI_REFRESH_RATE            30
  #define ESTIMATED_CYCLES_PER_INSN  8
  #define INSN_PER_REFRESH           Z80_FREQUENCY/UI_REFRESH_RATE/ESTIMATED_CYCLES_PER_INSN

  NascomDisplay &display;
  NascomMemory  &memory;

  static NascomCpu *self;

  static int simAction() {
    static uint32_t count   = 0;
    static uint32_t start   = millis();
    static uint32_t delayMs = 25;
    count++;
    if (count == UI_REFRESH_RATE) {
      int32_t now = millis();
      DEBUG_PRINTF("simAction called 60 times: %.3f. delayMs: %d\n", ((float)(now - start))/1000, delayMs);
      if (now - start > 1000) {
        uint32_t adjust = (now - start - 1000)/UI_REFRESH_RATE;
        if (adjust > delayMs)
          delayMs = delayMs >> 1;
        else
          delayMs = delayMs - adjust;	
      }
      else {
        delayMs = delayMs + (1000 - (now - start))/UI_REFRESH_RATE;
      }
      start = now;
      count = 0;
    }
    self->display.updateFromMemory(self->memory);
    delay(delayMs);
    return 0;
  }

public:
  NascomCpu(NascomDisplay &display, NascomMemory &memory) : display(display), memory(memory) {
    self = this;
  }
  void run() {
    z80::simz80(0, INSN_PER_REFRESH, simAction);
  }
};
NascomCpu *NascomCpu::self = nullptr;

NascomDisplay   nascomDisplay;
NascomKeyboard  nascomKeyboard(nascomDisplay);
NascomMemory    nascomMemory(z80::ram);
NascomIo        nascomIo(nascomKeyboard);
NascomCpu       nascomCpu(nascomDisplay, nascomMemory);

namespace z80 {
  int in(uint32_t port) {
    return nascomIo.in(port);
  }
  void out(uint32_t port, uint8_t value) {
    nascomIo.out(port, value);
  }
}

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
    file.close();
  }
  dir.close();
  nascomDisplay.init();
  nascomKeyboard.init();
  nascomMemory.nasFileLoad("/nassys3.nal");
  nascomMemory.nasFileLoad("/basic.nal");
  nascomMemory.nasFileLoad("/skakur.nas");
  nascomMemory.nasFileLoad("/BLS-maanelander.nas");
  nascomCpu.run();
  DEBUG_PRINTF("pc = %04x, sp = %04x\n", z80::pc, z80::sp);
}

void loop() {
  vTaskSuspend(NULL);
}
