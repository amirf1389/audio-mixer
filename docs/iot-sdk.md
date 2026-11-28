# AmF X1 IoT SDK (ESP32, Arduino, ESP-IDF)

The IoT SDK lets you control AmF X1 from your own hardware over Wi-Fi or Ethernet. You can build pots and motor faders, mute buttons with LEDs, rotary encoders, LED meter bars and tally lights. The SDK talks to the console's network service, **AmF X1 Network**. This is the same service the phone remote and the browser remote use.

| Part | Where | For |
|---|---|---|
| Arduino library `AmFX1` | `sdk/iot/arduino/AmFX1` | ESP32, ESP8266 and any board with an Arduino `Client` (WiFiNINA, WiFi101, WiFiS3, Ethernet) |
| ESP-IDF component `amfx1` | `sdk/iot/esp-idf/amfx1` | ESP-IDF 5.x projects, through the C API `amfx1_*` |
| Protocol core | `sdk/iot/arduino/AmFX1/src/amfx1_core.{h,c}` | Used by both of the above and by the host test |

The SDK contains no third-party code and allocates no memory in its core. It does not depend on ArduinoJson or on a WebSocket library: the JSON reader, HTTP and WebSocket (RFC 6455 client frames) code is written for this SDK. The JSON reader is a streaming scanner. It reads the console's full state document, which can be 30 KB, in pieces of any size and uses only a few hundred bytes of RAM.

## Protocol

The service listens on port **8765** by default.

| What | How |
|---|---|
| Authentication | The `X-Amix-Token` header on every request. The SDK never puts the token in a URL, because URLs end up in logs. |
| Pairing | `POST /api/pair {"pin":"123456","name":"Desk"}` returns `{"ok":true,"token":"amx_…","role":"control","days":30}`. The PIN is the 6-digit code the console shows for pairing phones. It changes every minute, and one PIN pairs one device. After 5 wrong PINs, the service waits for the next PIN. |
| Live link | `WS /ws/meters` (token in the header). The service sends `{"state":{…},"wsControl":true}` when anything changes, a meters frame 20 times a second, and `{"kicked":true}` when the console disconnects the device. The device sends its moves as text frames with a `seq`. Errors come back as `{"ack":seq,"controlError":"…"}`. |
| Polling | `GET /api/state?since=v` returns `{"same":true}` while nothing has changed. `GET /api/meters` returns the meters. `POST /api/control` sends one command per request. |
| Commands | `{"op":"gain","strip":0,"db":-6}`, `{"op":"mute","strip":0,"on":true}`, `{"op":"solo",…}`, `{"op":"pan","strip":0,"pan":-0.5}`, `{"op":"master","db":-3}`, `{"op":"master","mute":true}`. `send()` / `amfx1_send_raw()` sends any other command the service accepts. |

`strip` is 0-based in the protocol. The SDK numbers channels as the console does, starting at **1**. Faders are clamped to -120 .. +10 dB and pan to -1 .. +1. Out-of-range channels are rejected on the device before anything is sent. Meters arrive as linear peaks (1.0 is full scale), and the SDK converts them to dBFS (-120 is silence).

**Scenes.** The console keeps its scenes in the browser and app that saved them, so the SDK keeps scenes on the board. `saveScene(n)` snapshots the last state received: every fader, pan, mute, solo and the master level. `recallScene(n)` sends only what differs from the console. `sceneData(n)` returns the scene's bytes so you can store them in NVS or EEPROM (see the SceneRecall example).

## Arduino quick start

1. Install the library. Copy `sdk/iot/arduino/AmFX1` into your `Arduino/libraries` folder, or zip that folder and use **Sketch > Include Library > Add .ZIP Library**.
2. On the console, allow devices on the local network. This is the same setting that lets phones pair.
3. Open an example, then fill in `WIFI_SSID`, `WIFI_PASSWORD` and `CONSOLE_ADDRESS`. Use the console's IPv4 address. The service only answers requests that name it by address.
4. Upload the sketch and open the Serial Monitor at 115200 baud. When the sketch asks, type the PIN the console shows. The token is then saved to NVS, and the board reconnects by itself on every restart.

| Example | Hardware | Pins (ESP32) |
|---|---|---|
| BasicFader | 10 kΩ linear pot: ends to 3V3/GND, wiper to the ADC | GPIO 34 (A0 elsewhere) |
| MuteButtons | 4 push buttons to GND (internal pull-ups), 4 LEDs + 330 Ω | buttons 13, 12, 14, 27; LEDs 25, 26, 32, 33 |
| EncoderPan | Rotary encoder (A, B, push switch) to GND | A 18, B 19, switch 5 |
| VUMeterLEDs | 8 LEDs + 330 Ω (5 green, 2 yellow, 1 red) | 15, 2, 4, 16, 17, 21, 22, 23 |
| TallyLight | Relay module or logic-level MOSFET for the lamp, plus the on-board LED | 26 (lamp), 2 (LED) |
| SceneRecall | 4 push buttons to GND. Tap to recall, hold 2 s to save | 13, 12, 14, 27 |

