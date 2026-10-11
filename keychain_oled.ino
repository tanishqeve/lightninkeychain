// OLED keychain: ESP32-C3 + 1.5" full-color SSD1351 OLED (128x128).
// Screens (press the button to switch): Image -> Digital time/date -> Analog clock.
// After 15 s with no button press, it "sleeps": very dim always-on analog clock.
// Press the button to wake. Time is set from the website (Chrome) over Bluetooth.
// Libraries: "Adafruit SSD1351 library" and "Adafruit GFX Library".

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1351.h>
#include <SPI.h>
#include <LittleFS.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>

// Wiring (change to match your board)
#define PIN_SCK   4
#define PIN_MOSI  6
#define PIN_CS    7
#define PIN_DC    5
#define PIN_RST   3
#define PIN_BTN   9            // button to GND (GPIO 9 is the BOOT button on most C3 boards)

#define SRC       64
#define SCALE     2
#define DST       (SRC * SCALE)
#define CHUNK     128
#define IMG_BYTES (SRC * SRC * 2)

#define AWAKE_MS        15000  // time awake before sleeping
#define CONTRAST_FULL   0x0F   // 0x00-0x0F master contrast
#define CONTRAST_SLEEP  0x01   // very low brightness for the sleep clock
#define CMD_CONTRAST    0xC7

#define SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHAR_UUID    "6e400002-b5a3-f393-e0a9-e50e24dcca9e"

enum Screen { SCR_IMAGE, SCR_DIGITAL, SCR_ANALOG, SCR_SLEEP };

Adafruit_SSD1351 oled(128, 128, &SPI, PIN_CS, PIN_DC, PIN_RST);
GFXcanvas16 canvas(DST, DST);          // draw here, then push to the OLED (no flicker)
uint8_t img[IMG_BYTES];

Screen screen = SCR_IMAGE;
volatile bool imageReady = false;
volatile bool timeSynced = false;
bool hasImage = false, needRender = true, btnWasDown = false;
uint32_t lastActive = 0, lastBtn = 0;
int lastSec = -1;

void setContrast(uint8_t v) { oled.sendCommand(CMD_CONTRAST, &v, 1); }

void saveImage() {
  File f = LittleFS.open("/img.bin", "w");
  if (f) { f.write(img, IMG_BYTES); f.close(); }
}
bool loadImage() {
  File f = LittleFS.open("/img.bin", "r");
  if (!f) return false;
  bool ok = f.read(img, IMG_BYTES) == IMG_BYTES;
  f.close();
  return ok;
}

void drawImage() {
  if (!hasImage) {
    canvas.setTextColor(0xFFFF); canvas.setTextSize(1);
    canvas.setCursor(20, 60); canvas.print("Waiting for image");
    return;
  }
  for (int y = 0; y < SRC; y++)
    for (int x = 0; x < SRC; x++) {
      int p = (y * SRC + x) * 2;
      canvas.fillRect(x * SCALE, y * SCALE, SCALE, SCALE, ((uint16_t)img[p] << 8) | img[p + 1]);
    }
}

