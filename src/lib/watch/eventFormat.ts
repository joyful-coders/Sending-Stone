/**
 * Decodes a downloaded watch event: a 64-byte header, then audio (IMA-ADPCM),
 * motion and trigger-mark records. Port of parse_event()/save_event() from
 * hardware-ble-test/ble_client.py; byte formats in hardware/BLE_PROTOCOL.md
 * and hardware/src/recorder/event_format.h.
 */

import { triggerName } from './protocol';

const META_BYTES = 64;
const RECORD_BYTES = 8;
const AUDIO_HEADER_BYTES = 6;
const MOTION_SAMPLE_BYTES = 14;
const RecordType = { Audio: 1, Motion: 2, Mark: 3 } as const;

export type EventMeta = {
  eventId: number;
  trigger: string;
  triggerDetail: number;
  label: string;
  triggerMagnitude: number;
  triggerCount: number;
  /** Device time (ms since boot) of the first trigger. */
  triggerTimeMs: number;
  lastTriggerTimeMs: number;
  /** 0 if the watch lost power mid-event. */
  endTimeMs: number;
  complete: boolean;
  segments: number;
  dataBytes: number;
  audioRateHz: number;
  motionRateHz: number;
  accelRangeG: number;
  gyroRangeDps: number;
};

/** One motion sample: device time (ms), acceleration (g) and rotation (dps). */
export type MotionSample = [t: number, ax: number, ay: number, az: number, gx: number, gy: number, gz: number];

export type TriggerMark = { timeMs: number; trigger: string; detail: number; magnitude: number };

export type DecodedEvent = {
  meta: EventMeta;
  /** 16-bit PCM at meta.audioRateHz, missed chunks filled with silence. */
  audio: Int16Array;
  /** Device time of the first audio sample, or null if there's no audio. */
  audioStartMs: number | null;
  motion: MotionSample[];
  marks: TriggerMark[];
  /** Audio chunks the watch missed (filled with silence). */
  gaps: number;
};

// IMA-ADPCM tables (hardware/src/recorder/adpcm.h)
const INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8];
const STEP_TABLE = [
  7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
  118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
  1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
  6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
  32767
];

/** Decode `count` IMA-ADPCM samples (two per byte, low nibble first) into `out` at `at`. */
function adpcmDecode(codes: Uint8Array, count: number, predictor: number, index: number, out: Int16Array, at: number) {
  for (let i = 0; i < count; i++) {
    const code = (codes[i >> 1] >> (4 * (i & 1))) & 0x0f;
    const step = STEP_TABLE[index];
    let delta = step >> 3;
    if (code & 4) delta += step;
    if (code & 2) delta += step >> 1;
    if (code & 1) delta += step >> 2;
    predictor = code & 8 ? predictor - delta : predictor + delta;
    predictor = Math.max(-32768, Math.min(32767, predictor));
    index = Math.max(0, Math.min(88, index + INDEX_TABLE[code]));
    out[at + i] = predictor;
  }
}

export function parseMeta(blob: Uint8Array): EventMeta {
  if (blob.byteLength < META_BYTES) throw new Error('event too short');
  const v = new DataView(blob.buffer, blob.byteOffset, blob.byteLength);
  const magic = String.fromCharCode(blob[0], blob[1], blob[2], blob[3]);
  if (magic !== 'NVEV') throw new Error('not an event (bad magic)');
  if (blob[4] !== 1) throw new Error(`unsupported event format ${blob[4]}`);

  let label = '';
  for (let i = 42; i < 60 && blob[i] !== 0; i++) label += String.fromCharCode(blob[i]);

  return {
    eventId: v.getUint16(8, true),
    trigger: triggerName(blob[5]),
    triggerDetail: blob[6],
    complete: blob[7] !== 0,
    segments: v.getUint16(10, true),
    triggerTimeMs: v.getUint32(12, true),
    lastTriggerTimeMs: v.getUint32(16, true),
    endTimeMs: v.getUint32(20, true),
    dataBytes: v.getUint32(24, true),
    triggerMagnitude: Math.round(v.getFloat32(28, true) * 1000) / 1000,
    audioRateHz: v.getUint16(32, true),
    motionRateHz: v.getUint16(34, true),
    accelRangeG: v.getUint16(36, true),
    gyroRangeDps: v.getUint16(38, true),
    triggerCount: v.getUint16(40, true),
    label
  };
}

