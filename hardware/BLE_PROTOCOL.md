# BLE Sensor Protocol

How to connect to the device over Bluetooth Low Energy and decode the sensor data it streams. The firmware side of this contract is [`src/ble/ble_protocol.h`](src/ble/ble_protocol.h); UUIDs and timing live in `ble_config` in [`src/configs.h`](src/configs.h).

> The ESP32-C6 is **BLE only**. There is no Classic Bluetooth / serial port profile; clients must use a BLE (GATT) API.

## Connecting

1. **Scan** for the service UUID below (preferred), or for the device name `XIAO-Sensors`. The name is sent in the scan response, so passive scans may only see the UUID.
2. **Connect** and discover the service.
3. Optionally **read** the version characteristic and check it equals `1` before decoding.
4. **Subscribe** to notifications on the readings characteristic. One notification arrives per push, every `ble_config::kNotifyIntervalMs` (default **50 ms / 20 Hz**).

The device accepts a connection at any time. When a client disconnects it goes back to advertising automatically.

| Item | UUID | Properties | Value |
|---|---|---|---|
| Sensor service | `db118277-ac3c-4312-9c3f-8f0f77e70acc` | — | — |
| Readings characteristic | `4f2e8c83-317d-4a69-99bc-c9006d46e64e` | READ, NOTIFY | one 20-byte packet |
| Version characteristic | `48e4c40b-6e01-49f1-b99b-0e6e9bd77a16` | READ | 1 byte, packet format version |

A READ of the readings characteristic returns the most recently sent packet (all zeros before the first one).

## Packet format (version 1)

Every notification is exactly **20 bytes**: packed, no padding, **little-endian**. All of the IMU and sound data is in every packet. Values are sent as scaled whole numbers: **divide each field by its scale** to get the real value.

| Offset | Size | Type | Field | Scale | Real value | Unit |
|---|---|---|---|---|---|---|
| 0 | 4 | `uint32` | timestamp | — | as-is | ms since device boot |
| 4 | 2 | `int16` | accel X | 100 | raw / 100 | m/s² |
| 6 | 2 | `int16` | accel Y | 100 | raw / 100 | m/s² |
| 8 | 2 | `int16` | accel Z | 100 | raw / 100 | m/s² |
| 10 | 2 | `int16` | gyro X | 1000 | raw / 1000 | rad/s |
| 12 | 2 | `int16` | gyro Y | 1000 | raw / 1000 | rad/s |
| 14 | 2 | `int16` | gyro Z | 1000 | raw / 1000 | rad/s |
| 16 | 2 | `int16` | temperature | 100 | raw / 100 | °C (MPU6050 die temperature) |
| 18 | 2 | `uint16` | sound level | — | as-is | raw ADC, 0–4095 |

**Format string** (Python `struct`): `<I6hhH`

### Notes for decoders

- **Saturation:** values outside the `int16` range are clamped to `32767` / `-32768`, so they never wrap to the wrong sign. A field stuck at either limit means the reading was out of range. Accel covers ±327.67 m/s² (more than the sensor's ±16 g max). Gyro covers ±32.767 rad/s, which is enough for the default ±500 °/s range but saturates near the top of the ±2000 °/s range.
- **Timestamp** is the device's uptime when it built the packet. It resets on reboot and wraps after ~49.7 days. Use differences between packets for timing, not absolute time.
- **Rate:** consecutive packets may repeat a sample if `kNotifyIntervalMs` is set faster than the sensors sample (also 50 ms by default).
- **Versioning:** any change to this layout bumps the version characteristic. Reject versions you don't recognise rather than misreading the bytes.

### Worked example

Bytes received (hex):

```
40 E2 01 00  0C 00  FB FF  D5 03  0A 00  FD FF  00 00  7F 09  2A 07
```

| Field | Raw | Decoded |
|---|---|---|
| timestamp | `0x0001E240` = 123456 | 123.456 s since boot |
| accel X / Y / Z | 12 / -5 / 981 | 0.12 / -0.05 / 9.81 m/s² |
| gyro X / Y / Z | 10 / -3 / 0 | 0.010 / -0.003 / 0.000 rad/s |
| temperature | 2431 | 24.31 °C |
| sound level | 1834 | 1834 |

## Client examples

### Python ([bleak](https://github.com/hbldh/bleak))

```python
import asyncio, struct
from bleak import BleakScanner, BleakClient

SERVICE_UUID  = "db118277-ac3c-4312-9c3f-8f0f77e70acc"
READINGS_UUID = "4f2e8c83-317d-4a69-99bc-c9006d46e64e"
VERSION_UUID  = "48e4c40b-6e01-49f1-b99b-0e6e9bd77a16"

def decode(data: bytes) -> dict:
    t, ax, ay, az, gx, gy, gz, temp, sound = struct.unpack("<I6hhH", data)
    return {
        "timestamp_ms": t,
        "accel_ms2": (ax / 100, ay / 100, az / 100),
        "gyro_rads": (gx / 1000, gy / 1000, gz / 1000),
        "temp_c": temp / 100,
        "sound": sound,
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

function decode(view) {           // view: DataView over the 20 bytes
  const le = true;                // little-endian
  return {
    timestampMs: view.getUint32(0, le),
    accel: [view.getInt16(4, le) / 100, view.getInt16(6, le) / 100, view.getInt16(8, le) / 100],
    gyro:  [view.getInt16(10, le) / 1000, view.getInt16(12, le) / 1000, view.getInt16(14, le) / 1000],
    tempC: view.getInt16(16, le) / 100,
    sound: view.getUint16(18, le),
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

Install **nRF Connect** (Android/iOS/desktop), scan for `XIAO-Sensors`, connect, and tap the subscribe icon on the readings characteristic. Raw 20-byte hex values should arrive about 20 times per second. Decode them by hand with the table above.
