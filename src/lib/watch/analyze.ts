import { audioBlob, saveAnalysis, type SavedEvent } from './storage';
import { analysis } from './analysisState.svelte';
import { addActivity } from '$lib/db';

const ALERT_THRESHOLD = 0.8;

export async function analyzeEvent(ev: SavedEvent) {
  console.log('[analyze] start', ev.folder);
  try {
    const wav = await audioBlob(ev.folder);
    console.log('[analyze] audio bytes:', wav.size);

    const t = await fetch('/api/transcribe', {
      method: 'POST',
      headers: { 'Content-Type': 'audio/wav' },
      body: wav
    });
    console.log('[analyze] transcribe status:', t.status);
    if (!t.ok) throw new Error(`transcribe ${t.status}: ${await t.text()}`);
    const { text } = await t.json();
    console.log('[analyze] transcript:', text);

    if (!text?.trim()) {
      await saveAnalysis(ev.folder, { status: 'empty' });
      return null;
    }

    const s = await fetch('/api/toxicity', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ text })
    });
    console.log('[analyze] toxicity status:', s.status);
    if (!s.ok) throw new Error(`toxicity ${s.status}: ${await s.text()}`);
    const { score } = await s.json();

    await saveAnalysis(ev.folder, { transcript: text, toxicity: score, status: 'done' });
    if (score >= ALERT_THRESHOLD) {
  try {
    const n = await fetch('/api/notify', {
      method: 'POST',
    });
    if (!n.ok) throw new Error(`notify ${n.status}: ${await n.text()}`);
    await addActivity('Alert sent', `SMS sent for ${ev.folder} (score ${score.toFixed(2)}).`);
  } catch (e) {
    console.error('[analyze] SMS failed:', e);
    await addActivity('Alert failed', `Could not send SMS for ${ev.folder}.`).catch(() => {});
  }
}
    return { text, score };
  } catch (e) {
    console.error('[analyze] failed:', e);
    await saveAnalysis(ev.folder, { status: 'failed' });
    return null;
  } finally {
    analysis.version++;
  }
}