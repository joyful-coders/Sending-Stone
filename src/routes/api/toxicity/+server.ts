import { json, error } from '@sveltejs/kit';

const MODEL_URL = 'http://127.0.0.1:8000/score';

export async function POST({ request, fetch }) {
  const { text } = await request.json();
  if (!text?.trim()) throw error(400, 'Empty text');

  let r: Response;
  try {
    r = await fetch(MODEL_URL, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ text })
    });
  } catch {
    throw error(502, 'Cannot reach the model service on port 8000. Is uvicorn running?');
  }

  if (!r.ok) throw error(502, `Model service returned ${r.status}: ${await r.text()}`);
  return json(await r.json()); // { score, labels, chunks }
}