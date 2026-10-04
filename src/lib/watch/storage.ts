/**
 * Saving downloaded watch events.
 *
 * In the app, each event becomes a folder in the app's private data folder,
 * like the Python client's output/ folders:
 *   events/<YYYYMMDD-HHMMSS>_event<id>_<trigger>/
 *     audio.wav   16 kHz mono audio
 *     motion.csv  50 Hz acceleration (g) and rotation (dps)
 *     meta.json   trigger details, timing, trigger marks
 *     event.bin   the raw bytes, exactly as sent
 * and a row in the watch_events table. In a browser (no file system or
 * database), events are kept in memory for this session, playable and
 * downloadable from the Watch page.
 */

import { isTauri } from '@tauri-apps/api/core';
import { BaseDirectory, mkdir, readFile, writeFile, writeTextFile } from '@tauri-apps/plugin-fs';
import { addActivity, getDB } from '$lib/db';
import { encodeWav, eventSummary, motionCsv, parseEvent } from './eventFormat';

export type SavedEvent = {
  /** Folder name; unique per saved event. */
  folder: string;
  deviceEventId: number;
  trigger: string;
  label: string;
  triggeredAt: string;
  audioSeconds: number;
  motionSamples: number;
  triggerCount: number;
};

/** Browser-only: the files of events saved this session. */
const sessionFiles = new Map<string, { audio: Blob; motion: Blob; meta: Blob; raw: Blob }>();
/** Browser-only: events saved this session, newest first. */
const browserEvents: SavedEvent[] = [];

function pad(n: number, width = 2) {
  return String(n).padStart(width, '0');
}

function folderName(stamp: Date, eventId: number, trigger: string) {
  const date = `${stamp.getFullYear()}${pad(stamp.getMonth() + 1)}${pad(stamp.getDate())}`;
  const time = `${pad(stamp.getHours())}${pad(stamp.getMinutes())}${pad(stamp.getSeconds())}`;
  return `${date}-${time}_event${pad(eventId, 5)}_${trigger}`;
}

async function ensureTable() {
  const db = await getDB();
  await db.execute(`
    CREATE TABLE IF NOT EXISTS watch_events (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      folder TEXT NOT NULL UNIQUE,
      device_event_id INTEGER NOT NULL,
      trigger TEXT NOT NULL,
      label TEXT NOT NULL,
      triggered_at TEXT NOT NULL,
      audio_seconds REAL NOT NULL,
      motion_samples INTEGER NOT NULL,
      trigger_count INTEGER NOT NULL,
      created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
    )
  `);
  return db;
}

/**
 * Decode and store one downloaded event.
 *
 * @param blob The event's bytes.
 * @param triggeredAt When the phone heard about the trigger (EVENT_STARTED), if it did; else now.
 */
