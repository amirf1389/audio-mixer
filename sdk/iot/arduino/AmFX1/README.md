# AmFX1 (Arduino library)

This library lets a microcontroller control the AmF X1 mixing console over Wi-Fi or Ethernet. It can move faders, mute, solo, pan and the master, read the meters and recall scenes. You can use it to build pot or motor-fader controllers, mute buttons with LEDs, rotary encoders, LED meters and tally lights.

- It needs no other library. The JSON, HTTP and WebSocket code is part of the library and does not allocate memory.
- It works with any Arduino `Client`. On ESP32 and ESP8266 the library makes its own `WiFiClient`. On WiFiNINA, WiFi101 and Ethernet boards, pass your own client: `AmFX1 console(client, "192.168.1.50")`.
- 32-bit boards use a WebSocket. It sends live state and meters 20 times a second and carries your moves back on the same connection. On AVR boards, or when the WebSocket fails, the library uses HTTP polling instead.

```cpp
#include <WiFi.h>
#include <AmFX1.h>

AmFX1 console("192.168.1.50");          // the console's IPv4 address, port 8765

void setup() {
  WiFi.begin("your-network", "your-password");
  while (WiFi.status() != WL_CONNECTED) delay(200);
  if (!console.begin(savedToken))       // a token from an earlier pairing (see below)
    console.pair("123456", "Desk");     // or the 6-digit PIN the console shows
}

void loop() {
  console.loop();
  console.setFader(1, -6.0);            // CH 1 to -6 dB
}
```

## API

| Call | What it does |
|---|---|
| `begin(token)` / `pair(pin, name)` / `token()` | Connect with a stored token, or trade the console's PIN for a new token |
| `setFader(ch, db)`, `setMute(ch, on)`, `setSolo(ch, on)`, `setPan(ch, -1..1)` | Control one channel. Channels start at 1. dB is clamped to -120 .. +10 |
| `setMaster(db)`, `setMasterMute(on)` | Control the master |
| `getFader/getMute/getSolo/getPan(ch)`, `channelName(ch)`, `channels()` | Read the console's state as last received |
| `getMeter(ch)`, `getMeterRms(ch)`, `getMasterMeter()` | Read the meters in dBFS (-120 is silence) |
| `saveScene(n)`, `recallScene(n)`, `sceneData(n)` | Scenes kept on the board. `sceneData(n)` returns the bytes so you can store them in NVS or EEPROM |
| `onState(cb)`, `onMeter(cb)`, `onError(cb)` | Callbacks that run inside `loop()` |
| `send(json)` | Send any other console command, for example `{"op":"busLevel","bus":0,"db":-6}` |
| `setTransport(AMFX1_AUTO / AMFX1_WEBSOCKET / AMFX1_HTTP)`, `setPollInterval(stateMs, meterMs)` | Choose how the board talks to the console |

## Examples

The examples are BasicFader, MuteButtons, EncoderPan, VUMeterLEDs, TallyLight and SceneRecall. Each one pairs on first start and stores the token in ESP32 NVS with `Preferences`.

## Security

- The token is the key to the console. Keep it in NVS or EEPROM and never put it in a sketch.
- A paired token lasts 30 days and works only from the address that first used it. You can revoke it on the console.
- The console accepts connections only from private-network addresses. The traffic is not encrypted, so use the library only on a network you trust.

See `docs/iot-sdk.md` in the AmF X1 repository for wiring and details.

MIT License.
