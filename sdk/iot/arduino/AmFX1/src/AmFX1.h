/*
  AmFX1.h - control the AmF X1 console from a microcontroller over Wi-Fi or Ethernet.

  Works with any Arduino networking Client: WiFiClient (ESP32, ESP8266, WiFiNINA, WiFi101, WiFiS3), EthernetClient. On ESP32 and
  ESP8266 the library makes its own WiFiClient; on other boards pass yours to the constructor.

  Channels are numbered as on the console: 1 is the first strip. Faders are in dB (-120 .. +10), pan from -1 (left) to +1 (right).
  Meters are peak dBFS (0 is full scale, -120 silence).

  The console's AmF X1 Network service must accept devices on the local network (the same setting that lets phones pair).
  The connection is not encrypted: use it on a network you trust. See docs/iot-sdk.md for pairing, wiring and token storage.

  MIT License, AMFB Audio.
*/
#ifndef AMFX1_H_ARDUINO
#define AMFX1_H_ARDUINO

#include <Arduino.h>
#include <Client.h>
#include "amfx1_core.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#define AMFX1_OWN_CLIENT 1
#elif defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>
#define AMFX1_OWN_CLIENT 1
#endif

// how the library talks to the console
#define AMFX1_AUTO 0        // WebSocket where it fits (32-bit boards), HTTP polling otherwise or when the socket fails
#define AMFX1_WEBSOCKET 1   // one open connection: state and meters pushed 20 times a second, moves sent on it
#define AMFX1_HTTP 2        // each move is a request; the state and meters are polled (smallest boards)

class AmFX1 {
 public:
  typedef void (*Callback)(AmFX1& console);

#if defined(AMFX1_OWN_CLIENT)
  AmFX1(const char* host, uint16_t port = AMFX1_DEFAULT_PORT);
#endif
  AmFX1(Client& client, const char* host, uint16_t port = AMFX1_DEFAULT_PORT);

  // with a token kept from an earlier pairing; true when the console accepted it
  bool begin(const char* token);
  // trade the 6-digit PIN the console shows for a token of this device's own (30 days, bound to this device's address); store
  // token() in NVS (ESP32 Preferences) or EEPROM and call begin(token) on the next start. begin() is called on success.
  bool pair(const char* pin, const char* deviceName = "AmFX1 controller");
  const char* token() const { return _token; }
  void end();

  void setTransport(uint8_t transport) { _transport = transport; }
  void setPollInterval(uint16_t stateMs, uint16_t meterMs);   // AMFX1_HTTP only; meterMs 0: no meters
  void setTimeout(uint16_t ms) { _timeout = ms < 200 ? 200 : ms; }

  bool setFader(int ch, float db);
  bool setMute(int ch, bool on);
  bool setSolo(int ch, bool on);
  bool setPan(int ch, float pan);
  bool setMaster(float db);
  bool setMasterMute(bool on);
  bool send(const char* json);   // any command of the service's POST /api/control, e.g. {"op":"busLevel","bus":0,"db":-6}

  // scenes are kept on this device (the console's own scenes live in its browser and app): save from the last state received,
  // recall sends only what differs. sceneData() gives the bytes to store in NVS / EEPROM and to put back.
  bool saveScene(int n);
  bool recallScene(int n);
  amfx1_scene* sceneData(int n);

  float getFader(int ch) const;
  bool getMute(int ch) const;
  bool getSolo(int ch) const;
  float getPan(int ch) const;
  float getMeter(int ch) const;   // peak dBFS
  float getMeterRms(int ch) const;
  float getMasterMeter() const;   // the louder side, dBFS
  bool getMasterMute() const { return _m.masterMute; }
  float getMasterFader() const { return _m.masterDb; }
  const char* channelName(int ch) const;
  int channels() const { return _m.strips; }
  bool hasState() const { return _m.haveState; }

  void onState(Callback cb) { _onState = cb; }
  void onMeter(Callback cb) { _onMeter = cb; }
  void onError(Callback cb) { _onError = cb; }

  // call often from loop(): reads what the console sent, reconnects after a drop, polls in HTTP mode, runs the callbacks
  void loop();
  bool connected() const;
  bool usingWebSocket() const { return _ws; }
  const char* lastError() const { return _error; }

 private:
  bool openWs();
  bool waitFirstFrame();
  int http(const char* method, const char* path, const char* body);
  bool command(const char* json);
  bool resolveHost();
  void fire(uint16_t flags);
  void fail(const char* e);
  void stopClient();
  void feedWs(const uint8_t* d, size_t n);
  void writeFrame(int opcode, const uint8_t* p, size_t n);
  static void randomBytes(uint8_t* out, size_t n);

#if defined(AMFX1_OWN_CLIENT)
  WiFiClient _own;
#endif
  Client* _client;
  char _host[40];
  char _hostHeader[40];
  uint16_t _port;
  char _token[AMFX1_TOKEN_MAX + 1];
  uint8_t _transport;
  bool _ws, _authFail;
  uint16_t _timeout, _stateMs, _meterMs;
  unsigned long _nextRetry, _lastState, _lastMeter, _heldUntil;
  long _seq;
  amfx1_model _m;
  amfx1_ws_rx _wsrx;
  amfx1_scene _scenes[AMFX1_MAX_SCENES];
  char _error[64];
  Callback _onState, _onMeter, _onError;
};

#endif