Notes:

- **Motor faders.** Drive the motor from `onState`: move it toward `getFader(ch)`. Only call `setFader` while the touch sense says a hand is on the fader, so the fader does not fight the console.
- **Mains lamps.** Switch them only with a relay module rated for mains, and keep mains wiring away from the board.
- **ADC2 pins.** On ESP32, ADC2 pins do not work while Wi-Fi is on, so use ADC1 pins (GPIO 32 to 39) for pots.

On boards other than ESP32 and ESP8266, pass your client: `WiFiClient net; AmFX1 console(net, "192.168.1.50");`. On AVR boards, `AMFX1_AUTO` uses HTTP polling. You can set `AMFX1_MAX_STRIPS` and `AMFX1_MAX_SCENES` before including the library, or with build flags, to trade channels for RAM. By default these are 8 strips and 2 scenes on AVR, and 32 strips and 8 scenes elsewhere.

## ESP-IDF

Add the component to your project. You can copy or symlink `sdk/iot/esp-idf/amfx1` into `components/`, or reference it with `EXTRA_COMPONENT_DIRS`. Its `CMakeLists.txt` compiles the shared core from `../../arduino/AmFX1/src`, so keep the `sdk/iot` tree together. If you publish the component on its own, copy the two core files into it.

The component uses lwIP BSD sockets and needs no extra component, so `esp_http_client` and `esp_websocket_client` are not used.

```c
#include "amfx1.h"

static void on_state(amfx1_client_t* c, void* user) { /* update LEDs from amfx1_get_mute(c, ch) */ }

void console_task(void* arg) {
  amfx1_client_t* c = amfx1_create("192.168.1.50", 8765);
  amfx1_on_state(c, on_state, NULL);
  char token[AMFX1_TOKEN_MAX + 1] = "";            /* read it from NVS (nvs_get_str) */
  if (!token[0] || amfx1_begin(c, token) != AMFX1_OK) {
    if (amfx1_pair(c, "123456", "ESP32 desk") == AMFX1_OK) { /* nvs_set_str(h, "token", amfx1_token(c)); nvs_commit(h); */ }
    amfx1_begin(c, NULL);
  }
  for (;;) {
    amfx1_loop(c, 20);                             /* reads, reconnects, fires callbacks */
    /* amfx1_set_fader(c, 1, -6.0f); amfx1_set_mute(c, 2, 1); amfx1_get_meter(c, 1); */
  }
}
```

Call every `amfx1_*` function from one task.

## Security

- **The token is the key to the console.** Store it in NVS (ESP32 `Preferences` or `nvs_set_str`) or in EEPROM. Never store it in a sketch, a repository or a log. The examples never print it. A paired token has the **control** role: it can move the mix, but it cannot manage tokens or see other devices. It lasts 30 days, it is bound to the address that first used it, and it can be revoked on the console. When the console disconnects a device, the device waits two minutes before it reconnects.
- **LAN only.** The service accepts connections only from private-network addresses, and only requests that name it by IPv4 address (the local-network policy). Do not forward port 8765 on your router.
- **No TLS.** Traffic between the board and the console is not encrypted, the same as for the phone remote on a LAN. Use the SDK on a network you trust, such as a separate production VLAN or Wi-Fi with WPA2/WPA3. If you need the link encrypted, put the TLS proxy from `deploy/nginx` in front of the service. The SDK itself speaks plain HTTP and WebSocket only.
- **Throttling.** A wrong token costs the client 100 ms and is counted by the service. After more than 20 wrong tokens a minute, the service answers 429.
- **Input checks.** Channels, dB, pan, PINs (exactly 6 digits) and tokens are checked on the device. Tokens and hosts with control characters are refused, so no header can be injected. A malformed or oversized answer from the network is reported as an error and never overruns a buffer.

## Tests

`ctest -R iot_sdk` runs `sdk/iot/test/amfx1_sdk_test.cpp`. It checks the builders, the JSON scanner (fed one byte at a time), the HTTP and WebSocket readers, and scenes. It then starts the real AmF X1 Network service in-process on the null audio backend and drives it with the ESP-IDF client code over POSIX sockets. The live run covers:

- pairing with the minute's PIN, and refusing a wrong one
- fader, mute, solo, pan and master over HTTP and over the WebSocket
- state and meter frames coming back
- a scene round trip
- refusing a wrong token
