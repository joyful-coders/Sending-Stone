import Database from 'better-sqlite3';
import { mkdirSync } from 'node:fs';
import { SCHEMA } from '$lib/schema';

mkdirSync('data', { recursive: true });
export const db = new Database('data/safety-plan.db');
db.pragma('journal_mode = WAL');
for (const sql of SCHEMA) db.exec(sql);