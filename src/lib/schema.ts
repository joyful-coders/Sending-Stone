export const SCHEMA = [
  `CREATE TABLE IF NOT EXISTS safety_plan (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    check_in_enabled INTEGER NOT NULL DEFAULT 1,
    check_in_interval TEXT NOT NULL DEFAULT '30',
    grace_period TEXT NOT NULL DEFAULT '5',
    alert_method TEXT NOT NULL DEFAULT 'Text message',
    include_location INTEGER NOT NULL DEFAULT 0,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
  )`,
  `CREATE TABLE IF NOT EXISTS trusted_contacts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    relationship TEXT NOT NULL,
    phone TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
  )`,
  `CREATE TABLE IF NOT EXISTS activity_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title TEXT NOT NULL,
    detail TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
  )`,
  `INSERT OR IGNORE INTO safety_plan
    (id, check_in_enabled, check_in_interval, grace_period, alert_method, include_location)
    VALUES (1, 1, '30', '5', 'Text message', 0)`
];