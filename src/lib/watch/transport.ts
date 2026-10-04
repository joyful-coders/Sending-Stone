/**
 * BLE access for the watch client, on whatever the app runs on:
 *
 * - In the Tauri app (the Android APK, and desktop builds) through the native
 *   tauri-plugin-blec plugin. The Android WebView has no Web Bluetooth, so
 *   this is the only way BLE works in the APK.
 * - In a browser (Chrome/Edge on desktop or Android, during `npm run dev`)
 *   through Web Bluetooth.
 *
 * Both find boards by the watch's service UUID, so any board running the
 * firmware works, whatever its name or address.
 */

import { isTauri } from '@tauri-apps/api/core';
import * as blec from '@mnlphlp/plugin-blec';
import { DEFAULT_DEVICE_NAME, SERVICE_UUID } from './protocol';

export type FoundDevice = {
  /** Stable id to reconnect with: the BLE address (native) or the browser's device id. */
  id: string;
  name: string;
  /** Signal strength in dBm, if known (native scans only). */
  rssi: number | null;
};

export interface BleTransport {
  readonly kind: 'native' | 'web';
  /**
   * Whether the scan shows a live list (native), or the system picker chooses
   * one board (web, which can only show a picker after a tap).
   */
  readonly scanShowsList: boolean;
  /** Whether Bluetooth can be used at all here (permissions asked for if needed). */
  prepare(): Promise<string | null>;
  /** Look for watches. Native: calls `onFound` with the list as it grows. Web: opens the picker. */
  scan(onFound: (devices: FoundDevice[]) => void, timeoutMs: number): Promise<void>;
  stopScan(): Promise<void>;
  connect(deviceId: string, onDisconnect: () => void): Promise<void>;
  disconnect(): Promise<void>;
  read(characteristic: string): Promise<Uint8Array>;
  write(characteristic: string, data: Uint8Array): Promise<void>;
  subscribe(characteristic: string, onValue: (data: Uint8Array) => void): Promise<void>;
  /** The negotiated ATT MTU, or null if the platform doesn't say. */
  mtu(): Promise<number | null>;
}

/** Whether a scanned device is a watch: it advertises the service, or (if the OS hides services) has the default name. */
function isWatch(name: string, services: string[]): boolean {
  return services.some((s) => s.toLowerCase() === SERVICE_UUID) || name === DEFAULT_DEVICE_NAME;
}

class NativeTransport implements BleTransport {
  readonly kind = 'native' as const;
  readonly scanShowsList = true;

  async prepare(): Promise<string | null> {
    if (!(await blec.checkPermissions(true))) return 'Bluetooth permission was not granted.';
    if ((await blec.getAdapterState()) === 'Off') return 'Bluetooth is turned off.';
    // Ask Android for the largest MTU the watch supports, so chunks are 234 bytes instead of 14.
    await blec.setAndroidMtu(247).catch(() => {});
    return null;
  }

  async scan(onFound: (devices: FoundDevice[]) => void, timeoutMs: number): Promise<void> {
    await blec.startScan((devices) => {
      onFound(
        devices
          .filter((d) => isWatch(d.name, d.services))
          .map((d) => ({ id: d.address, name: d.name || DEFAULT_DEVICE_NAME, rssi: d.rssi }))
      );
    }, timeoutMs);
  }

  async stopScan(): Promise<void> {
    await blec.stopScan().catch(() => {});
  }

  async connect(deviceId: string, onDisconnect: () => void): Promise<void> {
    await blec.connect(deviceId, onDisconnect);
  }

  async disconnect(): Promise<void> {
    await blec.disconnect().catch(() => {});
  }

  async read(characteristic: string): Promise<Uint8Array> {
    return Uint8Array.from(await blec.read(characteristic, SERVICE_UUID));
  }

  async write(characteristic: string, data: Uint8Array): Promise<void> {
    await blec.send(characteristic, Array.from(data), 'withResponse', SERVICE_UUID);
  }

  async subscribe(characteristic: string, onValue: (data: Uint8Array) => void): Promise<void> {
    await blec.subscribe(characteristic, SERVICE_UUID, (data) => onValue(Uint8Array.from(data)));
  }

