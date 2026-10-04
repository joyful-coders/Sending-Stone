// src/routes/api/transcribe/+server.ts
import { json, error } from '@sveltejs/kit';
import { ElevenLabsClient } from '@elevenlabs/elevenlabs-js';
import { env } from '$env/dynamic/private';

export async function POST({ request }) {
  // Read per request, not at build time, so the app builds without the key.
  if (!env.ELEVENLABS_API_KEY) throw error(503, 'ELEVENLABS_API_KEY is not set (.env).');
  const client = new ElevenLabsClient({ apiKey: env.ELEVENLABS_API_KEY });

  const contentType = request.headers.get('content-type') ?? 'audio/mpeg';
  const bytes = await request.arrayBuffer();

  if (!bytes.byteLength) throw error(400, 'Empty body');

  const file = new Blob([bytes], { type: contentType });

  const result = await client.speechToText.convert({
    file,
    modelId: 'scribe_v2'
  });

  return json({ text: result.text });
}