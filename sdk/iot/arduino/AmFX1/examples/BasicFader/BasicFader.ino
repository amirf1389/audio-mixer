/*
  BasicFader: a potentiometer moves one channel fader.

  Wiring: a 10 k linear potentiometer, ends to 3V3 and GND, wiper to GPIO 34 (ESP32) or A0.
  The pot is read 50 times a second; a move of more than 0.5 dB is sent, so a noisy pot does not flood the network.
*/

#include <AmFX1.h>

// ---- network and pairing (the same in every example) ----------------------------------------------------------------------
// Fill in your own network and the console's address (the address the console shows for phone pairing). Never commit real credentials.
const char* WIFI_SSID = "your-network";
const char* WIFI_PASSWORD = "your-password";
const char* CONSOLE_ADDRESS = "192.168.1.50";   // the computer running AmF X1 (an IPv4 address)

#if defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#include <Preferences.h>
Preferences prefs;
AmFX1 console(CONSOLE_ADDRESS);
#else
#include <WiFiNINA.h>   // Arduino Nano 33 IoT, MKR WiFi 1010, Uno WiFi Rev2 (EthernetClient works the same way)
WiFiClient net;
AmFX1 console(net, CONSOLE_ADDRESS);
#endif

// the token is the key to the console: kept in flash (ESP32 NVS), never printed, never in the sketch
String loadToken() {
#if defined(ARDUINO_ARCH_ESP32)
  prefs.begin("amfx1", true);
  String t = prefs.getString("token", "");
  prefs.end();
  return t;
#else
  return "";   // on these boards keep it in EEPROM / flash storage of your choice, or pair at every start
#endif
}

void saveToken(const char* t) {
#if defined(ARDUINO_ARCH_ESP32)
  prefs.begin("amfx1", false);
  prefs.putString("token", t);
  prefs.end();
#else
  (void)t;
#endif
}

void connectConsole(const char* deviceName) {
  Serial.print("Wi-Fi ");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.println(" connected");
  String token = loadToken();
  if (token.length() && console.begin(token.c_str())) {
    Serial.println("AmF X1 connected");
    return;
  }
  // first start, or the token expired: pair with the PIN the console shows (where it pairs phones: the same PIN works for this board)
  while (true) {
    Serial.println("Type the 6-digit PIN shown on the console:");
    while (!Serial.available()) delay(10);
    String pin = Serial.readStringUntil('\n');
    pin.trim();
    if (console.pair(pin.c_str(), deviceName)) {
      saveToken(console.token());
      Serial.println("Paired and connected");
      return;
    }
    Serial.print("Pairing failed: ");
    Serial.println(console.lastError());
  }
}
// ---------------------------------------------------------------------------------------------------------------------------

const int CHANNEL = 1;
#if defined(ARDUINO_ARCH_ESP32)
const int POT_PIN = 34;
const int ADC_MAX = 4095;
#else
const int POT_PIN = A0;
const int ADC_MAX = 1023;
#endif

float lastSent = -1000;

// pot position (0..1) to dB with a fader-like curve: the top half covers -20 .. +10 dB, the bottom half down to silence
float positionToDb(float x) {
  if (x < 0.02f) return -120;
  if (x > 0.5f) return -20 + (x - 0.5f) * 60;
  return -60 + x * 80;
}

void setup() {
  Serial.begin(115200);
  connectConsole("BasicFader");
}

void loop() {
  console.loop();
  static unsigned long last = 0;
  if (millis() - last < 20) return;
  last = millis();
  float db = positionToDb(analogRead(POT_PIN) / (float)ADC_MAX);
  if (fabs(db - lastSent) > 0.5f) {
    if (console.setFader(CHANNEL, db)) lastSent = db;
  }
}
