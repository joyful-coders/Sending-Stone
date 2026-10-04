"""
BLE event client for the Arduino Nicla Voice safety recorder firmware.

Runs until Ctrl+C. It scans for the device, connects, and:
  - prints an alert the moment the device reports a triggered event,
  - downloads every finished event (the ~10 s before the trigger and the
    ~50 s after) into its own timestamped folder under ./output/ (named for
    when the trigger happened, e.g. output/20261004-153012_event00003_jolt/):
        audio.wav   16 kHz mono audio
        motion.csv  50 Hz acceleration (g) and rotation (dps)
        meta.json   trigger details, timing, and trigger marks
        event.bin   the raw bytes, exactly as sent
  - deletes each event from the device once it's saved.
If the device disconnects (out of range, rebooted), it goes back to scanning
and reconnects. The protocol and byte formats are documented in
../hardware/BLE_PROTOCOL.md.

Usage:
    python ble_client.py              # listen until Ctrl+C, saving to ./output/
    python ble_client.py --output D:ecordings   # save somewhere else
    python ble_client.py --trigger    # also trigger an event manually once connected (for testing)
    python ble_client.py --selftest   # check the decoder on a synthetic event, no BLE needed
"""

from __future__ import annotations

import argparse
import array
import asyncio
import csv
import json
import math
import struct
import wave
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# --- BLE service -------------------------------------------------------------

SERVICE_UUID = "db118277-ac3c-4312-9c3f-8f0f77e70acc"
VERSION_UUID = "48e4c40b-6e01-49f1-b99b-0e6e9bd77a16"
CONTROL_UUID = "5eb136f7-3b60-4ea6-a592-3b9778aed258"
EVENTS_UUID = "6c33adb5-05dd-4186-bc22-624ed92abb28"
DATA_UUID = "40d3f957-dded-4b7d-9eb2-f11db97dda09"
PROTOCOL_VERSION = 2

# Commands (client -> device, control characteristic)
CMD_LIST, CMD_GET, CMD_DELETE, CMD_CANCEL, CMD_TRIGGER = 0x01, 0x02, 0x03, 0x04, 0x05
# Messages (device -> client, events characteristic)
MSG_STARTED, MSG_READY, MSG_INFO, MSG_LIST_END, MSG_DONE, MSG_DELETED, MSG_ERROR = range(0x81, 0x88)
ERROR_NAMES = {1: "bad command", 2: "no such event", 3: "storage error"}

DATA_HEADER = struct.Struct("<HI")      # event id, offset
DEVICE_MAX_NOTIFY = 240                 # ble_config::kMaxDataNotifyBytes
RECONNECT_DELAY_S = 2.0
TRANSFER_STALL_S = 10.0                 # re-request if no data arrives for this long
OUTPUT_DIR = Path(__file__).resolve().parent / "output"

# --- Event byte format (src/recorder/event_format.h) -------------------------

TRIGGER_NAMES = {0: "none", 1: "keyword", 2: "jolt", 3: "manual"}
META = struct.Struct("<4sBBBBHHIIIIfHHHHH18s4s")
RECORD = struct.Struct("<BBHI")          # type, info, payload bytes, timestamp ms
AUDIO_HEADER = struct.Struct("<hBBH")    # predictor, step index, reserved, sample count
MOTION_SAMPLE = struct.Struct("<H6h")    # offset ms, accel xyz, gyro xyz (counts)
MARK = struct.Struct("<B3sf")            # detail, reserved, magnitude
REC_AUDIO, REC_MOTION, REC_MARK = 1, 2, 3
assert META.size == 64 and RECORD.size == 8 and AUDIO_HEADER.size == 6 and MOTION_SAMPLE.size == 14 and MARK.size == 8

# IMA-ADPCM tables (src/recorder/adpcm.h)
INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]
STEP_TABLE = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767]


def adpcm_decode(data: bytes, count: int, predictor: int, index: int) -> array.array:
    """Decode `count` IMA-ADPCM samples (two per byte, low nibble first)."""
    out = array.array("h")
    for i in range(count):
        code = (data[i >> 1] >> (4 * (i & 1))) & 0x0F
        step = STEP_TABLE[index]
        delta = step >> 3
        if code & 4:
            delta += step
        if code & 2:
            delta += step >> 1
        if code & 1:
            delta += step >> 2
        predictor = max(-32768, min(32767, predictor - delta if code & 8 else predictor + delta))
        index = max(0, min(88, index + INDEX_TABLE[code]))
        out.append(predictor)
    return out


