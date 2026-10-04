"""
BLE test client for the Arduino Nicla Voice sensor firmware.

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
import math
import struct

SERVICE_UUID = "db118277-ac3c-4312-9c3f-8f0f77e70acc"
READINGS_UUID = "4f2e8c83-317d-4a69-99bc-c9006d46e64e"
VERSION_UUID = "48e4c40b-6e01-49f1-b99b-0e6e9bd77a16"

SUPPORTED_VERSION = 1
# little-endian: uint32, 6x int16 (accel, gyro), int16 temp, uint16 sound,
# 3x int16 mag, uint16 battery mV, int8 battery %, uint8 flags
PACKET_FORMAT = "<I6hhH3hHbB"
PACKET_SIZE = struct.calcsize(PACKET_FORMAT)
# The first 20 bytes, all a client gets if the ATT MTU wasn't raised.
BASE_FORMAT = "<I6hhH"
BASE_SIZE = struct.calcsize(BASE_FORMAT)

ACCEL_SCALE = 100.0
GYRO_SCALE = 1000.0
TEMP_SCALE = 100.0
MAG_SCALE = 10.0
FLAG_ON_BATTERY = 1 << 0
FLAG_CHARGING = 1 << 1


def decode(data: bytes) -> dict:
    """Decode one sensor packet into real-world units.

    Accepts the full 30-byte packet, or the 20-byte prefix sent when the MTU
    wasn't negotiated (the result then has no mag/battery keys).
    """
    if len(data) not in (PACKET_SIZE, BASE_SIZE):
        raise ValueError(f"expected {PACKET_SIZE} (or {BASE_SIZE}) bytes, got {len(data)}")
    t, ax, ay, az, gx, gy, gz, temp, sound = struct.unpack_from(BASE_FORMAT, data)
    r = {
        "timestamp_ms": t,
        "accel_ms2": (ax / ACCEL_SCALE, ay / ACCEL_SCALE, az / ACCEL_SCALE),
        "gyro_rads": (gx / GYRO_SCALE, gy / GYRO_SCALE, gz / GYRO_SCALE),
        "temp_c": temp / TEMP_SCALE,
        "sound_rms": sound,
    }
    if len(data) == PACKET_SIZE:
        mx, my, mz, mv, pct, flags = struct.unpack_from(PACKET_FORMAT, data)[9:]
        r.update({
            "mag_ut": (mx / MAG_SCALE, my / MAG_SCALE, mz / MAG_SCALE),
            "battery_mv": mv,
            "battery_pct": pct,
            "on_battery": bool(flags & FLAG_ON_BATTERY),
            "charging": bool(flags & FLAG_CHARGING),
        })
    return r


def sound_dbfs(rms: int) -> float:
    """Convert the RMS sound level to dBFS (-inf for silence)."""
    return 20 * math.log10(rms / 32768) if rms > 0 else float("-inf")


def format_reading(r: dict) -> str:
    ax, ay, az = r["accel_ms2"]
    gx, gy, gz = r["gyro_rads"]
    line = (f"{r['timestamp_ms']:>10} ms | "
            f"accel [{ax:7.2f} {ay:7.2f} {az:7.2f}] m/s^2 | "
            f"gyro [{gx:7.3f} {gy:7.3f} {gz:7.3f}] rad/s | "
            f"{r['temp_c']:5.2f} C | sound {sound_dbfs(r['sound_rms']):6.1f} dBFS")
    if "mag_ut" in r:
        mx, my, mz = r["mag_ut"]
        power = "charging" if r["charging"] else "battery" if r["on_battery"] else "USB"
        line += (f" | mag [{mx:7.1f} {my:7.1f} {mz:7.1f}] uT | "
                 f"{r['battery_mv']} mV {r['battery_pct']}% {power}")
    return line


def selftest() -> None:
    """Decode the worked example from BLE_PROTOCOL.md and check every field."""
    example = bytes.fromhex("40E201000C00FBFFD5030A00FDFF00007F092A07FD00D7FF70FE480F5D01")
    assert PACKET_SIZE == 30, PACKET_SIZE
    r = decode(example)
    assert r["timestamp_ms"] == 123456, r
    assert r["accel_ms2"] == (0.12, -0.05, 9.81), r
    assert r["gyro_rads"] == (0.010, -0.003, 0.0), r
    assert r["temp_c"] == 24.31, r
    assert r["sound_rms"] == 1834, r
    assert round(sound_dbfs(r["sound_rms"]), 1) == -25.0, sound_dbfs(r["sound_rms"])
    assert r["mag_ut"] == (25.3, -4.1, -40.0), r
    assert r["battery_mv"] == 3912 and r["battery_pct"] == 93, r
    assert r["on_battery"] and not r["charging"], r

    # A truncated (MTU not raised) packet still decodes the original fields.
    truncated = decode(example[:BASE_SIZE])
    assert "mag_ut" not in truncated and truncated["accel_ms2"] == r["accel_ms2"], truncated

    print("selftest passed:")
    print(format_reading(r))


async def stream(seconds: float) -> None:
    from bleak import BleakClient, BleakScanner

    print(f"Scanning for service {SERVICE_UUID} ...")
    device = await BleakScanner.find_device_by_filter(
        lambda d, adv: SERVICE_UUID.lower() in [u.lower() for u in adv.service_uuids],
        timeout=10.0)
    if device is None:
        raise SystemExit("Device not found. Is it powered and advertising (green idle blip)?")

    print(f"Connecting to {device.name or '?'} ({device.address}) ...")
    async with BleakClient(device) as client:
        version = (await client.read_gatt_char(VERSION_UUID))[0]
        if version != SUPPORTED_VERSION:
            raise SystemExit(f"Unsupported packet version {version}, this client decodes v{SUPPORTED_VERSION}.")
        print(f"Connected, packet version {version}, MTU {client.mtu_size}. Streaming ...")

        count = 0
        warned_truncated = False

        def on_notify(_, data: bytearray) -> None:
            nonlocal count, warned_truncated
            count += 1
            if len(data) == BASE_SIZE and not warned_truncated:
                print(f"! Receiving {BASE_SIZE}-byte packets: MTU too small, mag/battery fields are cut off.")
                warned_truncated = True
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
