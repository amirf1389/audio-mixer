/*
  SceneRecall: four buttons recall four scenes; hold a button for two seconds to save the console as it is now into that scene.
  Scenes are kept in this board's flash (ESP32 NVS), so they survive a restart.

  Wiring (ESP32): buttons from GPIO 13, 12, 14, 27 to GND (internal pull-ups).
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

const int SCENES = 4;
const int BUTTON_PINS[SCENES] = {13, 12, 14, 27};
unsigned long pressedAt[SCENES];

void loadScenes() {
#if defined(ARDUINO_ARCH_ESP32)
  prefs.begin("amfx1", true);
  for (int i = 0; i < SCENES; ++i) {
    char key[8];
    snprintf(key, sizeof key, "scene%d", i + 1);
    amfx1_scene* s = console.sceneData(i + 1);
    if (s && prefs.getBytesLength(key) == sizeof(amfx1_scene)) prefs.getBytes(key, s, sizeof(amfx1_scene));
  }
  prefs.end();
#endif
}

void storeScene(int n) {
#if defined(ARDUINO_ARCH_ESP32)
  char key[8];
  snprintf(key, sizeof key, "scene%d", n);
  prefs.begin("amfx1", false);
  prefs.putBytes(key, console.sceneData(n), sizeof(amfx1_scene));
  prefs.end();
#else
  (void)n;
#endif
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < SCENES; ++i) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
    pressedAt[i] = 0;
  }
  connectConsole("SceneRecall");
  loadScenes();
}

void loop() {
  console.loop();
  for (int i = 0; i < SCENES; ++i) {
    bool pressed = digitalRead(BUTTON_PINS[i]) == LOW;
    if (pressed && pressedAt[i] == 0) pressedAt[i] = millis();
    if (!pressed && pressedAt[i] != 0) {
      unsigned long held = millis() - pressedAt[i];
      pressedAt[i] = 0;
      if (held < 30) continue;   // a bounce
      if (held >= 2000) {
        if (console.saveScene(i + 1)) {
          storeScene(i + 1);
          Serial.print("Saved scene ");
        } else {
          Serial.print("Could not save scene ");
        }
      } else {
        Serial.print(console.recallScene(i + 1) ? "Recalled scene " : "Empty scene ");
      }
      Serial.println(i + 1);
    }
  }
}