def adpcm_encode(samples, predictor: int = 0, index: int = 0) -> Tuple[bytes, int, int]:
    """IMA-ADPCM encoder, identical to the firmware's. Used by the self-test."""
    codes = []
    for sample in samples:
        step = STEP_TABLE[index]
        diff = sample - predictor
        code = 0
        if diff < 0:
            code, diff = 8, -diff
        delta = step >> 3
        if diff >= step:
            code |= 4; diff -= step; delta += step
        step >>= 1
        if diff >= step:
            code |= 2; diff -= step; delta += step
        step >>= 1
        if diff >= step:
            code |= 1; delta += step
        predictor = max(-32768, min(32767, predictor - delta if code & 8 else predictor + delta))
        index = max(0, min(88, index + INDEX_TABLE[code]))
        codes.append(code)
    if len(codes) % 2:
        codes.append(0)
    packed = bytes(codes[i] | (codes[i + 1] << 4) for i in range(0, len(codes), 2))
    return packed, predictor, index


@dataclass
class Event:
    """A decoded event."""
    meta: Dict[str, object]
    audio: array.array                           # 16-bit PCM at meta["audio_rate_hz"]
    audio_start_ms: Optional[int]                # device time of the first audio sample
    motion: List[Tuple[int, float, float, float, float, float, float]] = field(default_factory=list)
    marks: List[Dict[str, object]] = field(default_factory=list)
    gaps: int = 0                                # audio chunks the device missed


def parse_meta(blob: bytes) -> Dict[str, object]:
    (magic, fmt, trig, detail, complete, event_id, segments, trig_ms, last_ms, end_ms, data_bytes,
     magnitude, audio_hz, motion_hz, accel_g, gyro_dps, trig_count, label, _) = META.unpack_from(blob, 0)
    if magic != b"NVEV":
        raise ValueError("not an event (bad magic)")
    if fmt != 1:
        raise ValueError(f"unsupported event format {fmt}")
    return {
        "event_id": event_id, "trigger": TRIGGER_NAMES.get(trig, str(trig)), "trigger_detail": detail,
        "label": label.split(b"\0", 1)[0].decode(errors="replace"), "trigger_magnitude": round(magnitude, 3),
        "trigger_count": trig_count, "trigger_time_ms": trig_ms, "last_trigger_time_ms": last_ms,
        "end_time_ms": end_ms, "complete": bool(complete), "segments": segments, "data_bytes": data_bytes,
        "audio_rate_hz": audio_hz, "motion_rate_hz": motion_hz, "accel_range_g": accel_g, "gyro_range_dps": gyro_dps,
    }


def parse_event(blob: bytes) -> Event:
    """Decode an event's byte stream: header, then audio/motion/mark records."""
    meta = parse_meta(blob)
    accel_scale = meta["accel_range_g"] / 32768.0
    gyro_scale = meta["gyro_range_dps"] / 32768.0
    event = Event(meta=meta, audio=array.array("h"), audio_start_ms=None)

    pos, end = META.size, min(len(blob), META.size + meta["data_bytes"])
    while pos + RECORD.size <= end:
        rtype, info, length, t_ms = RECORD.unpack_from(blob, pos)
        payload = blob[pos + RECORD.size: pos + RECORD.size + length]
        pos += RECORD.size + length
        if len(payload) < length:
            break  # truncated final record

        if rtype == REC_AUDIO:
            predictor, index, _, count = AUDIO_HEADER.unpack_from(payload, 0)
            if event.audio_start_ms is None:
                event.audio_start_ms = t_ms
            if info:  # chunks the device missed: keep timing by inserting silence
                event.gaps += info
                event.audio.extend(array.array("h", bytes(2 * info * count)))
            event.audio.extend(adpcm_decode(payload[AUDIO_HEADER.size:], count, predictor, index))
        elif rtype == REC_MOTION:
            for i in range(info):
                off, ax, ay, az, gx, gy, gz = MOTION_SAMPLE.unpack_from(payload, i * MOTION_SAMPLE.size)
                event.motion.append((t_ms + off, ax * accel_scale, ay * accel_scale, az * accel_scale,
                                     gx * gyro_scale, gy * gyro_scale, gz * gyro_scale))
        elif rtype == REC_MARK:
            detail, _, magnitude = MARK.unpack_from(payload, 0)
            event.marks.append({"time_ms": t_ms, "trigger": TRIGGER_NAMES.get(info, str(info)),
                                "detail": detail, "magnitude": round(magnitude, 3)})
    return event


