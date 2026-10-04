import { error } from '@sveltejs/kit';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';

const DIR = path.resolve('data/audio');

// Folder names look like 20260101-120000_event00001_jolt~1. Reject anything else
// so a request can't escape the data/audio directory.
function fileFor(folder: string) {
  if (!/^[\w~-]+$/.test(folder)) throw error(400, 'Bad folder name');
  return path.join(DIR, `${folder}.wav`);
}

export async function PUT({ params, request }) {
  const file = fileFor(params.folder);
  const bytes = new Uint8Array(await request.arrayBuffer());
  if (!bytes.length) throw error(400, 'Empty body');

  await mkdir(DIR, { recursive: true });
  await writeFile(file, bytes);
  return new Response(null, { status: 204 });
}

export async function GET({ params }) {
  const file = fileFor(params.folder);
  let bytes: Uint8Array;
  try {
    bytes = new Uint8Array(await readFile(file));
  } catch {
    throw error(404, 'Recording not found');
  }
  return new Response(bytes, {
    headers: {
      'Content-Type': 'audio/wav',
      'Content-Length': String(bytes.length),
      'Cache-Control': 'no-store'
    }
  });
}