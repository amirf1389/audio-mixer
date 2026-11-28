/*
  EncoderPan: a rotary encoder pans a channel; its push button centres the pan.

  Wiring (ESP32): encoder A to GPIO 18, B to GPIO 19, switch to GPIO 5, common to GND (internal pull-ups).
  Each detent moves the pan by 2 %.
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
const int PIN_A = 18, PIN_B = 19, PIN_SW = 5;
volatile int steps = 0;
int lastA = HIGH;
bool lastSw = false;

void readEncoder() {
  int a = digitalRead(PIN_A);
  if (a != lastA && a == LOW) steps += digitalRead(PIN_B) == HIGH ? 1 : -1;
  lastA = a;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_A, INPUT_PULLUP);
  pinMode(PIN_B, INPUT_PULLUP);
  pinMode(PIN_SW, INPUT_PULLUP);
  connectConsole("EncoderPan");
}

void loop() {
  console.loop();
  readEncoder();
  static unsigned long last = 0;
  if (millis() - last < 30) return;   // gather the detents of 30 ms into one move
  last = millis();
  bool sw = digitalRead(PIN_SW) == LOW;
  if (sw && !lastSw) console.setPan(CHANNEL, 0);
  lastSw = sw;
  if (steps != 0) {
    float pan = console.getPan(CHANNEL) + steps * 0.02f;
    steps = 0;
    if (pan < -1) pan = -1;
    if (pan > 1) pan = 1;
    console.setPan(CHANNEL, pan);
  }
}
