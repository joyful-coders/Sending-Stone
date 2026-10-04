/**
 * Connection to the watch: finds it, stays connected (reconnecting after any
 * drop), raises an alert the moment it reports a trigger, then downloads each
 * finished event, saves it, and deletes it from the watch.
 *
 * Port of hardware-ble-test/ble_client.py (EventClient + listen_forever).
 * One shared instance, `watch`, so every page sees the same state.
 */

import { addActivity } from '$lib/db';
import { getTransport, type FoundDevice } from './transport';
import {
  CONTROL_UUID,
  DATA_UUID,
  DATA_HEADER_BYTES,
  DEVICE_MAX_NOTIFY,
  EVENTS_UUID,
  PROTOCOL_VERSION,
  VERSION_UUID,
  deleteCommand,
  getCommand,
  listCommand,
  parseDataChunk,
  parseMessage,
  triggerCommand,
  type WatchMessage
} from './protocol';
import { describeTrigger, saveEvent, type SavedEvent } from './storage';

export type WatchStatus = 'idle' | 'scanning' | 'connecting' | 'connected' | 'reconnecting' | 'error';

export type WatchAlert = { eventId: number; trigger: string; at: Date };

const REMEMBERED_KEY = 'watch.device';
const SCAN_TIMEOUT_MS = 10_000;
const RECONNECT_DELAY_MS = 2_000;
/** Re-request a transfer if no data arrives for this long. */
const TRANSFER_STALL_MS = 10_000;
const REPLY_TIMEOUT_MS = 10_000;
const LOG_LINES = 100;

/** Messages from the events characteristic, waited on with a timeout. */
class MessageQueue {
  private items: WatchMessage[] = [];
  private waiter: ((m: WatchMessage | null) => void) | null = null;

  push(message: WatchMessage) {
    if (this.waiter) this.waiter(message);
    else this.items.push(message);
  }

  /** The next message, or null after `timeoutMs`. Only one wait at a time: a new one ends the previous with null. */
  next(timeoutMs: number): Promise<WatchMessage | null> {
    if (this.items.length) return Promise.resolve(this.items.shift()!);
    return new Promise((resolve) => {
      const finish = (m: WatchMessage | null) => {
        clearTimeout(timer);
        if (this.waiter === finish) this.waiter = null;
        resolve(m);
      };
      const timer = setTimeout(() => finish(null), timeoutMs);
      this.waiter?.(null);
      this.waiter = finish;
    });
  }

  clear() {
    this.items = [];
    this.waiter?.(null);
  }
}

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

function loadRemembered(): FoundDevice | null {
  try {
    const raw = localStorage.getItem(REMEMBERED_KEY);
    return raw ? (JSON.parse(raw) as FoundDevice) : null;
  } catch {
    return null;
  }
}

class WatchClient {
  status = $state<WatchStatus>('idle');
  /** Human-readable detail for the status (errors, what's happening). */
  detail = $state('');
  /** Boards found by the current scan (native only; the browser uses its own picker). */
  found = $state<FoundDevice[]>([]);
  /** The board this app uses, remembered across launches. Any board running the firmware can be chosen. */
  device = $state<FoundDevice | null>(loadRemembered());
  /** Download in progress, if any. */
  transfer = $state<{ eventId: number; received: number; size: number } | null>(null);
  /** The latest trigger the watch reported, until dismissed. */
  alert = $state<WatchAlert | null>(null);
  /** The most recently saved event. */
  lastSaved = $state<SavedEvent | null>(null);
  /** Bumped whenever an event is saved, so lists can reload. */
  savedCount = $state(0);
  log = $state<string[]>([]);

  readonly transport = getTransport();

  /** Callbacks for other parts of the app (alerting contacts, for example). */
  onEventStarted: ((alert: WatchAlert) => void) | null = null;
  onEventSaved: ((event: SavedEvent) => void) | null = null;

  private messages = new MessageQueue();
  private running = false;
  private stopRequested = false;
  private linkUp = false;
  private triggeredAt = new Map<number, Date>();
  private toFetch: { id: number; size: number }[] = [];
  // transfer in progress
  private chunkPayload = DEVICE_MAX_NOTIFY - DATA_HEADER_BYTES;
  private buffer: Uint8Array | null = null;
  private fetchingId: number | null = null;
  private expected = 0;
  private lastData = 0;
  private restartGet = false;
  private lastProgress = 0;

  private say(line: string) {
    const stamp = new Date().toLocaleTimeString();
    this.log = [`${stamp}  ${line}`, ...this.log].slice(0, LOG_LINES);
  }

  /** Look for watches. Native: fills `found`. Browser: opens the picker and connects to the chosen board. */
  async scan() {
    const problem = await this.transport.prepare();
    if (problem) {
      this.status = 'error';
      this.detail = problem;
      return;
    }
    this.found = [];
    this.status = 'scanning';
    this.detail = this.transport.scanShowsList ? 'Looking for watches nearby…' : 'Choose your watch.';
    try {
      await this.transport.scan((devices) => (this.found = devices), SCAN_TIMEOUT_MS);
      if (this.transport.scanShowsList) {
        await sleep(SCAN_TIMEOUT_MS);
        await this.transport.stopScan();
        if (this.status === 'scanning') {
          this.status = 'idle';
          this.detail = this.found.length ? 'Pick your watch.' : 'No watch found. Is it on and nearby?';
        }
      } else if (this.found[0]) {
        await this.use(this.found[0]);
      }
    } catch (e) {
      this.status = 'idle';
      this.detail = e instanceof Error ? e.message : String(e); // includes the user cancelling the picker
    }
  }

