# BLE Event Protocol (v2)

How a client connects to the Nicla Voice safety recorder over Bluetooth Low Energy, learns about triggered events, and downloads them. The firmware side of this contract is [`src/ble/ble_protocol.h`](src/ble/ble_protocol.h) (commands and messages) and [`src/recorder/event_format.h`](src/recorder/event_format.h) (an event's bytes). A complete working client is [`../hardware-ble-test/ble_client.py`](../hardware-ble-test/ble_client.py).

> The Nicla Voice is **BLE only**. There is no Classic Bluetooth / serial port profile; clients must use a BLE (GATT) API.

## How it works

The device records audio and motion continuously into a ~10 s rolling buffer. Nothing is sent until a **trigger** (a keyword, a jolt of movement, or the client's TRIGGER command). Then:

1. The device notifies **EVENT_STARTED** immediately, so the client can alert right away.
2. It keeps recording for 50 s after the trigger. Further triggers extend that, up to 3 minutes in total.
3. It notifies **EVENT_READY**, and the event (the ~10 s before plus everything after) is stored on the device's flash until the client fetches and deletes it. Up to 16 events are kept. The oldest are deleted when there are more, or when free space drops below what one full-length event needs (~2.4 MB), so the device can always record.

Events wait on the device while no client is connected, so on every connect a client should send **LIST** and fetch anything pending.

## GATT

| Item | UUID | Properties | Purpose |
|---|---|---|---|
| Service | `db118277-ac3c-4312-9c3f-8f0f77e70acc` | — | advertised, filter scans by it |
| Version | `48e4c40b-6e01-49f1-b99b-0e6e9bd77a16` | READ | 1 byte, protocol version (`2`) |
| Control | `5eb136f7-3b60-4ea6-a592-3b9778aed258` | WRITE | commands from the client |
| Events | `6c33adb5-05dd-4186-bc22-624ed92abb28` | READ, NOTIFY | messages from the device |
| Data | `40d3f957-dded-4b7d-9eb2-f11db97dda09` | NOTIFY | event bytes during a transfer |

Device name: `Nicla-Sensors`. Subscribe to **Events** and **Data** before sending commands. Everything below is little-endian.

### Commands (write to Control)

| Byte 0 | Command | Arguments | Reply |
|---|---|---|---|
| `0x01` | LIST | — | one EVENT_INFO per stored event, then LIST_END |
| `0x02` | GET | u16 id, u32 offset, u16 max payload per chunk | data chunks from `offset` to the end, then TRANSFER_DONE |
| `0x03` | DELETE | u16 id | DELETED, or ERROR |
| `0x04` | CANCEL | — | stops the current transfer, no reply |
| `0x05` | TRIGGER | — | starts (or extends) an event, like any trigger |

### Messages (notified on Events)

| Byte 0 | Message | Fields |
|---|---|---|
| `0x81` | EVENT_STARTED | u16 id, u8 trigger type, u8 detail, u32 trigger time (ms since device boot) |
| `0x82` | EVENT_READY | u16 id, u32 size, u8 trigger type |
| `0x83` | EVENT_INFO | u16 id, u32 size (0 while recording), u8 trigger type, u8 complete |
| `0x84` | LIST_END | u16 number of events |
| `0x85` | TRANSFER_DONE | u16 id, u32 total size |
| `0x86` | DELETED | u16 id |
| `0x87` | ERROR | u8 code (1 bad command, 2 no such event / still recording, 3 storage error), u16 id |

Trigger types: `1` keyword (detail = the model's class index), `2` jolt (detail 1 = acceleration, 2 = rotation), `3` manual.

### Data chunks (notified on Data)

Each notification is `u16 event id, u32 offset` followed by the bytes at that offset of the event's byte stream. Set GET's *max payload* to your ATT MTU − 3 − 6 (the device caps it at 234). With the default MTU of 23 that's 14 bytes per chunk, which works but is slow, so negotiate a large MTU (bleak, Chrome, Android and iOS do automatically).

**Downloading reliably:** write each chunk at its offset and track the next expected offset. If chunks stop arriving before TRANSFER_DONE, or one is missing, send GET again from the expected offset; the transfer resumes there. After saving the event, send DELETE.

## Event byte stream

An event is a 64-byte **header** followed by its **records**.

### Header (64 bytes)

| Offset | Type | Field |
|---|---|---|
| 0 | char[4] | magic `NVEV` |
| 4 | u8 | format version (`1`) |
| 5 | u8 | trigger type of the first trigger |
| 6 | u8 | trigger detail |
| 7 | u8 | complete (1 = finished normally) |
| 8 | u16 | event id |
| 10 | u16 | segment count (internal) |
| 12 | u32 | first trigger time, ms since boot |
| 16 | u32 | last trigger time, ms |
| 20 | u32 | end time, ms (0 if the device lost power mid-event) |
| 24 | u32 | bytes of records that follow |
| 28 | f32 | first trigger strength: peak g (jolt by acceleration), peak dps (by rotation), else 0 |
| 32 | u16 | audio sample rate (16000) |
| 34 | u16 | motion sample rate (50) |
| 36 | u16 | accelerometer range, g |
| 38 | u16 | gyroscope range, dps |
| 40 | u16 | number of triggers in the event |
| 42 | char[18] | label: keyword (e.g. `NN0:alexa`), `jolt-accel`, `jolt-rotation`, or `manual` |
| 60 | u8[4] | reserved |

### Records

Every record is an 8-byte header — `u8 type, u8 info, u16 payload bytes, u32 timestamp ms` — then its payload. Records appear in time order; skip unknown types using the payload length.

**Audio (type 1)** — `info` = audio chunks the device missed just before this one (insert that many chunks of silence, each the same length as this one, to keep timing). Payload: `i16 predictor, u8 step index, u8 reserved, u16 sample count`, then `(count + 1) / 2` bytes of **IMA-ADPCM**, two 4-bit codes per byte, low nibble first. Decode with the standard IMA-ADPCM algorithm starting from the given predictor and step index. Output is 16-bit PCM at the header's sample rate.

**Motion (type 2)** — `info` = sample count. Payload: that many 14-byte samples of `u16 ms after the record timestamp, i16 accel x/y/z, i16 gyro x/y/z`, in raw counts: `g = counts × accel range / 32768`, `dps = counts × gyro range / 32768`.

**Mark (type 3)** — a trigger happened at the record's timestamp. `info` = trigger type. Payload: `u8 detail, u8[3] reserved, f32 strength`.

## Python client

```bash
cd ../hardware-ble-test
pip install -r requirements.txt
python ble_client.py              # listen forever; saves each event to output/<timestamp>_event<id>_<trigger>/
python ble_client.py --trigger    # also send one manual trigger after connecting (for testing)
python ble_client.py --selftest   # check the decoder without a device
```

Each saved event folder has `audio.wav`, `motion.csv` (with time relative to the trigger), `meta.json` (header fields, timing, marks) and `event.bin` (the raw bytes).