export async function saveEvent(blob: Uint8Array, triggeredAt: Date | null): Promise<SavedEvent> {
  const event = parseEvent(blob);
  const summary = eventSummary(event, triggeredAt);
  const stamp = triggeredAt ?? new Date();
  let folder = folderName(stamp, event.meta.eventId, event.meta.trigger);

  const wav = encodeWav(event.audio, event.meta.audioRateHz || 16000);
  const csv = motionCsv(event);
  const json = JSON.stringify(summary, null, 2);

  if (isTauri()) {
    const db = await ensureTable();
    // Never overwrite an earlier save (the watch reuses ids after a reset).
    for (let suffix = 1; (await db.select<unknown[]>('SELECT 1 FROM watch_events WHERE folder = $1', [folder])).length; suffix++) {
      folder = `${folderName(stamp, event.meta.eventId, event.meta.trigger)}~${suffix}`;
    }
    const dir = `events/${folder}`;
    const options = { baseDir: BaseDirectory.AppData };
    await mkdir(dir, { ...options, recursive: true });
    await writeFile(`${dir}/event.bin`, blob, options);
    await writeFile(`${dir}/audio.wav`, wav, options);
    await writeTextFile(`${dir}/motion.csv`, csv, options);
    await writeTextFile(`${dir}/meta.json`, json, options);
    await db.execute(
      `INSERT INTO watch_events (folder, device_event_id, trigger, label, triggered_at, audio_seconds, motion_samples, trigger_count)
       VALUES ($1, $2, $3, $4, $5, $6, $7, $8)`,
      [
        folder,
        event.meta.eventId,
        event.meta.trigger,
        event.meta.label,
        stamp.toISOString(),
        summary.audioSeconds,
        summary.motionSamples,
        event.meta.triggerCount
      ]
    );
  } else {
    sessionFiles.set(folder, {
      audio: new Blob([wav as BlobPart], { type: 'audio/wav' }),
      motion: new Blob([csv], { type: 'text/csv' }),
      meta: new Blob([json], { type: 'application/json' }),
      raw: new Blob([blob as BlobPart], { type: 'application/octet-stream' })
    });
  }

  const saved: SavedEvent = {
    folder,
    deviceEventId: event.meta.eventId,
    trigger: event.meta.trigger,
    label: event.meta.label,
    triggeredAt: stamp.toISOString(),
    audioSeconds: summary.audioSeconds,
    motionSamples: summary.motionSamples,
    triggerCount: event.meta.triggerCount
  };
  await addActivity(
    'Watch recording saved',
    `${describeTrigger(saved.trigger, saved.label)}: ${saved.audioSeconds.toFixed(0)} s of audio and motion` +
      (saved.triggerCount > 1 ? ` (${saved.triggerCount} triggers)` : '') +
      '.'
  ).catch(() => {}); // no database in a browser
  if (!isTauri()) browserEvents.unshift(saved);
  return saved;
}

/** Saved events, newest first. */
export async function listSavedEvents(limit = 50): Promise<SavedEvent[]> {
  if (!isTauri()) return browserEvents.slice(0, limit);
  const db = await ensureTable();
  const rows = await db.select<
    {
      folder: string;
      device_event_id: number;
      trigger: string;
      label: string;
      triggered_at: string;
      audio_seconds: number;
      motion_samples: number;
      trigger_count: number;
    }[]
  >(
    `SELECT folder, device_event_id, trigger, label, triggered_at, audio_seconds, motion_samples, trigger_count
     FROM watch_events ORDER BY triggered_at DESC, id DESC LIMIT $1`,
    [limit]
  );
  return rows.map((r) => ({
    folder: r.folder,
    deviceEventId: r.device_event_id,
    trigger: r.trigger,
    label: r.label,
    triggeredAt: r.triggered_at,
    audioSeconds: r.audio_seconds,
    motionSamples: r.motion_samples,
    triggerCount: r.trigger_count
  }));
}

/** A playable URL for a saved event's audio. Revoke it with URL.revokeObjectURL when done. */
export async function audioUrl(folder: string): Promise<string> {
  if (!isTauri()) {
    const files = sessionFiles.get(folder);
    if (!files) throw new Error('This recording is no longer in memory.');
    return URL.createObjectURL(files.audio);
  }
  const bytes = await readFile(`events/${folder}/audio.wav`, { baseDir: BaseDirectory.AppData });
  return URL.createObjectURL(new Blob([bytes as BlobPart], { type: 'audio/wav' }));
}

/** Browser-only: download links for an event's files. */
export function sessionDownloads(folder: string): { name: string; url: string }[] {
  const files = sessionFiles.get(folder);
  if (!files) return [];
  return [
    { name: 'audio.wav', url: URL.createObjectURL(files.audio) },
    { name: 'motion.csv', url: URL.createObjectURL(files.motion) },
    { name: 'meta.json', url: URL.createObjectURL(files.meta) },
    { name: 'event.bin', url: URL.createObjectURL(files.raw) }
  ];
}

/** "Jolt (jolt-accel)", "Keyword (NN0:alexa)", "Manual". */
export function describeTrigger(trigger: string, label: string): string {
  const name = trigger.charAt(0).toUpperCase() + trigger.slice(1);
  return label && label !== trigger ? `${name} (${label})` : name;
}