  /** Use this board from now on (remembered), and connect to it. */
  async use(device: FoundDevice) {
    await this.transport.stopScan();
    this.device = device;
    try {
      localStorage.setItem(REMEMBERED_KEY, JSON.stringify(device));
    } catch {
      // storage unavailable: just not remembered
    }
    this.start();
  }

  /** Connect to the remembered board and stay connected until `stop()`. Safe to call repeatedly. */
  start() {
    if (this.running || !this.device) return;
    this.running = true;
    this.stopRequested = false;
    void this.runForever();
  }

  /** Disconnect and stop reconnecting (the board stays remembered). */
  async stop() {
    this.stopRequested = true;
    this.messages.clear();
    await this.transport.disconnect();
    this.status = 'idle';
    this.detail = 'Disconnected.';
  }

  /** Disconnect and forget the board, so a different one can be chosen. */
  async forget() {
    await this.stop();
    this.device = null;
    this.found = [];
    try {
      localStorage.removeItem(REMEMBERED_KEY);
    } catch {
      // ignore
    }
  }

  /** Ask the watch to start (or extend) an event, for testing. */
  async sendTestTrigger() {
    if (!this.linkUp) return;
    await this.transport.write(CONTROL_UUID, triggerCommand());
    this.say('Sent a manual trigger.');
  }

  dismissAlert() {
    this.alert = null;
  }

  private async runForever() {
    while (!this.stopRequested && this.device) {
      try {
        await this.runSession(this.device);
      } catch (e) {
        if (this.stopRequested) break;
        const message = e instanceof Error ? e.message : String(e);
        this.say(`Connection problem: ${message}`);
        if (message.startsWith('Protocol')) {
          this.status = 'error';
          this.detail = message;
          break;
        }
      }
      if (this.stopRequested) break;
      this.status = 'reconnecting';
      this.detail = `Waiting for ${this.device?.name ?? 'the watch'}…`;
      await sleep(RECONNECT_DELAY_MS);
    }
    this.running = false;
  }

  /** On the native side the board must have been seen by a scan before connecting; wait until it shows up. */
  private async waitUntilVisible(device: FoundDevice): Promise<void> {
    if (!this.transport.scanShowsList) return;
    let seen = false;
    await this.transport.scan((devices) => {
      if (devices.some((d) => d.id === device.id)) seen = true;
    }, SCAN_TIMEOUT_MS);
    const until = Date.now() + SCAN_TIMEOUT_MS;
    while (!seen && Date.now() < until && !this.stopRequested) await sleep(200);
    await this.transport.stopScan();
    if (!seen) throw new Error(`${device.name} is not in range`);
  }

  private async runSession(device: FoundDevice) {
    this.status = 'connecting';
    this.detail = `Connecting to ${device.name}…`;
    this.messages.clear();
    this.toFetch = [];
    await this.waitUntilVisible(device);

    let lost!: () => void;
    const disconnected = new Promise<void>((resolve) => (lost = resolve));
    await this.transport.connect(device.id, () => {
      this.linkUp = false;
      this.messages.clear();
      lost();
    });
    this.linkUp = true;

    try {
      const version = (await this.transport.read(VERSION_UUID))[0];
      if (version !== PROTOCOL_VERSION) {
        throw new Error(`Protocol v${version} on the watch, the app needs v${PROTOCOL_VERSION}. Update one of them.`);
      }
      const mtu = await this.transport.mtu();
      // Unknown MTU (browsers): ask for the largest chunks; onData shrinks them if the link cuts them short.
      this.chunkPayload = Math.max(14, Math.min(DEVICE_MAX_NOTIFY, mtu ? mtu - 3 : DEVICE_MAX_NOTIFY) - DATA_HEADER_BYTES);

      await this.transport.subscribe(EVENTS_UUID, (bytes) => this.messages.push(parseMessage(bytes)));
      await this.transport.subscribe(DATA_UUID, (bytes) => this.onData(bytes));

      this.status = 'connected';
      this.detail = `Connected to ${device.name}.`;
      this.say(`Connected to ${device.name} (protocol v${version}${mtu ? `, MTU ${mtu}` : ''}).`);
      await this.listEvents();

      while (this.linkUp && !this.stopRequested) {
        const next = this.toFetch.shift();
        if (next) {
          await this.downloadAndSave(next.id, next.size);
          continue;
        }
        const message = await Promise.race([this.messages.next(1000), disconnected.then(() => null)]);
        if (message) this.handleMessage(message);
      }
    } finally {
      this.transfer = null;
      this.buffer = null;
      this.fetchingId = null;
      if (this.linkUp) await this.transport.disconnect();
      this.linkUp = false;
      this.say('Disconnected.');
    }
  }

