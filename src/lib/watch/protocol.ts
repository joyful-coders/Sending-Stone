/**
 * The watch's BLE protocol (v2): UUIDs, commands and messages.
 *
 * The firmware side is hardware/src/ble/ble_protocol.h, documented in
 * hardware/BLE_PROTOCOL.md. Everything is little-endian.
 */

/** Service every watch advertises. Any board running the firmware is found by it, whatever its name or address. */
export const SERVICE_UUID = 'db118277-ac3c-4312-9c3f-8f0f77e70acc';
/** READ: 1 byte, the protocol version. */
export const VERSION_UUID = '48e4c40b-6e01-49f1-b99b-0e6e9bd77a16';
/** WRITE: commands to the watch. */
export const CONTROL_UUID = '5eb136f7-3b60-4ea6-a592-3b9778aed258';
/** NOTIFY: messages from the watch. */
export const EVENTS_UUID = '6c33adb5-05dd-4186-bc22-624ed92abb28';
/** NOTIFY: event bytes during a transfer, each chunk prefixed by {@link DATA_HEADER_BYTES}. */
export const DATA_UUID = '40d3f957-dded-4b7d-9eb2-f11db97dda09';

/** The firmware's default advertised name (boards can be renamed; scanning goes by service). */
export const DEFAULT_DEVICE_NAME = 'Nicla-Sensors';
export const PROTOCOL_VERSION = 2;

/** Largest data notification the watch sends (ble_config::kMaxDataNotifyBytes). */
export const DEVICE_MAX_NOTIFY = 240;
/** u16 event id + u32 offset before each data chunk's bytes. */
export const DATA_HEADER_BYTES = 6;

export const Command = {
  List: 0x01,
  Get: 0x02,
  Delete: 0x03,
  Cancel: 0x04,
  Trigger: 0x05
} as const;

export const Message = {
  EventStarted: 0x81,
  EventReady: 0x82,
  EventInfo: 0x83,
  ListEnd: 0x84,
  TransferDone: 0x85,
  Deleted: 0x86,
  Error: 0x87
} as const;

export const TRIGGER_NAMES: Record<number, string> = { 0: 'none', 1: 'keyword', 2: 'jolt', 3: 'manual', 4: 'button' };
export const ERROR_NAMES: Record<number, string> = { 1: 'bad command', 2: 'no such event', 3: 'storage error' };

export function triggerName(type: number): string {
  return TRIGGER_NAMES[type] ?? String(type);
}

/** A message from the events characteristic, decoded. */
export type WatchMessage =
  | { kind: 'started'; id: number; trigger: string; detail: number; deviceTimeMs: number }
  | { kind: 'ready'; id: number; size: number; trigger: string }
  | { kind: 'info'; id: number; size: number; trigger: string; complete: boolean }
  | { kind: 'listEnd'; count: number }
  | { kind: 'done'; id: number; size: number }
  | { kind: 'deleted'; id: number }
  | { kind: 'error'; code: number; reason: string; id: number }
  | { kind: 'unknown'; code: number };

export function parseMessage(bytes: Uint8Array): WatchMessage {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const code = bytes[0];
  try {
    switch (code) {
      case Message.EventStarted:
        return {
          kind: 'started',
          id: view.getUint16(1, true),
          trigger: triggerName(bytes[3]),
          detail: bytes[4],
          deviceTimeMs: view.getUint32(5, true)
        };
      case Message.EventReady:
        return { kind: 'ready', id: view.getUint16(1, true), size: view.getUint32(3, true), trigger: triggerName(bytes[7]) };
      case Message.EventInfo:
        return {
          kind: 'info',
          id: view.getUint16(1, true),
          size: view.getUint32(3, true),
          trigger: triggerName(bytes[7]),
          complete: bytes[8] !== 0
        };
      case Message.ListEnd:
        return { kind: 'listEnd', count: view.getUint16(1, true) };
      case Message.TransferDone:
        return { kind: 'done', id: view.getUint16(1, true), size: view.getUint32(3, true) };
      case Message.Deleted:
        return { kind: 'deleted', id: view.getUint16(1, true) };
      case Message.Error:
        return { kind: 'error', code: bytes[1], reason: ERROR_NAMES[bytes[1]] ?? `error ${bytes[1]}`, id: view.getUint16(2, true) };
    }
  } catch {
    // too short: fall through
  }
  return { kind: 'unknown', code };
}

export function listCommand(): Uint8Array {
  return Uint8Array.of(Command.List);
}

export function getCommand(id: number, offset: number, maxPayload: number): Uint8Array {
  const bytes = new Uint8Array(9);
  const view = new DataView(bytes.buffer);
  bytes[0] = Command.Get;
  view.setUint16(1, id, true);
  view.setUint32(3, offset, true);
  view.setUint16(7, maxPayload, true);
  return bytes;
}

export function deleteCommand(id: number): Uint8Array {
  const bytes = new Uint8Array(3);
  bytes[0] = Command.Delete;
  new DataView(bytes.buffer).setUint16(1, id, true);
  return bytes;
}

export function triggerCommand(): Uint8Array {
  return Uint8Array.of(Command.Trigger);
}

/** A data notification: which event, where in it, and the bytes. */
export function parseDataChunk(bytes: Uint8Array): { id: number; offset: number; payload: Uint8Array } | null {
  if (bytes.byteLength < DATA_HEADER_BYTES) return null;
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  return {
    id: view.getUint16(0, true),
    offset: view.getUint32(2, true),
    payload: bytes.subarray(DATA_HEADER_BYTES)
  };
}
