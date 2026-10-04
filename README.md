# Sending Stone

### A discreet wearable concept for capturing context during a safety incident

Sending Stone is a hackathon prototype exploring how a small wearable and companion app might help someone preserve audio and motion context when they feel unsafe. A wearable records around a trigger, then transfers the event to a nearby phone or computer over Bluetooth Low Energy (BLE).

> **Important:** Sending Stone is an experimental prototype, not a validated domestic violence detector, emergency alert service, or substitute for a safety plan or emergency services. It may miss events or trigger accidentally, and it cannot guarantee that anyone will receive or respond to an alert. Audio recording can create serious privacy and legal risks; use only in a safe, controlled setting with informed consent and in accordance with local law.

## The idea

In a crisis, a person may not have time or ability to open an app and document what is happening. Sending Stone explores a different interaction: keep a discreet wearable ready, detect a configured trigger, and preserve a short window of context around it. The prototype emphasizes local event capture and a store-and-forward BLE workflow, so recorded events can remain on the device until a companion client connects.

## Demo flow

1. Flash the firmware to an Arduino Nicla Voice and power the board.
2. Open the companion app's Watch page or run the Python BLE client and connect to the device.
3. Send a manual trigger from the client, or demonstrate the configured motion-jolt trigger. The keyword trigger currently uses the board's stock “Alexa” model as a stand-in.
4. Observe the immediate event-started notification. The device records the event and makes it available to the client when complete.
5. Download the event and inspect its audio, motion data, metadata, and raw binary file.

For a quick software demo without the wearable, the BLE client's decoder can be checked with `python ble_client.py --selftest` from `hardware-ble-test/`.

## What works in this prototype

- Nicla Voice firmware with rolling audio and motion capture.
- Triggered events with about 10 seconds of pre-trigger context and about 50 seconds of post-trigger recording; additional triggers can extend an event.
- BLE event notifications and resumable, chunked event downloads.
- A Python BLE client that saves events as `audio.wav`, `motion.csv`, `meta.json`, and `event.bin`.
- A SvelteKit/Tauri companion app with a Watch connection and event viewer, plus early safety-plan, contacts, and privacy screens.
- 3D enclosure models in `models/`.

## What is still exploratory

The trigger logic has not been evaluated as a domestic violence detector. The current keyword is a stock-model stand-in, motion thresholds are configurable starting points, and everyday movement or noisy settings may cause false triggers. There is no integrated raised-voice detector, custom keyword model, or validated incident classifier. 

## System overview

```text
Nicla Voice wearable
  microphone + IMU → rolling buffer → trigger → stored event
                                               │
                                      Bluetooth Low Energy
                                               ↓
                   Companion app or Python client → event files
```

The firmware records 16 kHz audio and 50 Hz motion data to external flash. A triggered event is retained on the wearable until a client retrieves and deletes it. The BLE protocol supports listing stored events, downloading from an offset, and resuming an interrupted transfer.

## Built with

- **Wearable:** Arduino Nicla Voice, Arduino/C++, Syntiant NDP120, BMI270 IMU, external flash, OnShape CAD, Ultimaker CURA, Ender 3 3D
- **Companion app:** SvelteKit, Svelte, TypeScript, Tauri 2
- **Device communication:** Bluetooth Low Energy (GATT)
- **Development client:** Python and Bleak
- **Enclosure:** STL models

## Run the companion app

### Requirements

- Node.js and npm
- For native desktop/mobile builds: Rust and the platform prerequisites for Tauri 2
- For Android builds: Android SDK/NDK and Rust Android targets; see the [Tauri Android prerequisites](https://v2.tauri.app/start/prerequisites/#android)

The root chat screen calls Gemini through the SvelteKit `/api/chat` server route. Create a Gemini API key in [Google AI Studio](https://aistudio.google.com/app/apikey), copy `.env.example` to `.env`, and set `GEMINI_API_KEY`. Keep the key on the server and out of client-side code. The API route requires a server runtime; it works with the SvelteKit dev server, but a static Tauri build does not include server routes. For a deployed or native app, point the chat request at a separately hosted API service before shipping.

```bash
npm install
npm run dev
```

Open the local URL printed by Vite. Browser BLE requires a compatible browser, such as Chrome or Edge. Native app builds use the Tauri BLE plugin.

```bash
npm run check       # Svelte and TypeScript diagnostics
npm run build       # production web build
npm run tauri       # Tauri CLI
npm run tauri android build
```

## Build and flash the wearable

The firmware targets the Arduino Nicla Voice (ABX00061). See [`hardware/README.md`](hardware/README.md) for the pinned Arduino core and library versions, setup, recovery, and NDP120 firmware provisioning. From the `hardware/` directory:

```bash
arduino-cli compile --profile nicla_voice
arduino-cli board list
arduino-cli upload --profile nicla_voice -p <PORT>
```

## Run the Python BLE client

From `hardware-ble-test/`:

```bash
python -m pip install -r requirements.txt
python ble_client.py
```

Use `python ble_client.py --trigger` to send a manual test trigger, or `python ble_client.py --selftest` to exercise the event decoder without a device. See [`hardware/BLE_PROTOCOL.md`](hardware/BLE_PROTOCOL.md) for the BLE service and event format.

## Repository map

- `src/` — companion app, watch connection, event storage, and prototype screens
- `src-tauri/` — Tauri desktop/mobile shell
- `hardware/` — Nicla Voice firmware and BLE protocol documentation
- `hardware-ble-test/` — Python BLE client and event decoder
- `audioProcessing/` — standalone audio/text model experiments
- `models/` — enclosure STL files

## Next steps

- Evaluate trigger reliability and false positives using consent-based, representative data.
- Explore custom on-device trigger models and configurable detection behavior.
- Improve safe, private event handling and make data retention and sharing choices clear to users.
- Validate the companion app workflows and device connection across target platforms.

## Privacy and Responsible Testing Notice

**Please note:** this prototype is always recording audio (when powered on) and storing event files on the wearable and client. Any downloaded recordings are to be privated and or deleted. Any stored recordings after hackathon judging will be deleted. Audio recording can cause major security risks; do not use Sending Stone to record people without appropriate consent. 

