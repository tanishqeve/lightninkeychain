Lightning Keychain

A pocket-sized keychain with a full-color OLED screen. Draw pixel art, pick an emoji, or upload a photo on a web page, and send it to the keychain over Bluetooth. It also works as a clock: digital time and date, plus a dim always-on analog clock while it sleeps.

Status: prototype. The firmware and web page have not yet been tested on real hardware, so small fixes may be needed. Issues and pull requests are welcome.

Features

	•	Send drawings, emojis, and photos from a web page to the keychain over Bluetooth LE
	•	Full-color 128x128 OLED (images are sent at 64x64 and shown at 2x)
	•	Digital clock (HH:MM:SS) and date (DD/MM/YYYY)
	•	After 15 seconds of no button presses it sleeps: a very-low-brightness analog clock stays on
	•	Remembers the last image after power-off
	•	One button switches between image, digital clock, and analog clock
	•	Rechargeable over USB-C

Hardware

Part	Notes
ESP32-C3 Super Mini	Microcontroller with Bluetooth LE
1.5" RGB OLED, SSD1351, 128x128, SPI	The code is written for this display
TP4056 USB-C charging board with protection	Charges the battery
3.7V LiPo battery, about 500 mAh, with protection circuit	Check the polarity before connecting
Mini slide switch	Power on/off
Push button (optional)	The BOOT button on GPIO 9 also works
Wires, heat shrink, small case, keyring	

A cheaper 0.95" SSD1331 (96x64) display also exists, but it needs code changes (different library and image size). It is not supported yet.

Wiring

OLED to ESP32-C3 Super Mini:

OLED pin	ESP32-C3 pin
VCC	3V3
GND	GND
SCL / CLK	GPIO 4
SDA / DIN	GPIO 6
CS	GPIO 7
DC	GPIO 5
RES / RST	GPIO 3

Button: one leg to GPIO 9, the other to GND.

Power: battery to the TP4056 B+ / B-, TP4056 OUT+ through the slide switch to the ESP32 5V pin, and TP4056 OUT- to GND.

Safety: use a protected LiPo cell, double-check polarity, and never puncture or crush the battery.

You can change the pins at the top of the firmware file.

Firmware setup

	1.	Install Arduino IDE.
	2.	Add the ESP32 by Espressif board package (Boards Manager).
	3.	Install the libraries Adafruit SSD1351 library and Adafruit GFX Library (Library Manager).
	4.	Select the board ESP32C3 Dev Module and enable USB CDC On Boot.
	5.	Open firmware/keychain_oled/keychain_oled.ino, choose your port, and click Upload.

Settings at the top of the file:

Setting	Meaning
PIN_*	Wiring pins
AWAKE_MS	Time awake before sleeping (default 15000 ms)
CONTRAST_FULL / CONTRAST_SLEEP	OLED brightness, 0x00 to 0x0F

Website

The page in web/index.html runs in the browser with no server code.

	•	It needs Web Bluetooth: use Chrome on Android or desktop. iPhone/Safari is not supported.
	•	It must be opened over HTTPS (or localhost). GitHub Pages works well.

To publish on GitHub Pages: push this repository, then open Settings → Pages and choose the branch and the /web folder (or move index.html to the repository root).

Using it

	1.	Draw, type an emoji, or upload a photo. The preview shows what will be sent.
	2.	Tap Send to keychain and pick PixelKeychain in the Bluetooth window.
	3.	The page also sends your device's current time. Use Sync time to update only the clock.

Keychain buttons

Action	Result
Press while awake	Cycle: image, digital clock, analog clock
Press while sleeping	Wake up and show the image

The keychain has no clock battery, so the time resets if the power is cut. Sync it again from the web page.

How it works

The keychain advertises as PixelKeychain with one writable Bluetooth characteristic.

	•	Service UUID: 6e400001-b5a3-f393-e0a9-e50e24dcca9e
	•	Characteristic UUID: 6e400002-b5a3-f393-e0a9-e50e24dcca9e

Every packet starts with a 2-byte chunk number (big-endian):

Chunk number	Meaning
0x0000 to 0x003F	Image data: 128 bytes per packet (64x64 pixels, RGB565, high byte first)
0xFFFF	Image complete: show it and save it to flash
0xFFFE	Set the clock: 4 more bytes, local time as Unix seconds (big-endian)

The image is saved in the ESP32's flash (LittleFS) and reloaded at boot.

Repository layout

firmware/keychain_oled/keychain_oled.ino   ESP32 firmware (Arduino)
web/index.html                              Web page (rename of keychain_oled.html)
README.md

Troubleshooting

	•	Blank screen: re-check the SCL, SDA, CS, DC, and RES wires and the 3.3V supply.
	•	Can't find the keychain in the Bluetooth window: make sure it is powered and use Chrome (not Safari or Firefox).
	•	"Web Bluetooth isn't supported": open the page in Chrome on Android or desktop, over HTTPS.
	•	Brightness command has no effect or a compile error appears: your library version may differ; open an issue with the error text.
	•	Wrong time after reboot: sync the time again from the web page.

Roadmap

	☐︎	Test on real hardware
	☐︎	Support for the cheaper 0.95" SSD1331 display
	☐︎	Several saved images that cycle automatically
	☐︎	Wi-Fi mode for iPhone
