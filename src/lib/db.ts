import { isTauri } from '@tauri-apps/api/core';
import { SCHEMA } from './schema';

export type DB = {
  select<T>(sql: string, params?: unknown[]): Promise<T>;
  execute(sql: string, params?: unknown[]): Promise<{ rowsAffected: number; lastInsertId?: number }>;
};

let database: DB | null = null;

async function call(op: 'select' | 'execute', sql: string, params: unknown[] = []) {
  const res = await fetch('/api/db', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ op, sql, params })
  });
  if (!res.ok) {
    let msg = `DB error ${res.status}`;
    try { msg = (await res.json()).message ?? msg; } catch {}
    throw new Error(msg);
  }
  return res.json();
}

export async function getDB(): Promise<DB> {
  if (database) return database;

  if (isTauri()) {
    const { default: Database } = await import('@tauri-apps/plugin-sql');
    const tauriDb = await Database.load('sqlite:safety-plan.db');
    for (const sql of SCHEMA) await tauriDb.execute(sql);
    database = tauriDb as unknown as DB;
  } else {
    // Browser/demo: the server owns the SQLite file and creates the schema.
    database = {
      select: (sql, params) => call('select', sql, params),
      execute: (sql, params) => call('execute', sql, params)
    };
  }
  return database;
}

export async function addActivity(title: string, detail: string) {
  const db = await getDB();
  await db.execute(`INSERT INTO activity_events (title, detail) VALUES ($1, $2)`, [title, detail]);
}