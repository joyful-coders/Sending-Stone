# Tauri + SvelteKit + TypeScript

This template should help get you started developing with Tauri, SvelteKit and TypeScript in Vite.

## Watch (Bluetooth)

The app talks to the wrist-worn recorder in [`hardware/`](hardware/) over BLE (protocol in [`hardware/BLE_PROTOCOL.md`](hardware/BLE_PROTOCOL.md)). It's the app version of [`hardware-ble-test/ble_client.py`](hardware-ble-test/ble_client.py): it stays connected, raises an alert as soon as the watch reports a trigger, then downloads each recording, saves it and deletes it from the watch.

- **Any board works.** The app scans for the watch's service UUID, so any board running the firmware can be chosen on the Watch page (`/private/watch`). The chosen board is remembered and reconnected to automatically.
- **Bluetooth in the app** (Android APK, desktop) uses the native [`tauri-plugin-blec`](https://github.com/MnlPhlp/tauri-plugin-blec) plugin, because the Android WebView has no Web Bluetooth. **In a browser** (`npm run dev` in Chrome or Edge) it uses Web Bluetooth instead.
- **Recordings** go to the app's private data folder, `events/<time>_event<id>_<trigger>/` (`audio.wav`, `motion.csv`, `meta.json`, `event.bin`), listed in the `watch_events` table. In a browser they're kept for the session, with download links.

Code: [`src/lib/watch/`](src/lib/watch/) (`protocol.ts` the BLE protocol, `eventFormat.ts` decoding, `transport.ts` native/web BLE, `storage.ts` saving, `watchClient.svelte.ts` the connection).

The plugin adds the Bluetooth permissions; the app asks for them the first time it searches.

## Building

```bash
npm run build           # Windows app + Android APK, collected in release/
npm run build:windows   # release/untitled.exe (runs as is) and the installer
npm run build:android   # release/untitled-android.apk (debug-signed: installs on any phone for testing)
npm run build:web       # just the web files (build/), which Tauri packages
```

Android needs Android Studio's SDK and NDK, with `ANDROID_HOME`, `NDK_HOME` and `JAVA_HOME` set ([Tauri Android prerequisites](https://v2.tauri.app/start/prerequisites/#android)); without them the script still builds Windows and says what's missing. The Rust Android targets are added automatically.

The `api/` routes (SMS, transcription) read their keys from `.env` when called. They only run under `npm run dev`; the built apps have no server.

## Recommended IDE Setup

[VS Code](https://code.visualstudio.com/) + [Svelte](https://marketplace.visualstudio.com/items?itemName=svelte.svelte-vscode) + [Tauri](https://marketplace.visualstudio.com/items?itemName=tauri-apps.tauri-vscode) + [rust-analyzer](https://marketplace.visualstudio.com/items?itemName=rust-lang.rust-analyzer).
