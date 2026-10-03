"""
BLE test client for the XIAO ESP32-C6 sensor firmware.

Connects to the device, checks the packet version, subscribes to the readings
characteristic and prints each decoded packet. The packet format is documented
in ../hardware/BLE_PROTOCOL.md.

Usage:
    python ble_client.py              # scan, connect, stream for 60 s
    python ble_client.py --seconds 0  # stream until Ctrl+C
    python ble_client.py --selftest   # check the decoder against the documented example, no BLE needed
"""

import argparse
import asyncio
import struct

SERVICE_UUID = "db118277-ac3c-4312-9c3f-8f0f77e70acc"
READINGS_UUID = "4f2e8c83-317d-4a69-99bc-c9006d46e64e"
VERSION_UUID = "48e4c40b-6e01-49f1-b99b-0e6e9bd77a16"

SUPPORTED_VERSION = 1
PACKET_FORMAT = "<I6hhH"  # little-endian: uint32, 6x int16, int16, uint16
PACKET_SIZE = struct.calcsize(PACKET_FORMAT)

ACCEL_SCALE = 100.0
GYRO_SCALE = 1000.0
TEMP_SCALE = 100.0


def decode(data: bytes) -> dict:
    """Decode one 20-byte sensor packet into real-world units."""
    if len(data) != PACKET_SIZE:
        raise ValueError(f"expected {PACKET_SIZE} bytes, got {len(data)}")
    t, ax, ay, az, gx, gy, gz, temp, sound = struct.unpack(PACKET_FORMAT, data)
    return {
        "timestamp_ms": t,
        "accel_ms2": (ax / ACCEL_SCALE, ay / ACCEL_SCALE, az / ACCEL_SCALE),
        "gyro_rads": (gx / GYRO_SCALE, gy / GYRO_SCALE, gz / GYRO_SCALE),
        "temp_c": temp / TEMP_SCALE,
        "sound": sound,
    }


def format_reading(r: dict) -> str:
    ax, ay, az = r["accel_ms2"]
    gx, gy, gz = r["gyro_rads"]
    return (f"{r['timestamp_ms']:>10} ms | "
            f"accel [{ax:7.2f} {ay:7.2f} {az:7.2f}] m/s^2 | "
            f"gyro [{gx:7.3f} {gy:7.3f} {gz:7.3f}] rad/s | "
            f"{r['temp_c']:5.2f} C | sound {r['sound']:4}")


def selftest() -> None:
    """Decode the worked example from BLE_PROTOCOL.md and check every field."""
    example = bytes.fromhex("40E201000C00FBFFD5030A00FDFF00007F092A07")
    r = decode(example)
    assert PACKET_SIZE == 20, PACKET_SIZE
    assert r["timestamp_ms"] == 123456, r
    assert r["accel_ms2"] == (0.12, -0.05, 9.81), r
    assert r["gyro_rads"] == (0.010, -0.003, 0.0), r
    assert r["temp_c"] == 24.31, r
    assert r["sound"] == 1834, r
    print("selftest passed:")
    print(format_reading(r))


async def stream(seconds: float) -> None:
    from bleak import BleakClient, BleakScanner

    print(f"Scanning for service {SERVICE_UUID} ...")
    device = await BleakScanner.find_device_by_filter(
        lambda d, adv: SERVICE_UUID.lower() in [u.lower() for u in adv.service_uuids],
        timeout=10.0)
    if device is None:
        raise SystemExit("Device not found. Is it powered and advertising (LED idle blink)?")

    print(f"Connecting to {device.name or '?'} ({device.address}) ...")
    async with BleakClient(device) as client:
        version = (await client.read_gatt_char(VERSION_UUID))[0]
        if version != SUPPORTED_VERSION:
            raise SystemExit(f"Unsupported packet version {version}, this client decodes v{SUPPORTED_VERSION}.")
        print(f"Connected, packet version {version}. Streaming ...")

        count = 0

        def on_notify(_, data: bytearray) -> None:
            nonlocal count
            count += 1
            print(format_reading(decode(bytes(data))))

        await client.start_notify(READINGS_UUID, on_notify)
        try:
            if seconds > 0:
                await asyncio.sleep(seconds)
            else:
                while True:
                    await asyncio.sleep(1)
        finally:
            await client.stop_notify(READINGS_UUID)
            print(f"Received {count} packets.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--selftest", action="store_true", help="verify the decoder offline and exit")
    parser.add_argument("--seconds", type=float, default=60, help="how long to stream, 0 = until Ctrl+C")
    args = parser.parse_args()

    if args.selftest:
        selftest()
        return
    try:
        asyncio.run(stream(args.seconds))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
