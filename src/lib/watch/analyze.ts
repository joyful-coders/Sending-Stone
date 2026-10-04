import { audioBlob, saveAnalysis, type SavedEvent } from './storage';
import { analysis } from './analysisState.svelte';


export async function analyzeEvent(ev: SavedEvent) {
  try {
    const wav = await audioBlob(ev.folder);

    const t = await fetch('/api/transcribe', {
      method: 'POST',
      headers: { 'Content-Type': 'audio/wav' }, // match the real format
      body: wav
    });
    if (!t.ok) throw new Error(`transcribe ${t.status}`);
    const { text } = await t.json();

    if (!text?.trim()) {
      await saveAnalysis(ev.folder, { status: 'empty' });
      return null;
    }

    const s = await fetch('/api/toxicity', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ text })
    });
    if (!s.ok) throw new Error(`toxicity ${s.status}`);
    const { score } = await s.json();

    await saveAnalysis(ev.folder, { transcript: text, toxicity: score, status: 'done' });
    return { text, score };
  } catch (e) {
    console.error(e);
    await saveAnalysis(ev.folder, { status: 'failed' });
    return null;
  }
  finally {
    analysis.version++;
  }
}