  async mtu(): Promise<number | null> {
    return blec.getMtu().catch(() => null);
  }
}

class WebTransport implements BleTransport {
  readonly kind = 'web' as const;
  readonly scanShowsList = false;
  private devices = new Map<string, BluetoothDevice>();
  private device: BluetoothDevice | null = null;
  private service: BluetoothRemoteGATTService | null = null;
  private characteristics = new Map<string, BluetoothRemoteGATTCharacteristic>();
  private onDisconnect: (() => void) | null = null;

  async prepare(): Promise<string | null> {
    if (!('bluetooth' in navigator)) return 'This browser has no Web Bluetooth. Use Chrome or Edge, or the app.';
    if (!(await navigator.bluetooth.getAvailability())) return 'Bluetooth is turned off or unavailable.';
    return null;
  }

  async scan(onFound: (devices: FoundDevice[]) => void): Promise<void> {
    // The browser shows its own picker, filtered to boards advertising the watch's service.
    const device = await navigator.bluetooth.requestDevice({
      filters: [{ services: [SERVICE_UUID] }, { name: DEFAULT_DEVICE_NAME }],
      optionalServices: [SERVICE_UUID]
    });
    this.devices.set(device.id, device);
    onFound([{ id: device.id, name: device.name || DEFAULT_DEVICE_NAME, rssi: null }]);
  }

  async stopScan(): Promise<void> {}

  async connect(deviceId: string, onDisconnect: () => void): Promise<void> {
    let device = this.devices.get(deviceId) ?? null;
    if (!device && 'getDevices' in navigator.bluetooth) {
      // Boards this site was allowed before (Chrome keeps them across reloads where supported).
      const known = await navigator.bluetooth.getDevices();
      device = known.find((d) => d.id === deviceId) ?? null;
    }
    if (!device) throw new Error('Pick the watch again: the browser needs a tap to allow it.');

    this.device = device;
    this.onDisconnect = onDisconnect;
    device.removeEventListener('gattserverdisconnected', this.handleDisconnect);
    device.addEventListener('gattserverdisconnected', this.handleDisconnect);
    const server = await device.gatt!.connect();
    this.service = await server.getPrimaryService(SERVICE_UUID);
    this.characteristics.clear();
  }

  private handleDisconnect = () => {
    this.service = null;
    this.characteristics.clear();
    this.onDisconnect?.();
  };

  async disconnect(): Promise<void> {
    this.device?.gatt?.disconnect();
  }

  private async characteristic(uuid: string): Promise<BluetoothRemoteGATTCharacteristic> {
    if (!this.service) throw new Error('Not connected.');
    let c = this.characteristics.get(uuid);
    if (!c) {
      c = await this.service.getCharacteristic(uuid);
      this.characteristics.set(uuid, c);
    }
    return c;
  }

  async read(characteristic: string): Promise<Uint8Array> {
    const value = await (await this.characteristic(characteristic)).readValue();
    return new Uint8Array(value.buffer.slice(value.byteOffset, value.byteOffset + value.byteLength));
  }

  async write(characteristic: string, data: Uint8Array): Promise<void> {
    await (await this.characteristic(characteristic)).writeValueWithResponse(data.slice()); // a copy on a plain ArrayBuffer
  }

  async subscribe(characteristic: string, onValue: (data: Uint8Array) => void): Promise<void> {
    const c = await this.characteristic(characteristic);
    c.addEventListener('characteristicvaluechanged', () => {
      const value = c.value!;
      onValue(new Uint8Array(value.buffer.slice(value.byteOffset, value.byteOffset + value.byteLength)));
    });
    await c.startNotifications();
  }

  async mtu(): Promise<number | null> {
    return null; // Web Bluetooth doesn't expose it; Chrome negotiates the largest the device allows
  }
}

let transport: BleTransport | null = null;

/** The transport for this platform: native in the Tauri app, Web Bluetooth in a browser. */
export function getTransport(): BleTransport {
  transport ??= isTauri() ? new NativeTransport() : new WebTransport();
  return transport;
}
