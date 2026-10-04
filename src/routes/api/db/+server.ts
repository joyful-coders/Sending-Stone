import { json, error } from '@sveltejs/kit';
import { db } from '$lib/server/db';

export async function POST({ request }) {
  const { op, sql, params = [] } = await request.json();

  const order: number[] = [];
  const text = String(sql).replace(/\$(\d+)/g, (_, n) => {
    order.push(Number(n) - 1);
    return '?';
  });
  const args = order.length ? order.map((i) => params[i]) : params;

  try {
    const stmt = db.prepare(text);
    if (op === 'select') return json(stmt.all(...args));
    const r = stmt.run(...args);
    return json({ rowsAffected: r.changes, lastInsertId: Number(r.lastInsertRowid) });
  } catch (e) {
    throw error(500, e instanceof Error ? e.message : String(e));
  }
}