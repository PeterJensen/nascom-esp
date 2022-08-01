// Author: Peter Jensen
//
// NASCOM-2 Simulator running on an ESP32S
//
// The simulator uses a VGA display and a PS/2 keyboard.
// The cassette tape is simulated with both the internal flash and an attached SD card reader.
//
// PlatformIO/VS Code development environment was used for development.
//
// The project uses software from various sources:
//
// 1. Tommy Thorn's virtual-nascom: https://github.com/tommythorn/virtual-nascom:
//    Nascom-2 keyboard simulation.  Original code is mostly rewritten, but the ideas are the same.
//    Nascom-2 font definition.
// 2. bitluni's ESP32Lib: https://github.com/bitluni/ESP32Lib:
//    VGA driver logic.  Made it work with the Nascom-2 font defintion from 1)
// 3. Fabrizio Di Vittorio's FabGL: https://github.com/fdivitto/FabGL:
//    PS/2 keyboard handling.  This fabulous library does much more than just handling a PS/2
//    keyboard.  It's also able to handle the VGA and Z80 CPU simulation, but those
//    parts aren't used.  I'm considering using the Z80 simulator, since it tracks clock cycles,
//    so it should be possible to get a more accurate simulation of time.
// 4. Z80 emulator from Frank D. Cringle:
//    There are many Z80 emulator's to choose from.  This is the one used by virtual-nascom, and
//    I use it here as well.  It's been proven to work for NASCOM-2 simulation.
//
// Notes:
//
// 1. The display is set to be refreshed at 30fps. The number of instructions that will execute between
//    each frame is based on roughly 8 cycles per instructions and a clock frequency of 4MHz.
//    The necessary delay for each frame in order to get accurate simulation speed is dynamically
//    determined and turns out to be roughly 25ms.  So for each 33ms (a frame) the ESP32 is idling for
//    25ms. The ESP32 is more than capable of simulating a 4MHz Z80.
//

#include <Arduino.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
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

static const char *startText =
  "Nascom-2 Emulation on ESP32S - V1.0\x17\x14"
  "        by Peter Jensen\x17\x14";

// Pin configuration
class Pins {
public:
  static const gpio_num_t red      = GPIO_NUM_14;
  static const gpio_num_t green    = GPIO_NUM_12;
  static const gpio_num_t blue     = GPIO_NUM_27;
  static const gpio_num_t hsync    = GPIO_NUM_32;
  static const gpio_num_t vsync    = GPIO_NUM_33;
  static const gpio_num_t kbdClock = GPIO_NUM_25;
  static const gpio_num_t kbdData  = GPIO_NUM_26;
  static const gpio_num_t tapeLed  = GPIO_NUM_16;
  static const gpio_num_t sdSck    = GPIO_NUM_18;
  static const gpio_num_t sdMiso   = GPIO_NUM_19;
  static const gpio_num_t sdMosi   = GPIO_NUM_23;
  static const gpio_num_t sdCs     = GPIO_NUM_5;
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
  VGA3Bit::Color        white;
  VGA3Bit::Color        black;
  VGA3Bit::Color        red;
  VGA3Bit::Color        green;
  VGA3Bit::Color        blue;

  void init() {
    //vga.setFrameBufferCount(2);
    vga.init(vga.MODE400x300, Pins::red, Pins::green, Pins::blue, Pins::hsync, Pins::vsync);
    white = vga.RGB(255, 255, 255);
    black = vga.RGB(0, 0, 0);
    red   = vga.RGB(255, 0, 0);
    green = vga.RGB(0, 255, 0);
    blue  = vga.RGB(0, 0, 255);
    vga.setFont(NascomFont);
    vga.setTextColor(white, black);
  }

  void setTextColor(VGA3Bit::Color fg, VGA3Bit::Color bg) {
    vga.setTextColor(fg, bg);
  }

  void clear() {
    for (uint32_t y = 0; y < height; y++) {
      for (uint32_t x = 0; x < width; x++) {
        drawCharAt(x, y, ' ');
      }
    }
  }

