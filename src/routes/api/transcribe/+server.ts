// src/routes/api/transcribe/+server.ts
import { json, error } from '@sveltejs/kit';
import { ElevenLabsClient } from '@elevenlabs/elevenlabs-js';
import { ELEVENLABS_API_KEY } from '$env/static/private';

const client = new ElevenLabsClient({ apiKey: ELEVENLABS_API_KEY });

export async function POST({ request }) {
  const contentType = request.headers.get('content-type') ?? 'audio/mpeg';
  const bytes = await request.arrayBuffer();

  if (!bytes.byteLength) throw error(400, 'Empty body');

  const file = new Blob([bytes], { type: contentType });

  const result = await client.speechToText.convert({
    file,
    modelId: 'scribe_v2',
    diarize: true
  });

  return json({ text: result.text });
}