void drawDigital(const tm &t) {
  char buf[16];
  canvas.setTextSize(2);
  canvas.setTextColor(0xFFFF);
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
  canvas.setCursor(16, 36); canvas.print(buf);
  canvas.setTextColor(0x07FF);
  snprintf(buf, sizeof(buf), "%02d/%02d/%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
  canvas.setCursor(4, 74); canvas.print(buf);
  if (!timeSynced) {
    canvas.setTextSize(1); canvas.setTextColor(0xF800);
    canvas.setCursor(10, 110); canvas.print("Sync time from website");
  }
}

void drawHand(float deg, int len, uint16_t color, bool thick) {
  float a = deg * M_PI / 180.0;
  int x = 64 + len * sin(a), y = 64 - len * cos(a);
  canvas.drawLine(64, 64, x, y, color);
  if (thick) { canvas.drawLine(65, 64, x + 1, y, color); canvas.drawLine(64, 65, x, y + 1, color); }
}

void drawAnalog(const tm &t, bool dim) {
  uint16_t face = dim ? 0x39E7 : 0xFFFF;   // ticks and ring
  uint16_t hand = dim ? 0x7BEF : 0xFFFF;
  uint16_t secC = dim ? 0x4000 : 0xF800;
  canvas.drawCircle(64, 64, 62, face);
  for (int i = 0; i < 12; i++) {
    float a = i * 30 * M_PI / 180.0;
    int r1 = (i % 3 == 0) ? 50 : 56;
    canvas.drawLine(64 + r1 * sin(a), 64 - r1 * cos(a), 64 + 60 * sin(a), 64 - 60 * cos(a), face);
  }
  drawHand(((t.tm_hour % 12) + t.tm_min / 60.0) * 30, 32, hand, true);
  drawHand((t.tm_min + t.tm_sec / 60.0) * 6, 48, hand, false);
  drawHand(t.tm_sec * 6, 54, secC, false);
  canvas.fillCircle(64, 64, 2, hand);
}

void render() {
  time_t now = time(nullptr);
  tm t; localtime_r(&now, &t);
  canvas.fillScreen(0);
  switch (screen) {
    case SCR_IMAGE:   drawImage(); break;
    case SCR_DIGITAL: drawDigital(t); break;
    case SCR_ANALOG:  drawAnalog(t, false); break;
    case SCR_SLEEP:   drawAnalog(t, true); break;
  }
  oled.drawRGBBitmap(0, 0, canvas.getBuffer(), DST, DST);
}

void wake(Screen s) {
  screen = s; lastActive = millis();
  setContrast(CONTRAST_FULL); needRender = true;
}

// Packet: bytes 0-1 = chunk number (big-endian), then data.
// 0xFFFF = image complete. 0xFFFE = set time (4 bytes: local time as Unix seconds).
class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) {
    auto v = c->getValue();
    size_t n = v.length();
    if (n < 2) return;
    uint16_t chunk = ((uint8_t)v[0] << 8) | (uint8_t)v[1];
    if (chunk == 0xFFFF) { imageReady = true; return; }
    if (chunk == 0xFFFE && n >= 6) {
      uint32_t e = ((uint32_t)(uint8_t)v[2] << 24) | ((uint32_t)(uint8_t)v[3] << 16) |
                   ((uint32_t)(uint8_t)v[4] << 8) | (uint8_t)v[5];
      timeval tv = { (time_t)e, 0 };
      settimeofday(&tv, nullptr);
      timeSynced = true;
      return;
    }
    size_t offset = (size_t)chunk * CHUNK;
    for (size_t i = 2; i < n && offset + i - 2 < IMG_BYTES; i++)
      img[offset + i - 2] = (uint8_t)v[i];
  }
};
class ServerCallbacks : public BLEServerCallbacks {
  void onDisconnect(BLEServer *s) { BLEDevice::startAdvertising(); }
};

void setup() {
  pinMode(PIN_BTN, INPUT_PULLUP);
  SPI.begin(PIN_SCK, -1, PIN_MOSI, PIN_CS);
  oled.begin();
  oled.fillScreen(0);
  LittleFS.begin(true);
  hasImage = loadImage();
  wake(SCR_IMAGE);

  BLEDevice::init("PixelKeychain");
  BLEDevice::setMTU(517);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  BLEService *service = server->createService(SERVICE_UUID);
  BLECharacteristic *ch = service->createCharacteristic(
      CHAR_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  ch->setCallbacks(new RxCallbacks());
  service->start();
  BLEDevice::getAdvertising()->addServiceUUID(SERVICE_UUID);
  BLEDevice::getAdvertising()->start();
}

void loop() {
  uint32_t ms = millis();

  bool down = digitalRead(PIN_BTN) == LOW;
  if (down && !btnWasDown && ms - lastBtn > 250) {
    lastBtn = ms;
    if (screen == SCR_SLEEP) wake(SCR_IMAGE);
    else {
      screen = (screen == SCR_IMAGE) ? SCR_DIGITAL : (screen == SCR_DIGITAL) ? SCR_ANALOG : SCR_IMAGE;
      lastActive = ms; needRender = true;
    }
  }
  btnWasDown = down;

  if (imageReady) { imageReady = false; hasImage = true; saveImage(); wake(SCR_IMAGE); }

  if (screen != SCR_SLEEP && ms - lastActive > AWAKE_MS) {
    screen = SCR_SLEEP; setContrast(CONTRAST_SLEEP); needRender = true;
  }

  time_t now = time(nullptr);
  tm t; localtime_r(&now, &t);
  if (screen != SCR_IMAGE && t.tm_sec != lastSec) needRender = true;
  lastSec = t.tm_sec;

  if (needRender) { needRender = false; render(); }
  delay(50);
}