export function parseEvent(blob: Uint8Array): DecodedEvent {
  const meta = parseMeta(blob);
  const v = new DataView(blob.buffer, blob.byteOffset, blob.byteLength);
  const accelScale = meta.accelRangeG / 32768;
  const gyroScale = meta.gyroRangeDps / 32768;
  const end = Math.min(blob.byteLength, META_BYTES + meta.dataBytes);

  // First pass: count audio samples (including silence for gaps) to size the output once.
  let sampleCount = 0;
  for (let pos = META_BYTES; pos + RECORD_BYTES <= end; ) {
    const length = v.getUint16(pos + 2, true);
    if (pos + RECORD_BYTES + length > end) break;
    if (blob[pos] === RecordType.Audio && length >= AUDIO_HEADER_BYTES) {
      const count = v.getUint16(pos + RECORD_BYTES + 4, true);
      sampleCount += count * (1 + blob[pos + 1]);
    }
    pos += RECORD_BYTES + length;
  }

  const audio = new Int16Array(sampleCount); // zero-filled, so gaps are already silence
  const motion: MotionSample[] = [];
  const marks: TriggerMark[] = [];
  let audioStartMs: number | null = null;
  let gaps = 0;
  let written = 0;

  for (let pos = META_BYTES; pos + RECORD_BYTES <= end; ) {
    const type = blob[pos];
    const info = blob[pos + 1];
    const length = v.getUint16(pos + 2, true);
    const timeMs = v.getUint32(pos + 4, true);
    const payload = pos + RECORD_BYTES;
    pos = payload + length;
    if (pos > end) break; // truncated final record

    if (type === RecordType.Audio && length >= AUDIO_HEADER_BYTES) {
      const predictor = v.getInt16(payload, true);
      const index = blob[payload + 2];
      const count = v.getUint16(payload + 4, true);
      if (audioStartMs === null) audioStartMs = timeMs;
      if (info) {
        // chunks the watch missed: keep timing by inserting silence
        gaps += info;
        written += info * count;
      }
      adpcmDecode(blob.subarray(payload + AUDIO_HEADER_BYTES, payload + length), count, predictor, index, audio, written);
      written += count;
    } else if (type === RecordType.Motion) {
      for (let i = 0; i < info && (i + 1) * MOTION_SAMPLE_BYTES <= length; i++) {
        const s = payload + i * MOTION_SAMPLE_BYTES;
        motion.push([
          timeMs + v.getUint16(s, true),
          v.getInt16(s + 2, true) * accelScale,
          v.getInt16(s + 4, true) * accelScale,
          v.getInt16(s + 6, true) * accelScale,
          v.getInt16(s + 8, true) * gyroScale,
          v.getInt16(s + 10, true) * gyroScale,
          v.getInt16(s + 12, true) * gyroScale
        ]);
      }
    } else if (type === RecordType.Mark && length >= 8) {
      marks.push({
        timeMs,
        trigger: triggerName(info),
        detail: blob[payload],
        magnitude: Math.round(v.getFloat32(payload + 4, true) * 1000) / 1000
      });
    }
  }

  return { meta, audio: audio.subarray(0, written), audioStartMs, motion, marks, gaps };
}

/** 16-bit mono PCM WAV file. */
export function encodeWav(samples: Int16Array, rateHz: number): Uint8Array {
  const bytes = new Uint8Array(44 + samples.byteLength);
  const v = new DataView(bytes.buffer);
  const text = (at: number, s: string) => [...s].forEach((c, i) => (bytes[at + i] = c.charCodeAt(0)));
  text(0, 'RIFF');
  v.setUint32(4, 36 + samples.byteLength, true);
  text(8, 'WAVE');
  text(12, 'fmt ');
  v.setUint32(16, 16, true);
  v.setUint16(20, 1, true); // PCM
  v.setUint16(22, 1, true); // mono
  v.setUint32(24, rateHz, true);
  v.setUint32(28, rateHz * 2, true);
  v.setUint16(32, 2, true);
  v.setUint16(34, 16, true);
  text(36, 'data');
  v.setUint32(40, samples.byteLength, true);
  for (let i = 0; i < samples.length; i++) v.setInt16(44 + 2 * i, samples[i], true);
  return bytes;
}

/** motion.csv, with time relative to the first trigger. */
export function motionCsv(event: DecodedEvent): string {
  const trigger = event.meta.triggerTimeMs;
  const lines = ['t_ms,t_from_trigger_s,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps'];
  for (const [t, ax, ay, az, gx, gy, gz] of event.motion) {
    lines.push(
      [t, ((t - trigger) / 1000).toFixed(3), ax.toFixed(4), ay.toFixed(4), az.toFixed(4), gx.toFixed(2), gy.toFixed(2), gz.toFixed(2)].join(',')
    );
  }
  return lines.join('\n') + '\n';
}

/** meta.json contents: the header plus timing and the trigger marks. */
export function eventSummary(event: DecodedEvent, triggeredAt: Date | null) {
  const { meta } = event;
  const rate = meta.audioRateHz || 16000;
  return {
    ...meta,
    savedAt: new Date().toISOString(),
    triggeredAt: triggeredAt ? triggeredAt.toISOString() : null,
    audioSeconds: Math.round((event.audio.length / rate) * 100) / 100,
    audioStartMs: event.audioStartMs,
    audioSecondsBeforeTrigger:
      event.audioStartMs !== null ? Math.round((meta.triggerTimeMs - event.audioStartMs) / 10) / 100 : null,
    audioGapsChunks: event.gaps,
    motionSamples: event.motion.length,
    marks: event.marks
  };
}