  private async command(bytes: Uint8Array) {
    await this.transport.write(CONTROL_UUID, bytes);
  }

  /** React to unsolicited messages; returns the message so callers can also wait on replies. */
  private handleMessage(message: WatchMessage): WatchMessage {
    switch (message.kind) {
      case 'started': {
        const alert = { eventId: message.id, trigger: message.trigger, at: new Date() };
        this.triggeredAt.set(message.id, alert.at);
        this.alert = alert;
        this.say(`ALERT: ${message.trigger} trigger, event ${message.id}. Recording ~50 s more.`);
        addActivity('Watch alert', `${describeTrigger(message.trigger, '')} trigger on the watch.`).catch(() => {});
        this.onEventStarted?.(alert);
        break;
      }
      case 'ready':
        this.say(`Event ${message.id} ready (${Math.round(message.size / 1024)} KB).`);
        this.queueFetch(message.id, message.size);
        break;
      case 'error':
        this.say(`Watch error: ${message.reason} (event ${message.id}).`);
        break;
    }
    return message;
  }

  private queueFetch(id: number, size: number) {
    if (id !== this.fetchingId && !this.toFetch.some((e) => e.id === id)) this.toFetch.push({ id, size });
  }

  private async listEvents() {
    await this.command(listCommand());
    while (this.linkUp) {
      const message = await this.messages.next(REPLY_TIMEOUT_MS);
      if (!message) {
        this.say('No reply to LIST.');
        return;
      }
      this.handleMessage(message);
      if (message.kind === 'info') {
        if (message.complete) this.queueFetch(message.id, message.size);
        else this.say(`Event ${message.id} is still recording.`);
      } else if (message.kind === 'listEnd') {
        if (message.count) this.say(`${message.count} event(s) stored on the watch.`);
        return;
      }
    }
  }

  private onData(bytes: Uint8Array) {
    const chunk = parseDataChunk(bytes);
    if (!chunk || !this.buffer || chunk.id !== this.fetchingId || chunk.offset !== this.expected) return;
    const room = this.buffer.byteLength - chunk.offset;
    const payload = chunk.payload.byteLength > room ? chunk.payload.subarray(0, room) : chunk.payload;
    this.buffer.set(payload, chunk.offset);
    this.expected += payload.byteLength;
    this.lastData = Date.now();

    if (payload.byteLength < this.chunkPayload && this.expected < this.buffer.byteLength) {
      // The link cut the notification short (smaller MTU than assumed): use that size from here on.
      this.chunkPayload = payload.byteLength;
      this.restartGet = true;
    }
    if (this.lastData - this.lastProgress > 150 || this.expected >= this.buffer.byteLength) {
      this.lastProgress = this.lastData;
      this.transfer = { eventId: chunk.id, received: this.expected, size: this.buffer.byteLength };
    }
  }

  /** Download one event, resuming from the last good byte whenever it stalls. */
  private async fetch(id: number, size: number): Promise<Uint8Array | null> {
    this.buffer = new Uint8Array(size);
    this.expected = 0;
    this.fetchingId = id;
    this.transfer = { eventId: id, received: 0, size };
    const started = Date.now();
    try {
      while (this.expected < size) {
        if (!this.linkUp) return null;
        this.lastData = Date.now();
        this.restartGet = false;
        await this.command(getCommand(id, this.expected, this.chunkPayload));
        while (this.expected < size && this.linkUp) {
          const message = await this.messages.next(250);
          if (message) {
            this.handleMessage(message);
            if (message.kind === 'done') break;
            if (message.kind === 'error') return null;
          }
          if (this.restartGet) break;
          if (Date.now() - this.lastData > TRANSFER_STALL_MS) {
            this.say(`Transfer stalled at ${this.expected} bytes, resuming.`);
            break;
          }
        }
      }
      const seconds = (Date.now() - started) / 1000;
      this.say(`Received event ${id}: ${Math.round(size / 1024)} KB in ${seconds.toFixed(1)} s.`);
      return this.buffer;
    } finally {
      this.buffer = null;
      this.fetchingId = null;
      this.transfer = null;
    }
  }

  private async downloadAndSave(id: number, size: number) {
    const blob = await this.fetch(id, size);
    if (!blob) return;
    try {
      const saved = await saveEvent(blob, this.triggeredAt.get(id) ?? null);
      this.triggeredAt.delete(id);
      this.lastSaved = saved;
      this.savedCount++;
      this.say(`Saved event ${id} (${saved.audioSeconds.toFixed(0)} s of audio).`);
      this.onEventSaved?.(saved);
    } catch (e) {
      // Keep it on the watch: it's fetched again next time.
      this.say(`Could not save event ${id}: ${e instanceof Error ? e.message : String(e)}`);
      return;
    }
    await this.command(deleteCommand(id));
    while (this.linkUp) {
      const message = await this.messages.next(REPLY_TIMEOUT_MS);
      if (!message) return;
      this.handleMessage(message);
      if (message.kind === 'deleted' || message.kind === 'error') return;
    }
  }
}

/** The app's one watch connection. */
export const watch = new WatchClient();
