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
  transcript: string | null;
  toxicityScore: number | null;
  analysisStatus: 'pending' | 'done' | 'failed' | 'empty';
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

  // Migration: add the analysis columns to tables created before this change.
  const cols = await db.select<{ name: string }[]>('PRAGMA table_info(watch_events)');
  const have = new Set(cols.map((c) => c.name));
  if (!have.has('transcript')) await db.execute('ALTER TABLE watch_events ADD COLUMN transcript TEXT');
  if (!have.has('toxicity_score')) await db.execute('ALTER TABLE watch_events ADD COLUMN toxicity_score REAL');
  if (!have.has('analysis_status'))
    await db.execute("ALTER TABLE watch_events ADD COLUMN analysis_status TEXT NOT NULL DEFAULT 'pending'");
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

    const db = await ensureTable();
    // Never overwrite an earlier save (the watch reuses ids after a reset).
    for (let suffix = 1; (await db.select<unknown[]>('SELECT 1 FROM watch_events WHERE folder = $1', [folder])).length; suffix++) {
      folder = `${folderName(stamp, event.meta.eventId, event.meta.trigger)}~${suffix}`;
    }

    if(isTauri()){
      const dir = `events/${folder}`;
      const options = { baseDir: BaseDirectory.AppData };
      await mkdir(dir, { ...options, recursive: true });
      await writeFile(`${dir}/event.bin`, blob, options);
      await writeFile(`${dir}/audio.wav`, wav, options);
      await writeTextFile(`${dir}/motion.csv`, csv, options);
      await writeTextFile(`${dir}/meta.json`, json, options);
    }
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
  
   if (!isTauri()) {
  const audioBlobData = new Blob([wav as BlobPart], { type: 'audio/wav' });
  sessionFiles.set(folder, {
    audio: audioBlobData,
    motion: new Blob([csv], { type: 'text/csv' }),
    meta: new Blob([json], { type: 'application/json' }),
    raw: new Blob([blob as BlobPart], { type: 'application/octet-stream' })
  });
  // Persist the audio on the server so playback survives a refresh.
  try {
    const res = await fetch(`/api/audio/${encodeURIComponent(folder)}`, {
      method: 'PUT',
      headers: { 'Content-Type': 'audio/wav' },
      body: audioBlobData
    });
    if (!res.ok) console.warn('Audio upload failed:', res.status);
  } catch (e) {
    console.warn('Audio upload failed:', e);
  }
}

  const saved: SavedEvent = {
    folder,
    deviceEventId: event.meta.eventId,
    trigger: event.meta.trigger,
    label: event.meta.label,
    triggeredAt: stamp.toISOString(),
    audioSeconds: summary.audioSeconds,
    motionSamples: summary.motionSamples,
    triggerCount: event.meta.triggerCount,
    transcript: null,
    toxicityScore: null,
    analysisStatus: 'pending'
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
  browserEvents.slice(0, limit);
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
      transcript: string | null;
      toxicity_score: number | null;
      analysis_status: string;
    }[]
  >(
    `SELECT folder, device_event_id, trigger, label, triggered_at, audio_seconds, motion_samples, trigger_count, transcript, toxicity_score, analysis_status
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
    triggerCount: r.trigger_count,
    transcript: r.transcript,
    toxicityScore: r.toxicity_score,
    analysisStatus: r.analysis_status as SavedEvent['analysisStatus']
  }));
}
/** A playable URL for a saved event's audio. Revoke it with URL.revokeObjectURL when done. */

export async function audioBlob(folder: string): Promise<Blob> {
  if (!isTauri()) {
    const files = sessionFiles.get(folder);
    if (files) return files.audio;
    // Not in memory (page was refreshed): fetch it from the server.
    const res = await fetch(`/api/audio/${encodeURIComponent(folder)}`);
    if (!res.ok) throw new Error('This recording is no longer available.');
    return res.blob();
  }
  const bytes = await readFile(`events/${folder}/audio.wav`, { baseDir: BaseDirectory.AppData });
  return new Blob([bytes as BlobPart], { type: 'audio/wav' });
}

/** A playable URL for a saved event's audio. Revoke it with URL.revokeObjectURL when done. */
export async function audioUrl(folder: string): Promise<string> {
  return URL.createObjectURL(await audioBlob(folder));
}

export async function saveAnalysis(
  folder: string,
  r: { transcript?: string; toxicity?: number | null; status: 'done' | 'failed' | 'empty' }
) {
  const db = await ensureTable();
  await db.execute(
    `UPDATE watch_events SET transcript = $1, toxicity_score = $2, analysis_status = $3 WHERE folder = $4`,
    [r.transcript ?? null, r.toxicity ?? null, r.status, folder]
  );
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

//Manual Audio File import for dev testing
/** Dev/demo: add a plain audio file as a recording (no watch needed). */
export async function importAudioFile(file: File): Promise<SavedEvent> {
  const buf = await file.arrayBuffer();

  // Get the duration by decoding (works for wav/mp3/m4a/webm)
  const ctx = new AudioContext();
  const seconds = (await ctx.decodeAudioData(buf.slice(0))).duration;
  await ctx.close();

  const stamp = new Date();
  const db = await ensureTable();
  const base = folderName(stamp, 0, 'import');
  let folder = base;
  for (let n = 1; (await db.select<unknown[]>('SELECT 1 FROM watch_events WHERE folder = $1', [folder])).length; n++) {
    folder = `${base}~${n}`;
  }

  const audio = new Blob([buf], { type: file.type || 'audio/wav' });

  if (isTauri()) {
    const options = { baseDir: BaseDirectory.AppData };
    await mkdir(`events/${folder}`, { ...options, recursive: true });
    await writeFile(`events/${folder}/audio.wav`, new Uint8Array(buf), options);
  } else {
    sessionFiles.set(folder, {
      audio,
      motion: new Blob([''], { type: 'text/csv' }),
      meta: new Blob(['{}'], { type: 'application/json' }),
      raw: new Blob([buf])
    });
    const res = await fetch(`/api/audio/${encodeURIComponent(folder)}`, {
      method: 'PUT',
      headers: { 'Content-Type': 'audio/wav' },
      body: audio
    });
    if (!res.ok) throw new Error(`Audio upload failed (${res.status})`);
  }

  await db.execute(
    `INSERT INTO watch_events (folder, device_event_id, trigger, label, triggered_at, audio_seconds, motion_samples, trigger_count)
     VALUES ($1, $2, $3, $4, $5, $6, $7, $8)`,
    [folder, 0, 'import', 'import', stamp.toISOString(), seconds, 0, 1]
  );

  return {
    folder,
    deviceEventId: 0,
    trigger: 'import',
    label: 'import',
    triggeredAt: stamp.toISOString(),
    audioSeconds: seconds,
    motionSamples: 0,
    triggerCount: 1,
    transcript: null,
    toxicityScore: null,
    analysisStatus: 'pending'
  };
}