def save_event(blob: bytes, root: Path = OUTPUT_DIR, triggered_at: Optional[datetime] = None) -> Path:
    """Decode an event into a new timestamped folder under `root`: audio.wav, motion.csv, meta.json, event.bin.

    The folder is named for `triggered_at` (when the trigger happened, if known), else the current time.
    """
    event = parse_event(blob)
    meta = event.meta
    stamp = triggered_at or datetime.now()
    folder = root / f"{stamp:%Y%m%d-%H%M%S}_event{meta['event_id']:05d}_{meta['trigger']}"
    suffix = 1
    while folder.exists():  # never overwrite an earlier save
        folder = folder.with_name(f"{folder.name.split('~')[0]}~{suffix}")
        suffix += 1
    folder.mkdir(parents=True)

    (folder / "event.bin").write_bytes(blob)
    with wave.open(str(folder / "audio.wav"), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(meta["audio_rate_hz"])
        wav.writeframes(event.audio.tobytes())

    trigger_ms = meta["trigger_time_ms"]
    with open(folder / "motion.csv", "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["t_ms", "t_from_trigger_s", "ax_g", "ay_g", "az_g", "gx_dps", "gy_dps", "gz_dps"])
        for t, ax, ay, az, gx, gy, gz in event.motion:
            writer.writerow([t, f"{(t - trigger_ms) / 1000:.3f}", f"{ax:.4f}", f"{ay:.4f}", f"{az:.4f}",
                             f"{gx:.2f}", f"{gy:.2f}", f"{gz:.2f}"])

    info = dict(meta)
    info.update({
        "saved_at": datetime.now().isoformat(timespec="seconds"),
        "triggered_at": triggered_at.isoformat(timespec="seconds") if triggered_at else None,
        "audio_seconds": round(len(event.audio) / meta["audio_rate_hz"], 2),
        "audio_start_ms": event.audio_start_ms,
        "audio_seconds_before_trigger": (round((trigger_ms - event.audio_start_ms) / 1000, 2)
                                         if event.audio_start_ms is not None else None),
        "audio_gaps_chunks": event.gaps,
        "motion_samples": len(event.motion),
        "marks": event.marks,
    })
    (folder / "meta.json").write_text(json.dumps(info, indent=2))
    return folder


# --- Self-test ---------------------------------------------------------------

def selftest() -> None:
    """Build an event the way the firmware does, then decode it and check every part."""
    rate, chunk = 16000, 256
    tone = [int(8000 * math.sin(2 * math.pi * 440 * i / rate)) for i in range(chunk * 8)]

    records = bytearray()
    predictor = index = 0
    for c in range(8):  # eight audio chunks, the fourth reporting one missed chunk before it
        pcm = tone[c * chunk:(c + 1) * chunk]
        header = AUDIO_HEADER.pack(predictor, index, 0, chunk)
        packed, predictor, index = adpcm_encode(pcm, predictor, index)
        payload = header + packed
        records += RECORD.pack(REC_AUDIO, 1 if c == 3 else 0, len(payload), 1000 + 16 * c) + payload
    motion = b"".join(MOTION_SAMPLE.pack(20 * i, 0, 0, 2048, 0, 0, 1638) for i in range(25))  # 1 g on Z, 100 dps on Z
    records += RECORD.pack(REC_MOTION, 25, len(motion), 1000) + motion
    records += RECORD.pack(REC_MARK, 2, MARK.size, 1100) + MARK.pack(1, b"\0\0\0", 5.5)

    label = b"jolt-accel".ljust(18, b"\0")
    meta = META.pack(b"NVEV", 1, 2, 1, 1, 7, 1, 1100, 1100, 2000, len(records), 5.5,
                     rate, 50, 16, 2000, 1, label, b"\0" * 4)
    event = parse_event(meta + records)

    assert event.meta["event_id"] == 7 and event.meta["trigger"] == "jolt" and event.meta["label"] == "jolt-accel"
    assert len(event.audio) == chunk * 9, len(event.audio)  # 8 chunks + 1 chunk of silence for the gap
    assert event.gaps == 1 and event.audio_start_ms == 1000
    decoded = [s for i, s in enumerate(event.audio) if not (3 * chunk <= i < 4 * chunk)]  # drop the gap
    noise = sum((a - b) ** 2 for a, b in zip(decoded, tone))
    snr = 10 * math.log10(sum(s * s for s in tone) / max(noise, 1))
    assert snr > 25, f"ADPCM round trip SNR {snr:.1f} dB"
    assert len(event.motion) == 25 and abs(event.motion[0][3] - 1.0) < 1e-6 and abs(event.motion[0][6] - 99.98) < 0.01
    assert event.marks == [{"time_ms": 1100, "trigger": "jolt", "detail": 1, "magnitude": 5.5}]

    print(f"selftest passed: {len(event.audio)} audio samples (ADPCM SNR {snr:.1f} dB), "
          f"{len(event.motion)} motion samples, {len(event.marks)} mark")


# --- BLE client --------------------------------------------------------------

class EventClient:
    """Talks to one connected device: alerts on events, downloads and deletes them."""

    def __init__(self, client, trigger_once: bool, output: Path) -> None:
        self.client = client
        self.trigger_once = trigger_once
        self.output = output
        self.triggered_at: Dict[int, datetime] = {}         # PC time each event's trigger was announced
        self.messages: asyncio.Queue = asyncio.Queue()
        self.to_fetch: List[Tuple[int, int]] = []           # (id, size)
        self.chunks: Optional[bytearray] = None
        self.expected = 0
        self.fetching_id: Optional[int] = None
        self.last_data = 0.0
        self.chunk_payload = 14

    def on_events(self, _, data: bytearray) -> None:
        self.messages.put_nowait(bytes(data))

    def on_data(self, _, data: bytearray) -> None:
        if self.chunks is None or len(data) < DATA_HEADER.size:
            return
        event_id, offset = DATA_HEADER.unpack_from(data, 0)
        payload = data[DATA_HEADER.size:]
        if event_id != self.fetching_id or offset != self.expected:
            return  # duplicate or out of order: re-requested from self.expected if it stalls
        end = offset + len(payload)
        self.chunks[offset:end] = payload
        self.expected = end
        self.last_data = asyncio.get_running_loop().time()

    async def command(self, *parts: bytes) -> None:
        await self.client.write_gatt_char(CONTROL_UUID, b"".join(parts), response=True)

    async def next_message(self, timeout: float) -> Optional[bytes]:
        try:
            return await asyncio.wait_for(self.messages.get(), timeout)
        except asyncio.TimeoutError:
            return None

    def handle_message(self, msg: bytes) -> Optional[bytes]:
        """Print/queue unsolicited messages. Returns the message so callers can wait on replies."""
        code = msg[0]
        if code == MSG_STARTED:
            event_id, trig, detail, t_ms = struct.unpack_from("<HBBI", msg, 1)
            self.triggered_at[event_id] = datetime.now()
            print(f"\n!!! EVENT {event_id} STARTED: {TRIGGER_NAMES.get(trig, trig)} trigger "
                  f"(detail {detail}) at device time {t_ms / 1000:.1f} s - recording ~50 s more\n")
        elif code == MSG_READY:
            event_id, size, trig = struct.unpack_from("<HIB", msg, 1)
            print(f"Event {event_id} ready ({size} bytes, {TRIGGER_NAMES.get(trig, trig)}).")
            self.queue_fetch(event_id, size)
        elif code == MSG_ERROR:
            err, event_id = struct.unpack_from("<BH", msg, 1)
            print(f"Device error: {ERROR_NAMES.get(err, err)} (event {event_id}).")
        return msg

    def queue_fetch(self, event_id: int, size: int) -> None:
        if all(event_id != e for e, _ in self.to_fetch) and event_id != self.fetching_id:
            self.to_fetch.append((event_id, size))

    async def list_events(self) -> None:
        await self.command(bytes([CMD_LIST]))
        while True:
            msg = await self.next_message(10.0)
            if msg is None:
                print("No reply to LIST.")
                return
            self.handle_message(msg)
            if msg[0] == MSG_INFO:
                event_id, size, trig, complete = struct.unpack_from("<HIBB", msg, 1)
                if complete:
                    self.queue_fetch(event_id, size)
                else:
                    print(f"Event {event_id} is still recording.")
            elif msg[0] == MSG_LIST_END:
                return

    async def fetch(self, event_id: int, size: int) -> Optional[bytes]:
        """Download one event, re-requesting from the last good offset if it stalls."""
        self.chunks, self.expected, self.fetching_id = bytearray(size), 0, event_id
        loop = asyncio.get_running_loop()
        print(f"Downloading event {event_id} ({size} bytes, {self.chunk_payload} B chunks) ...")
        started = loop.time()
        try:
            while self.expected < size:
                self.last_data = loop.time()
                await self.command(struct.pack("<BHIH", CMD_GET, event_id, self.expected, self.chunk_payload))
                while self.expected < size:
                    msg = await self.next_message(1.0)
                    if msg is not None:
                        self.handle_message(msg)
                        if msg[0] == MSG_DONE:
                            break
                        if msg[0] == MSG_ERROR:
                            return None
                    if loop.time() - self.last_data > TRANSFER_STALL_S:
                        print(f"  stalled at {self.expected} bytes, re-requesting")
                        break
            secs = loop.time() - started
            print(f"  received {size} bytes in {secs:.1f} s ({size / max(secs, 1e-3) / 1024:.1f} KB/s)")
            return bytes(self.chunks)
        finally:
            self.chunks, self.fetching_id = None, None

    async def delete(self, event_id: int) -> None:
        await self.command(struct.pack("<BH", CMD_DELETE, event_id))
        while True:
            msg = await self.next_message(10.0)
            if msg is None or self.handle_message(msg)[0] in (MSG_DELETED, MSG_ERROR):
                return

    async def run(self, disconnected: asyncio.Event) -> None:
        version = (await self.client.read_gatt_char(VERSION_UUID))[0]
        if version != PROTOCOL_VERSION:
            raise SystemExit(f"Device speaks protocol v{version}, this client needs v{PROTOCOL_VERSION}.")
        mtu = getattr(self.client, "mtu_size", 23) or 23
        self.chunk_payload = max(14, min(DEVICE_MAX_NOTIFY, mtu - 3) - DATA_HEADER.size)

        await self.client.start_notify(EVENTS_UUID, self.on_events)
        await self.client.start_notify(DATA_UUID, self.on_data)
        print(f"Connected (protocol v{version}, MTU {mtu}). Waiting for events (Ctrl+C to quit) ...")
        await self.list_events()
        if self.trigger_once:
            self.trigger_once = False
            print("Sending a manual trigger.")
            await self.command(bytes([CMD_TRIGGER]))

        while not disconnected.is_set():
            if self.to_fetch:
                event_id, size = self.to_fetch.pop(0)
                blob = await self.fetch(event_id, size)
                if blob is not None:
                    folder = save_event(blob, self.output, self.triggered_at.pop(event_id, None))
                    print(f"  saved to {folder}")
                    await self.delete(event_id)
                continue
            msg = await self.next_message(1.0)
            if msg is not None:
                self.handle_message(msg)


async def listen_once(trigger_once: bool, output: Path) -> None:
    """Find the device, connect, and handle events until it disconnects."""
    from bleak import BleakClient, BleakScanner

    print(f"Scanning for service {SERVICE_UUID} ...")
    device = None
    while device is None:  # keep scanning until the device shows up
        device = await BleakScanner.find_device_by_filter(
            lambda d, adv: SERVICE_UUID in (u.lower() for u in adv.service_uuids), timeout=10.0)

    disconnected = asyncio.Event()
    print(f"Connecting to {device.name or '?'} ({device.address}) ...")
    async with BleakClient(device, disconnected_callback=lambda _: disconnected.set()) as client:
        session = EventClient(client, trigger_once, output)
        runner = asyncio.ensure_future(session.run(disconnected))
        waiter = asyncio.ensure_future(disconnected.wait())
        done, _ = await asyncio.wait({runner, waiter}, return_when=asyncio.FIRST_COMPLETED)
        for task in (runner, waiter):
            if task not in done:
                task.cancel()
        if runner in done and runner.exception():
            raise runner.exception()
    print("Disconnected.")


async def listen_forever(trigger_once: bool, output: Path) -> None:
    """Run listen_once() in a loop, reconnecting after every disconnect or connection error."""
    from bleak.exc import BleakError

    while True:
        try:
            await listen_once(trigger_once, output)
            trigger_once = False
        except (BleakError, OSError, asyncio.TimeoutError) as e:  # link dropped or refused mid-setup
            print(f"Connection problem: {e}")
        await asyncio.sleep(RECONNECT_DELAY_S)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--selftest", action="store_true", help="verify the decoder offline and exit")
    parser.add_argument("--trigger", action="store_true", help="send one manual trigger after connecting")
    parser.add_argument("--output", type=Path, default=OUTPUT_DIR,
                        help=f"folder for saved events (default: {OUTPUT_DIR})")
    args = parser.parse_args()

    if args.selftest:
        selftest()
        return
    try:
        print(f"Saving events to {args.output.resolve()}")
        asyncio.run(listen_forever(args.trigger, args.output))
    except KeyboardInterrupt:
        print("Stopped.")


if __name__ == "__main__":
    main()