  void show() {
    //vga.show();
  }
  void setCacheUsed(bool used) {
    cacheUsed = used;
  }
  void clearCache() {
    cacheInitialized = false;
  }
  void drawCharAt(uint32_t x, uint32_t y, uint8_t ch) {
    if (x < width && y < height) {
      vga.drawChar((x + leftMargin)*NascomFont.charWidth, (y + topMargin)*NascomFont.charHeight, ch);
    }
  }
  void drawTextAt(uint32_t x, uint32_t y, const char *text) {
    while (*text != 0) {
      drawCharAt(x, y, *text);
      x++;
      text++;
    }
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

class NascomTape {
  bool                  tapeLed        = false;
  static const uint32_t maxFileNameLen = 32;
  char                  inFileName[maxFileNameLen];
  char                  outFileName[maxFileNameLen];
  File  inFile;
  File  outFile;
  FS   *inFs;
  FS   *outFs;
public:
//  NascomTape() : tapeLed(false), inFileIsOpen(false), outFileIsOpen(false) {}
  void init() {
    pinMode(Pins::tapeLed, OUTPUT);
    inFileName[0] = 0;
    outFileName[0] = 0;
  }
  void setLed(bool isOn) {
    tapeLed = isOn;
    digitalWrite(Pins::tapeLed, isOn ? HIGH : LOW);
  }
  bool getLed() {
    return tapeLed;
  }
  void setOutputFile(FS *fs, const char *fileName) {
    outFs = fs;
    if (fileName[0] != '/') {
      outFileName[0] = '/';
      strncpy(&(outFileName[1]), fileName, maxFileNameLen-1);
    }
    else
      strncpy(outFileName, fileName, maxFileNameLen);
    outFileName[maxFileNameLen-1] = 0;
  }
  void setInputFile(FS *fs, const char *fileName) {
    inFs = fs;
    if (fileName[0] != '/') {
      inFileName[0] = '/';
      strncpy(&(inFileName[1]), fileName, maxFileNameLen-1);
    }
    else
      strncpy(inFileName, fileName, maxFileNameLen);
    inFileName[maxFileNameLen-1] = 0;
    DEBUG_PRINTF("setInputFile: %s\n", inFileName);
  }
  void openFiles() {
    if (inFs == &SD || outFs == &SD)
      SD.begin(Pins::sdCs);
    if (inFileName[0] != 0) {
      inFile = inFs->open(inFileName, "r");
    }
    if (outFileName[0] != 0) {
      outFile = outFs->open(outFileName, "w");
    }
  }
  void closeFiles() {
    if (inFile) {
      inFile.close();
    }
    if (outFile) {
      outFile.close();
    }
    if (inFs == &SD || outFs == &SD)
      SD.end();
  }
  bool hasData() {
    return inFile && inFile.available();
  }
  uint8_t readByte() {
    if (!inFile) {
      return 0;
    }
    if (!inFile.available()) {
      inFile.seek(0);
    }
    return inFile.read();
  }
  void writeByte(uint8_t b) {
    if (outFile) {
      outFile.write(b);
    }
  }
};

// Nascom Control
// UI for picking tape i/o files

class NascomControl {
  NascomDisplay        &display;
  NascomTape           &tape;
  static NascomControl *self;
  bool                  isActive = false;

  class FieldValues {
  protected:
    const char **values = nullptr;
    uint32_t     numValues = 0;
    uint32_t     current = 0;
    bool         refreshed = false;
  public:
    virtual void refresh() = 0;

    const char *getCurrent() {
      if (numValues == 0)
        return "";
      else
        return values[current];
    }
    const char *getNext() {
      current += 1;
      if (current == numValues) {
        current = 0;
      }
      return getCurrent();
    }
    const char *getPrev() {
      if (current == 0) {
        current = numValues;
      }
      current -= 1;
      return getCurrent();
    }
    void reset() {
      current = 0;
    }
    bool isRefreshed() {
      return refreshed;
    }
  };

  class TapeFsValues : public FieldValues {
  public:
    void refresh() {
      DEBUG_PRINTF("TapeFsValues::refresh\n");
      if (values != nullptr) {
        free(values);
      }
      values = (const char **)malloc(2*sizeof(void *));
      bool hasInternal = true; //LittleFS.begin();
      bool hasSd       = SD.begin(Pins::sdCs);
      if (hasSd) {
        sdcard_type_t cardType = SD.cardType();
        if (cardType == CARD_NONE || cardType == CARD_UNKNOWN) {
          hasSd = false;
        }
      }
      SD.end();

      numValues = 0;
      if (hasInternal) {
        values[numValues] = "Internal Flash";
        numValues += 1;
      }
      if (hasSd) {
        values[numValues] = "SD Card";
        numValues += 1;
      }
      this->reset();
      refreshed = true;
    }
  };
  class FileNames {
  public:
    static uint32_t fileCount(File dir) {
      uint32_t count = 0;
      dir.rewindDirectory();
      while (File file = dir.openNextFile()) {
        count += 1;
        file.close();
      }
      return count;
    }
    static const char **makeFileNames(File dir, uint32_t *count) {
      *count = fileCount(dir);
      char const **names = (const char **)malloc((*count)*sizeof(char *));
      uint32_t fileNum = 0;
      dir.rewindDirectory();
      while (File file = dir.openNextFile()) {
        const char *name = file.name();
        char *nameValue = (char *)malloc(strlen(name)+1);
        strcpy(nameValue, name);
        names[fileNum] = nameValue;
        fileNum += 1;
        file.close();
      }
      return names;
    }
    static void freeFileNames(const char **names, uint32_t count) {
      for (uint32_t vi = 0; vi < count; vi++)
        free((void *)names[vi]);
      free(names);
    }

  };
  class TapeFileNamesSd : public FieldValues, public FileNames {
  public:
    void refresh() {
      DEBUG_PRINTF("TapeFileNamesSd::refresh\n");
      if (values != nullptr) {
        freeFileNames(values, numValues);
      }
      bool hasSd = SD.begin(Pins::sdCs);
      if (!hasSd) {
        values = nullptr;
        numValues = 0;
        return;
      }
      File dir = SD.open("/");
      values = makeFileNames(dir, &numValues);
      dir.close();
      SD.end();
      this->reset();
      refreshed = true;
    }
  };

  class TapeFileNamesInt : public FieldValues, public FileNames {
  public:
    void refresh() {
      DEBUG_PRINTF("TapeFileNamesInt::refresh\n");
      if (values != nullptr) {
        freeFileNames(values, numValues);
      }
      File dir = LittleFS.open("/");
      values = makeFileNames(dir, &numValues);
      dir.close();
      this->reset();
      refreshed = true;
    }
  };

  struct Field {
    uint32_t        x;
    uint32_t        y;
    uint32_t        length;
    FieldValues    *values = nullptr;
    const char     *text;
    VGA3Bit::Color  background;
  };
  enum FieldNames {
    noField         = 0,
    tapeInFs        = 1,
    tapeInFileName  = 2,
    tapeOutFs       = 3,
    tapeOutFileName = 4,
    numFields       = 5 // pseudo field name
  };
  static const FieldNames firstField = tapeInFs;
  static const FieldNames lastField  = tapeOutFileName;

  enum FieldMove {
    current,
    next,
    prev
  };

  Field            fields[numFields];
  FieldNames       activeField = noField;
  TapeFsValues     tapeFsValues;
  TapeFileNamesSd  tapeFileNamesSd;
  TapeFileNamesInt tapeFileNamesInt;

  void addField(Field &field, uint32_t x, uint32_t y, uint32_t length, FieldValues *values = nullptr) {
    field.x = x;
    field.y = y;
    field.length = length;
    field.background = display.green;
    field.values = values;
    if (values != nullptr)
      setFieldText(field, values->getCurrent());
    else
      setFieldText(field, "");
  }
  void setFieldText(Field &field, const char *text) {
    field.text = text;
    refreshField(field);
  }
  void refreshField(Field &field) {
    bool filling = false;
    display.setTextColor(display.white, field.background);
    for (uint32_t xi = 0; xi < field.length; xi++) {
      if (!filling && field.text[xi] == 0)
        filling = true;
      if (filling)
        display.drawCharAt(field.x + xi, field.y, ' ');
      else
        display.drawCharAt(field.x + xi, field.y, field.text[xi]);
    }
  }
  void setActiveField(FieldNames fieldName) {
    if (fieldName == noField) {
      activeField = fieldName;
    }
    else if (activeField != fieldName) {
      fields[activeField].background = display.green;
      refreshField(fields[activeField]);
      activeField = fieldName;
      fields[activeField].background = display.red;
      refreshField(fields[activeField]);
    }
  }
  void gotoNextField() {
    FieldNames nextField;
    if (activeField == lastField)
      nextField = firstField;
    else
      nextField = static_cast<FieldNames>(static_cast<uint32_t>(activeField) + 1);
    setActiveField(nextField);
  }
  void updateFieldValues(FieldNames fieldName, FieldValues *values) {
    DEBUG_PRINTF("updateFieldValues: %d\n", fieldName);
    fields[fieldName].values = values;
    updateFieldValue(fieldName, current);
  }
  void updateFieldValue(FieldNames fieldName, FieldMove move) {
    DEBUG_PRINTF("updateFieldValue: %d\n", fieldName);
    FieldValues *values = fields[fieldName].values;
    const char  *newValue = nullptr; 
    if (values != nullptr) {
      if (move == current)
        newValue = values->getCurrent();
      else if (move == next)
        newValue = values->getNext();
      else // if (move == prev)
        newValue = values->getPrev();
      setFieldText(fields[fieldName], newValue);
      if (fieldName == tapeInFs) {
        if (newValue[0] == 'I') //
          updateFieldValues(tapeInFileName, &tapeFileNamesInt);
        else 
          updateFieldValues(tapeInFileName, &tapeFileNamesSd);
      }
    }
    else
      setFieldText(fields[activeField], "");
  }
  const char *getFieldText(FieldNames fieldName) {
    return fields[fieldName].text;
  }

public:
  NascomControl (NascomDisplay &display, NascomTape &tape) : display(display), tape(tape) {
    self = this;
  }

  static void handleVirtualKey(fabgl::VirtualKey *vk, bool down) {
    DEBUG_PRINTF("UI: handleVirtualKey\n");
    if (down && (*vk == fabgl::VK_TAB))
      self->gotoNextField();
    else if (down && (*vk == fabgl::VK_UP))
      self->updateFieldValue(self->activeField, prev);
    else if (down && (*vk == fabgl::VK_DOWN))
      self->updateFieldValue(self->activeField, next);
  }

  void activate() {
    DEBUG_PRINTF("UI: Activate\n");
    display.setTextColor(display.white, display.blue);
    isActive = true;
  }

  void showScreen() {
    display.clear();
    setActiveField(noField);
    display.drawTextAt(16, 1, "Nascom-2 Control");
    display.drawTextAt(10, 3, "File System");
    display.drawTextAt(25, 3, "File Name");
    display.drawTextAt(1, 4, "Tape In");
    display.drawTextAt(1, 6, "Tape Out");
    tapeFsValues.refresh();
    addField(fields[tapeInFs], 10, 4, 14, &tapeFsValues);
    tapeFileNamesInt.refresh();
    tapeFileNamesSd.refresh();
    addField(fields[tapeInFileName], 25, 4, 22, &tapeFileNamesInt);
    addField(fields[tapeOutFs], 10, 6, 14, &tapeFsValues);
    addField(fields[tapeOutFileName], 25, 6, 22, &tapeFileNamesSd);
    setActiveField(firstField);
  }

  void deactivate() {
    DEBUG_PRINTF("UI: Deactivate\n");
    isActive = false;
    display.clearCache();
    display.setTextColor(display.white, display.black);
    const char *fs   = getFieldText(tapeInFs);
    const char *name = getFieldText(tapeInFileName);
    DEBUG_PRINTF("fs: %s\n", fs);
    DEBUG_PRINTF("name: %s\n", name);
    if (fs[0] == 'I')
      tape.setInputFile(&LittleFS, name);
    else
      tape.setInputFile(&SD, name);
  }

  bool getIsActive() {
    return isActive;
  }
};
NascomControl *NascomControl::self = nullptr;

// Nascom keyboard map.  Used to provide simulated input from keyboard
class NascomKeyboardMap {
  // Encoding: csrrrccc
  // c   = CTRL
  // s   = SHIFT
  // rrr = row
  // ccc = col
  #define NK_MAKE(row, col) ((row) << 3 | (col))
  #define NK_ROW(key)       (((key) & 0x38) >> 3)
  #define NK_COL(key)       ((key) & 0x07)
  #define NK_SHIFT_MASK     0x40
  #define NK_CTRL_MASK      0x80
  #define NK_BASE(key)      ((key) & 0x3f)
  #define NK_HAS_SHIFT(key) (((key) & NK_SHIFT_MASK) != 0)
  #define NK_HAS_CTRL(key)  (((key) & NK_CTRL_MASK) != 0)

  #define NK_NONE           NK_MAKE(7, 7)
  #define NK_UP             NK_MAKE(1, 6)
  #define NK_DOWN           NK_MAKE(3, 6)
  #define NK_LEFT           NK_MAKE(2, 6)
  #define NK_RIGHT          NK_MAKE(4, 6)
  #define NK_SHIFT          NK_MAKE(0, 4)
  #define NK_CTRL           NK_MAKE(0, 3)
  #define NK_GRAPH          NK_MAKE(5, 6)
  #define NK_SPACE          NK_MAKE(7, 4)
  #define NK_0              NK_MAKE(6, 2)
  #define NK_2              NK_MAKE(6, 3)
  #define NK_A              NK_MAKE(4, 4)
  #define NK_B              NK_MAKE(1, 1)
  #define NK_C              NK_MAKE(7, 3)
  #define NK_D              NK_MAKE(2, 3)
  #define NK_E              NK_MAKE(3, 3)
  #define NK_F              NK_MAKE(1, 3)
  #define NK_G              NK_MAKE(7, 0)
  #define NK_H              NK_MAKE(1, 0)
  #define NK_I              NK_MAKE(4, 5)
  #define NK_J              NK_MAKE(2, 0)
  #define NK_K              NK_MAKE(3, 0)
  #define NK_L              NK_MAKE(4, 0)
  #define NK_M              NK_MAKE(3, 1)
  #define NK_N              NK_MAKE(2, 1)
  #define NK_O              NK_MAKE(5, 5)
  #define NK_P              NK_MAKE(6, 5)
  #define NK_Q              NK_MAKE(5, 4)
  #define NK_R              NK_MAKE(7, 5)
  #define NK_S              NK_MAKE(3, 4)
  #define NK_T              NK_MAKE(1, 5)
  #define NK_U              NK_MAKE(3, 5)
  #define NK_V              NK_MAKE(7, 1)
  #define NK_W              NK_MAKE(4, 3)
  #define NK_X              NK_MAKE(1, 4)
  #define NK_Y              NK_MAKE(2, 5)
  #define NK_Z              NK_MAKE(2, 4)
  #define NK_LEFTBRACKET    NK_MAKE(6, 6)
  #define NK_RIGHTBRACKET   NK_MAKE(7, 6)
  
  static const uint32_t mapSize = 8;
  uint8_t               map[mapSize];
  uint8_t               mapSnapshot[mapSize];
  bool                  mapIsUpdating = false;
  TaskHandle_t          mainTaskId;

  static constexpr char encoding[mapSize][9] = {
    /*        _   6    5   4   3   2   1    0 */
    /* 0 */  "_" "\t" "@" "_" "_" "-" "\r" "\010",
    /* 1 */  "_" "_"  "T" "X" "F" "5" "B"  "H",
    /* 2 */  "_" "_"  "Y" "Z" "D" "6" "N"  "J",
    /* 3 */  "_" "_"  "U" "S" "E" "7" "M"  "K",
    /* 4 */  "_" "_"  "I" "A" "W" "8" ","  "L",
    /* 5 */  "_" "_"  "O" "Q" "3" "9" "."  ";",
    /* 6 */  "_" "["  "P" "1" "2" "0" "/"  ":",
    /* 7 */  "_" "]"  "R" " " "C" "4" "V"  "G",
    };
  uint32_t mapIndex = 0;

  void set(uint8_t row, uint8_t col, bool down) {
    if (down)
      map[row] |= 1 << col;
    else
      map[row] &= ~(1 << col);
  }

  uint8_t getNk(uint8_t ascChar) {
    if (ascChar == '_') {
      // '_' is used as an 'unknown'
      return NK_NONE;
    }
    for (uint32_t row = 0; row < mapSize; row++) {
      for (uint32_t col = 0; col < 7; col++) {
        if (encoding[row][7-col] == ascChar) {
          return NK_MAKE(row, col);
        }
      }
    }
    return NK_NONE;
  }

  #define ___ " " // Dummy
  static constexpr const char *keys          = ";" ":" "["    "]" "-" "," "." "/"    "0" "1" "2"  "3" "4" "5" "6" "7" "8" "9" " ";
  static constexpr const char *keysShift     = "+" "*" "\\"   "_" "=" "<" ">" "?"    "^" "!" "\"" "#" "$" "%" "&" "'" "(" ")" ___;
  static constexpr const char *keysCtrl      = "{" ___ "\033" ___ ___ ___ ___ ___    ___ ___ ___  ___ ___ ___ ___ ___ ___ ___ "`";
  static constexpr const char *keysShiftCtrl = ___ ___ ___    ___ "}" "|" "~" "\177" ___ ___ ___  ___ ___ ___ ___ ___ ___ ___ ___;

  static constexpr const uint8_t ascNkMap[32] = {
    NK_SPACE|NK_CTRL_MASK, NK_A|NK_CTRL_MASK, NK_B|NK_CTRL_MASK, NK_C|NK_CTRL_MASK,           //  0 -  3
    NK_D|NK_CTRL_MASK,     NK_E|NK_CTRL_MASK, NK_F|NK_CTRL_MASK, NK_G|NK_CTRL_MASK,           //  4 -  7
    NK_H|NK_CTRL_MASK,     NK_I|NK_CTRL_MASK, NK_J|NK_CTRL_MASK, NK_K|NK_CTRL_MASK,           //  8 - 11
    NK_L|NK_CTRL_MASK,     NK_M|NK_CTRL_MASK, NK_N|NK_CTRL_MASK, NK_O|NK_CTRL_MASK,           // 12 - 15
    NK_P|NK_CTRL_MASK,     NK_Q|NK_CTRL_MASK, NK_R|NK_CTRL_MASK, NK_S|NK_CTRL_MASK,           // 16 - 19
    NK_T|NK_CTRL_MASK,     NK_U|NK_CTRL_MASK, NK_V|NK_CTRL_MASK, NK_W|NK_CTRL_MASK,           // 20 - 23
    NK_X|NK_CTRL_MASK,     NK_Y|NK_CTRL_MASK, NK_Z|NK_CTRL_MASK, NK_LEFTBRACKET|NK_CTRL_MASK, // 24 - 27
    NK_LEFTBRACKET|NK_SHIFT_MASK|NK_CTRL_MASK, NK_RIGHTBRACKET|NK_CTRL_MASK, NK_0|NK_SHIFT_MASK|NK_CTRL_MASK, NK_RIGHTBRACKET|NK_SHIFT_MASK|NK_CTRL_MASK // 28 - 31
  };

  uint8_t getNascomKey(uint8_t ascChar) {
    // Handle upper/lower case letters.
    // Lowercase should be mapped to uppercase and vice versa
    if (isupper(ascChar) || ascChar == '@') {
      // for some reason '@' requires SHIFT
      return getNk(ascChar) | NK_SHIFT_MASK;
    }
    else if (islower(ascChar)) {
      return getNk(toupper(ascChar));
    }

    // Check if ascChar is in the keyboard map
    uint8_t nk = getNk(ascChar);
    if (nk != NK_NONE) {
      return nk;
    }
    // Check if ascChar is in the set of chars that must use SHIFT and be remapped
    for (uint32_t i = 0; keysShift[i] != 0; i++) {
      if (keysShift[i] == ascChar) {
        return getNk(keys[i]) | NK_SHIFT_MASK;
      }
    }
    // Check if ascChar is in the set of chars that must use CTRL and be remapped
    for (uint32_t i = 0; keysCtrl[i] != 0; i++) {
      if (keysCtrl[i] == ascChar) {
        return getNk(keys[i]) | NK_CTRL_MASK;
      }
    }
    // Check if ascChar is in the set of chars that must use SHIFT+CTRL and be remapped
    for (uint32_t i = 0; keysShiftCtrl[i] != 0; i++) {
      if (keysShiftCtrl[i] == ascChar) {
        return getNk(keys[i]) | NK_SHIFT_MASK | NK_CTRL_MASK;
      }
    }
    if (ascChar < 32) {
      return ascNkMap[ascChar];
    }
    return NK_NONE;
  }

public:
  NascomKeyboardMap() {
    mainTaskId = xTaskGetCurrentTaskHandle();
  }
  void reset() {
    memset(map, 0, sizeof(map));
  }

  bool setAsciiChar(uint8_t ascChar, bool down) {
    uint8_t nk = getNascomKey(ascChar);
    DEBUG_PRINTF("nk: %02x (%d, %d) %s %s\n", nk, NK_ROW(nk), NK_COL(nk), NK_HAS_SHIFT(nk) ? "SHIFT" : "", NK_HAS_CTRL(nk) ? "CTRL" : "");
    if (nk != NK_NONE) {
      mapIsUpdating = true;
      setKeyAll(nk, down);
      mapIsUpdating = false;
      return true;
    }
    return false;
  }

  void setKey(uint8_t nk, bool down) {
    set(NK_ROW(nk), NK_COL(nk), down);
  }

  void setKeyAll(uint8_t nk, bool down) {
    if (NK_HAS_SHIFT(nk)) {
      setKey(NK_SHIFT, down);
    }
    if (NK_HAS_CTRL(nk)) {
      setKey(NK_CTRL, down);
    }
    setKey(nk, down);
  }
  
  void rewind() {
    mapIndex = 0;
    if (!mapIsUpdating) {
      memcpy(mapSnapshot, map, mapSize);
    }
  }

  void step() {
    mapIndex += 1;
    if (mapIndex == mapSize)
      mapIndex = 0;
  }

  uint8_t current() {
    return mapSnapshot[mapIndex];
  }

  uint32_t getMapIndex() {
    return mapIndex;
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
constexpr uint8_t NascomKeyboardMap::ascNkMap[32];

// Nascom keyboard
class NascomKeyboard {
  fabgl::Keyboard        keyboard;
  NascomKeyboardMap      map;
  NascomControl         &control;
  bool                   shiftDown = false;
  bool                   ctrlDown  = false;
  const char            *startText;
  uint32_t               startTextIndex = 0;
  bool                   startTextKeyDown = false;
  static NascomKeyboard *self;
  static void handleVirtualKey(fabgl::VirtualKey *vk, bool down) {
    DEBUG_PRINTF("%s (%s)\n", self->keyboard.virtualKeyToString(*vk), down ? "down" : "up");
    if (down && *vk == fabgl::VK_F1) {
      if (!self->control.getIsActive()) {
        self->control.activate();
      }
      else {
        self->control.deactivate();
      }
    }
    if (self->control.getIsActive()) {
      self->control.handleVirtualKey(vk, down);
      return;
    }
    uint8_t shiftCtrlMask = 0;
    if (*vk == fabgl::VK_LSHIFT || *vk == fabgl::VK_RSHIFT) {
      self->shiftDown = down;
    }
    if (*vk == fabgl::VK_LCTRL || *vk == fabgl::VK_RCTRL) {
      self->ctrlDown = down;
    }
    if (self->shiftDown) {
      shiftCtrlMask = NK_SHIFT_MASK;
    }
    if (self->ctrlDown) {
      shiftCtrlMask |= NK_CTRL_MASK;
    }
    bool mapUpdated = true;
    // Handle special non-ascii characters
    switch (*vk) {
      case fabgl::VK_UP:
        self->map.setKeyAll(NK_UP | shiftCtrlMask, down);
        break;
      case fabgl::VK_DOWN:
        self->map.setKeyAll(NK_DOWN | shiftCtrlMask, down);
        break;
      case fabgl::VK_LEFT:
        self->map.setKeyAll(NK_LEFT | shiftCtrlMask, down);
        break;
      case fabgl::VK_RIGHT:
        self->map.setKeyAll(NK_RIGHT | shiftCtrlMask, down);
        break;
      default:
        mapUpdated = false;
        break;
    }
    // Handle ascii characters
    if (!mapUpdated) {
      int asc = self->keyboard.virtualKeyToASCII(*vk);
      DEBUG_PRINTF("ASCII: 0x%02x\n", asc);
      if (asc != -1)
        mapUpdated = self->map.setAsciiChar(asc, down);
    }
    if (mapUpdated) {
      //DEBUG_PRINTF("Nascom Keyboard Map\n");
      //self->map.dump();
    }
  }
public:
  NascomKeyboard(NascomControl &control, const char *startText = "") : control(control), startText(startText) {
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
    if (startText[startTextIndex] != 0) {
      if (startTextKeyDown) {
        map.setAsciiChar(startText[startTextIndex], false);
        startTextKeyDown = false;
        startTextIndex += 1;
      }
      else {
        map.setAsciiChar(startText[startTextIndex], true);
        startTextKeyDown = true;
      }
    }
    map.rewind();
  }
  void mapStep() {
    map.step();
  }
  uint8_t mapCurrent() {
    return map.current();
  }

  uint32_t mapGetIndex() {
    return map.getMapIndex();
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
  #define P0_OUT_KEYBOARD_INCREMENT 1 << 0
  #define P0_OUT_KEYBOARD_RESET     1 << 1
  #define P0_OUT_TAPE_LED           1 << 4
  #define P2_IN_UART_TBR_EMPTY      1 << 6
  #define P2_IN_UART_DATA_READY     1 << 7

  NascomKeyboard &keyboard;
  NascomTape     &tape;
  uint8_t        p0LastValue;
public:
  NascomIo(NascomKeyboard &keyboard, NascomTape &tape) : keyboard(keyboard), tape(tape), p0LastValue(0) {}
  uint8_t in(uint32_t port) {
    //DEBUG_PRINTF("in(%d) called\n", port);
    switch (port) {
      case 0: {
        uint8_t val = keyboard.mapCurrent();
        return ~val;
      }
      case 1:
        if (tape.hasData() && tape.getLed()) {
          return tape.readByte();
        }
        else {
          return 0;
        }
      case 2: {
          // UART Status: Always ready to send data. Query tape object for input available
          return P2_IN_UART_TBR_EMPTY | (tape.hasData() && tape.getLed() ? P2_IN_UART_DATA_READY : 0);
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
        uint8_t one2Zero = p0LastValue & ~value;
        if ((value & P0_OUT_KEYBOARD_RESET) != 0) {
          keyboard.mapRewind();
        }
        else if ((zero2One & P0_OUT_KEYBOARD_INCREMENT) != 0) {
          //DEBUG_PRINTF("out(0): value: %02x, zero2One: %02x\n", value, zero2One);
          keyboard.mapStep();
        }
        if ((zero2One & P0_OUT_TAPE_LED) != 0) {
          tape.setLed(true);
          tape.openFiles();
        }
        if ((one2Zero & P0_OUT_TAPE_LED) != 0) {
          tape.setLed(false);
          tape.closeFiles();
        }
        p0LastValue = value;
        break;
      }
      case 1:
        tape.writeByte(value);
        break;
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
  NascomControl &control;

  static NascomCpu *self;

  static int simAction() {
    static uint32_t count   = 0;
    static uint32_t start   = millis();
    static uint32_t delayMs = 25;
    count++;
    if (count == UI_REFRESH_RATE) {
      int32_t now = millis();
      //DEBUG_PRINTF("simAction called %d times: %.3f. delayMs: %d\n", UI_REFRESH_RATE, ((float)(now - start))/1000, delayMs);
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
    if (self->control.getIsActive()) {
      return -1;
    }
    else {
      return 0;
    }
  }

public:
  NascomCpu(NascomDisplay &display, NascomMemory &memory, NascomControl &control) : display(display), memory(memory), control(control) {
    self = this;
  }
  void run() {
    bool controlScreen = false;
    z80::pc = 0;
    while (true) {
      if (!control.getIsActive()) {
        controlScreen = false;
        z80::simz80(z80::pc, INSN_PER_REFRESH, simAction);
      }
      else {
        if (!controlScreen) {
          control.showScreen();
          controlScreen = true;
        }
      }
    }
  }
};
NascomCpu *NascomCpu::self = nullptr;

NascomDisplay   nascomDisplay;
NascomTape      nascomTape;
NascomControl   nascomControl(nascomDisplay, nascomTape);
NascomKeyboard  nascomKeyboard(nascomControl, startText);
NascomMemory    nascomMemory(z80::ram);
NascomIo        nascomIo(nascomKeyboard, nascomTape);
NascomCpu       nascomCpu(nascomDisplay, nascomMemory, nascomControl);

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
  if (!SD.begin(Pins::sdCs)) {
    DEBUG_PRINTF("SD card mount failed\n");
  }

  DEBUG_PRINTF("Internal files:\n");
  File dir = LittleFS.open("/");
  while (File file = dir.openNextFile()) {
    DEBUG_PRINTF("Name: %s, Size: %d\n", file.name(), file.size());
    file.close();
  }
  dir.close();

  DEBUG_PRINTF("External files:\n");
  dir = SD.open("/");
  while (File file = dir.openNextFile()) {
    DEBUG_PRINTF("Name: %s, Size: %d\n", file.name(), file.size());
    file.close();
  }
  dir.close();
  SD.end();

  nascomTape.init();
  nascomDisplay.init();
  nascomKeyboard.init();
//  nascomTape.setInputFile(&LittleFS, "/blspascal13.cas");
  nascomTape.setInputFile(&SD, "/Nip.cas");
  nascomTape.setOutputFile(&LittleFS, "/tape-out.cas");
  nascomMemory.nasFileLoad("/nassys3.nal");
  nascomMemory.nasFileLoad("/basic.nal");
  nascomMemory.nasFileLoad("/skakur.nas");
  nascomMemory.nasFileLoad("/BLS-maanelander.nas");
  nascomTape.setLed(false);
  nascomCpu.run();
  DEBUG_PRINTF("pc = %04x, sp = %04x\n", z80::pc, z80::sp);
}

void loop() {
  // Should never get here
  vTaskSuspend(NULL);
}
