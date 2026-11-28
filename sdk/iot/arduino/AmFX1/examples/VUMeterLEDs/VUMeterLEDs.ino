/*
  VUMeterLEDs: a bar of 8 LEDs shows a channel's peak meter (or the master with CHANNEL 0).

  Wiring (ESP32): 8 LEDs with 330 ohm resistors on GPIO 15, 2, 4, 16, 17, 21, 22, 23 (green, green, green, green, green, yellow,
  yellow, red). The steps are -48, -36, -24, -18, -12, -6, -3 and 0 dBFS; the peak falls back at 20 dB a second.
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

const int CHANNEL = 1;   // 0: the master
const int LEDS = 8;
const int LED_PINS[LEDS] = {15, 2, 4, 16, 17, 21, 22, 23};
const float STEPS_DB[LEDS] = {-48, -36, -24, -18, -12, -6, -3, 0};
float shown = -120;
unsigned long lastDraw = 0;

void onMeters(AmFX1& c) {
  float db = CHANNEL == 0 ? c.getMasterMeter() : c.getMeter(CHANNEL);
  if (db > shown) shown = db;   // the bar jumps up at once
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < LEDS; ++i) pinMode(LED_PINS[i], OUTPUT);
  console.onMeter(onMeters);
  connectConsole("VUMeterLEDs");
}

void loop() {
  console.loop();
  unsigned long now = millis();
  if (now - lastDraw < 20) return;
  shown -= (now - lastDraw) * 0.02f;   // and falls back slowly
  if (shown < -120) shown = -120;
  lastDraw = now;
  for (int i = 0; i < LEDS; ++i) digitalWrite(LED_PINS[i], shown >= STEPS_DB[i] ? HIGH : LOW);
}
