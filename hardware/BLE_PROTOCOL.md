# BLE Sensor Protocol

How to connect to the Arduino Nicla Voice over Bluetooth Low Energy and decode the sensor data it streams. The firmware side of this contract is [`src/ble/ble_protocol.h`](src/ble/ble_protocol.h); UUIDs and timing live in `ble_config` in [`src/configs.h`](src/configs.h).

> The Nicla Voice is **BLE only**. There is no Classic Bluetooth / serial port profile; clients must use a BLE (GATT) API.

## Connecting

1. **Scan** for the service UUID below (preferred), or for the device name `Nicla-Sensors`.
2. **Connect**, and **request an ATT MTU of at least 33** (most stacks do this automatically, see [Packet size](#packet-size-and-mtu)).
3. Optionally **read** the version characteristic and check it equals `1` before decoding.
4. **Subscribe** to notifications on the readings characteristic. One notification arrives per push, every `ble_config::kNotifyIntervalMs` (default **50 ms / 20 Hz**).

The device accepts a connection at any time. When a client disconnects it goes back to advertising automatically.

| Item | UUID | Properties | Value |
|---|---|---|---|
| Sensor service | `db118277-ac3c-4312-9c3f-8f0f77e70acc` | — | — |
| Readings characteristic | `4f2e8c83-317d-4a69-99bc-c9006d46e64e` | READ, NOTIFY | one 30-byte packet |
| Version characteristic | `48e4c40b-6e01-49f1-b99b-0e6e9bd77a16` | READ | 1 byte, packet format version |

A READ of the readings characteristic returns the most recently sent packet (all zeros before the first one).

## Packet format (version 1)

Every notification is exactly **30 bytes**: packed, no padding, **little-endian**. Every onboard sensor is in every packet. Scaled values are sent as whole numbers: **divide each field by its scale** to get the real value.

| Offset | Size | Type | Field | Scale | Real value | Unit | Source |
|---|---|---|---|---|---|---|---|
| 0 | 4 | `uint32` | timestamp | — | as-is | ms since device boot | — |
| 4 | 2 | `int16` | accel X | 100 | raw / 100 | m/s² | BMI270 |
| 6 | 2 | `int16` | accel Y | 100 | raw / 100 | m/s² | BMI270 |
| 8 | 2 | `int16` | accel Z | 100 | raw / 100 | m/s² | BMI270 |
| 10 | 2 | `int16` | gyro X | 1000 | raw / 1000 | rad/s | BMI270 |
| 12 | 2 | `int16` | gyro Y | 1000 | raw / 1000 | rad/s | BMI270 |
| 14 | 2 | `int16` | gyro Z | 1000 | raw / 1000 | rad/s | BMI270 |
| 16 | 2 | `int16` | temperature | 100 | raw / 100 | °C (IMU die temperature) | BMI270 |
| 18 | 2 | `uint16` | sound level | — | as-is | RMS amplitude of 16-bit PCM, 0–32767 | IM69D130 mic |
| 20 | 2 | `int16` | mag X | 10 | raw / 10 | µT | BMM150 |
| 22 | 2 | `int16` | mag Y | 10 | raw / 10 | µT | BMM150 |
| 24 | 2 | `int16` | mag Z | 10 | raw / 10 | µT | BMM150 |
| 26 | 2 | `uint16` | battery voltage | — | as-is | mV, 0 = unknown | BQ25120A PMIC |
| 28 | 1 | `int8` | battery percent | — | as-is | % of full-charge voltage, -1 = unknown | BQ25120A PMIC |
| 29 | 1 | `uint8` | flags | — | bitfield | bit 0: on battery, bit 1: charging | BQ25120A PMIC |

**Format string** (Python `struct`): `<I6hhH3hHbB`

### Packet size and MTU

BLE's default ATT MTU of 23 bytes only fits a 20-byte notification. The firmware sends the full packet in **one** notification and relies on the client negotiating a larger MTU (≥ 33):

- **bleak (Python)**, **Chrome/Edge Web Bluetooth**, **Android** and **iOS** negotiate a large MTU automatically on connect.
- On Android you can force it with `requestMtu(247)` if needed.

If the MTU is not raised, the device truncates notifications to the **first 20 bytes**. Those 20 bytes keep exactly the layout above (bytes 20–29 were appended after), so a truncated packet still decodes the IMU, temperature, and sound fields correctly. Check the received length to tell the two apart.

### Notes for decoders

- **Saturation:** `int16` fields that go out of range are clamped to `32767` / `-32768` instead of wrapping to the wrong sign. A field stuck at either limit means the reading was out of range. Accel covers ±327.67 m/s² (beyond the sensor's ±16 g). Gyro covers ±32.767 rad/s, which is enough for the default ±1000 °/s but saturates near the top of ±2000 °/s. Mag covers ±3276.7 µT, beyond the BMM150's ±2500 µT.
- **Sound level** is a loudness measure, not audio: the RMS of one ~16 ms chunk of 16 kHz audio. To convert it to dBFS: `20 * log10(level / 32768)` (0 means silence, use -inf).
- **Battery percent** is the battery voltage as a percentage of the PMIC's regulated full-charge voltage (60–100), **not** state of charge. For a 4.2 V LiPo, below ~84% is effectively empty. With no battery connected (flags bit 0 clear), the battery fields are not meaningful.
- **Timestamp** is the device's uptime when it built the packet. It resets on reboot and wraps after ~49.7 days. Use differences between packets for timing, not absolute time.
- **Update rates:** IMU and magnetometer sample at 20 Hz, sound at 20 Hz, battery every 5 s. Every packet carries the latest value of each, so slower fields repeat between updates.
- **Versioning:** any change to existing byte offsets bumps the version characteristic. Reject versions you don't recognise rather than misreading the bytes.

### Worked example

Bytes received (hex):

```text
40 E2 01 00  0C 00  FB FF  D5 03  0A 00  FD FF  00 00  7F 09  2A 07  FD 00  D7 FF  70 FE  48 0F  5D  01
```

| Field | Raw | Decoded |
|---|---|---|
| timestamp | `0x0001E240` = 123456 | 123.456 s since boot |
| accel X / Y / Z | 12 / -5 / 981 | 0.12 / -0.05 / 9.81 m/s² |
| gyro X / Y / Z | 10 / -3 / 0 | 0.010 / -0.003 / 0.000 rad/s |
| temperature | 2431 | 24.31 °C |
| sound level | 1834 | RMS 1834 ≈ -25.0 dBFS |
| mag X / Y / Z | 253 / -41 / -400 | 25.3 / -4.1 / -40.0 µT |
| battery voltage | 3912 | 3.912 V |
| battery percent | 93 | 93% of full-charge voltage |
| flags | `0x01` | on battery, not charging |

## Client examples

A ready-to-run Python test client lives in [`../hardware-ble-test/`](../hardware-ble-test/).

### Python ([bleak](https://github.com/hbldh/bleak))

```python
import asyncio, struct
from bleak import BleakScanner, BleakClient

SERVICE_UUID  = "db118277-ac3c-4312-9c3f-8f0f77e70acc"
READINGS_UUID = "4f2e8c83-317d-4a69-99bc-c9006d46e64e"
VERSION_UUID  = "48e4c40b-6e01-49f1-b99b-0e6e9bd77a16"

def decode(data: bytes) -> dict:
    t, ax, ay, az, gx, gy, gz, temp, sound, mx, my, mz, mv, pct, flags = struct.unpack("<I6hhH3hHbB", data)
    return {
        "timestamp_ms": t,
        "accel_ms2": (ax / 100, ay / 100, az / 100),
        "gyro_rads": (gx / 1000, gy / 1000, gz / 1000),
        "temp_c": temp / 100,
        "sound_rms": sound,
        "mag_ut": (mx / 10, my / 10, mz / 10),
        "battery_mv": mv,
        "battery_pct": pct,
        "on_battery": bool(flags & 1),
        "charging": bool(flags & 2),
    }

async def main():
    device = await BleakScanner.find_device_by_filter(
        lambda d, adv: SERVICE_UUID in adv.service_uuids)
    async with BleakClient(device) as client:
        version = (await client.read_gatt_char(VERSION_UUID))[0]
        assert version == 1, f"unsupported packet version {version}"
        await client.start_notify(READINGS_UUID, lambda _, data: print(decode(data)))
        await asyncio.sleep(60)  # stream for 60 s

asyncio.run(main())
```

### JavaScript ([Web Bluetooth](https://developer.mozilla.org/docs/Web/API/Web_Bluetooth_API), Chrome/Edge)

```js
const SERVICE_UUID  = "db118277-ac3c-4312-9c3f-8f0f77e70acc";
const READINGS_UUID = "4f2e8c83-317d-4a69-99bc-c9006d46e64e";

function decode(view) {           // view: DataView over the 30 bytes
  const le = true;                // little-endian
  const flags = view.getUint8(29);
  return {
    timestampMs: view.getUint32(0, le),
    accel: [view.getInt16(4, le) / 100, view.getInt16(6, le) / 100, view.getInt16(8, le) / 100],
    gyro:  [view.getInt16(10, le) / 1000, view.getInt16(12, le) / 1000, view.getInt16(14, le) / 1000],
    tempC: view.getInt16(16, le) / 100,
    soundRms: view.getUint16(18, le),
    magUt: [view.getInt16(20, le) / 10, view.getInt16(22, le) / 10, view.getInt16(24, le) / 10],
    batteryMv: view.getUint16(26, le),
    batteryPct: view.getInt8(28),
    onBattery: (flags & 1) !== 0,
    charging: (flags & 2) !== 0,
  };
}

// Must be called from a user gesture, e.g. a button click.
async function connect() {
  const device = await navigator.bluetooth.requestDevice({ filters: [{ services: [SERVICE_UUID] }] });
  const server = await device.gatt.connect();
  const service = await server.getPrimaryService(SERVICE_UUID);
  const readings = await service.getCharacteristic(READINGS_UUID);
  readings.addEventListener("characteristicvaluechanged", e => console.log(decode(e.target.value)));
  await readings.startNotifications();
}
```

## Testing without writing a client

Install **nRF Connect** (Android/iOS/desktop), scan for `Nicla-Sensors`, connect, and tap the subscribe icon on the readings characteristic. Raw 30-byte hex values should arrive about 20 times per second. Decode them by hand with the table above. If only 20 bytes arrive, request a larger MTU from nRF Connect's menu.
