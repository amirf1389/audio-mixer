/*
  TallyLight: an ON AIR light is lit while a channel is live: not muted and its fader above -60 dB (and the master not muted).

  Wiring (ESP32): a relay module or a MOSFET driving the lamp on GPIO 26 (active HIGH); the on-board LED (GPIO 2) shows the same.
  Use a relay module made for mains switching if the lamp runs on mains, and keep mains wiring away from the board.
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
const int TALLY_PIN = 26;
const int BOARD_LED = 2;
const float LIVE_ABOVE_DB = -60;

bool live = false;

void updateTally(AmFX1& c) {
  bool now = !c.getMute(CHANNEL) && c.getFader(CHANNEL) > LIVE_ABOVE_DB && !c.getMasterMute();
  if (now != live) {
    live = now;
    digitalWrite(TALLY_PIN, live ? HIGH : LOW);
    digitalWrite(BOARD_LED, live ? HIGH : LOW);
    Serial.println(live ? "ON AIR" : "off air");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(TALLY_PIN, OUTPUT);
  pinMode(BOARD_LED, OUTPUT);
  console.onState(updateTally);
  connectConsole("TallyLight");
  updateTally(console);
}

void loop() {
  console.loop();
  if (!console.connected() && live) {   // no news from the console: do not claim to be on air
    live = false;
    digitalWrite(TALLY_PIN, LOW);
    digitalWrite(BOARD_LED, LOW);
  }
}
