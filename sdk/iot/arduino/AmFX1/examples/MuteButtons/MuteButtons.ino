/*
  MuteButtons: four buttons toggle the mutes of channels 1..4; four LEDs show each mute as the console has it,
  also when it is changed on the console or from a phone.

  Wiring (ESP32): buttons from GPIO 13, 12, 14, 27 to GND (internal pull-ups); LEDs with 330 ohm resistors on GPIO 25, 26, 32, 33.
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

const int CHANNELS = 4;
const int BUTTON_PINS[CHANNELS] = {13, 12, 14, 27};
const int LED_PINS[CHANNELS] = {25, 26, 32, 33};
bool lastPressed[CHANNELS];
unsigned long lastChange[CHANNELS];

void showMutes(AmFX1& c) {
  for (int i = 0; i < CHANNELS; ++i) digitalWrite(LED_PINS[i], c.getMute(i + 1) ? HIGH : LOW);
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < CHANNELS; ++i) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
    pinMode(LED_PINS[i], OUTPUT);
    lastPressed[i] = false;
    lastChange[i] = 0;
  }
  console.onState(showMutes);
  connectConsole("MuteButtons");
  showMutes(console);
}

void loop() {
  console.loop();
  for (int i = 0; i < CHANNELS; ++i) {
    bool pressed = digitalRead(BUTTON_PINS[i]) == LOW;
    if (pressed != lastPressed[i] && millis() - lastChange[i] > 30) {   // 30 ms debounce
      lastChange[i] = millis();
      lastPressed[i] = pressed;
      if (pressed) {
        console.setMute(i + 1, !console.getMute(i + 1));
        showMutes(console);
      }
    }
  }
}
