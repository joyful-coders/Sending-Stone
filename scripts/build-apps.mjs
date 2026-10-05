// Builds the app for testing and collects the results in release/:
//   release/sending-stone.exe         Windows app (runs without installing)
//   release/Sending Stone_*-setup.exe  Windows installer
//   release/sending-stone.apk         Android app for 64-bit ARM phones (optimized, signed with
//                                     the Android debug key: installs on any phone with
//                                     "install unknown apps" allowed)
//
// Usage:
//   npm run build              both
//   npm run build:windows      Windows only
//   npm run build:android      Android only
//
// Each build first builds the web app (npm run build:web, run by Tauri).

import { execSync } from 'node:child_process';
import { copyFileSync, existsSync, mkdirSync, readdirSync, statSync } from 'node:fs';
import { homedir } from 'node:os';
import { join } from 'node:path';

const root = join(import.meta.dirname, '..');
const releaseDir = join(root, 'release');
const which = process.argv[2] ?? 'all';
const problems = [];

function run(command) {
  console.log(`\n> ${command}`);
  execSync(command, { cwd: root, stdio: 'inherit' });
}

/** Newest file under `dir` (recursively) whose name matches `pattern`. */
function newest(dir, pattern) {
  let best = null;
  if (!existsSync(dir)) return null;
  for (const entry of readdirSync(dir, { withFileTypes: true, recursive: true })) {
    if (!entry.isFile() || !pattern.test(entry.name)) continue;
    const path = join(entry.parentPath ?? entry.path, entry.name);
    const time = statSync(path).mtimeMs;
    if (!best || time > best.time) best = { path, time };
  }
  return best?.path ?? null;
}

function collect(from, name) {
  if (!from) return false;
  mkdirSync(releaseDir, { recursive: true });
  copyFileSync(from, join(releaseDir, name));
  console.log(`  -> release/${name}`);
  return true;
}

function buildWindows() {
  run('npx tauri build');
  const target = join(root, 'src-tauri', 'target', 'release');
  collect(join(target, 'sending-stone.exe'), 'sending-stone.exe');
  const installer = newest(join(target, 'bundle', 'nsis'), /setup\.exe$/i);
  if (installer) collect(installer, installer.split(/[\\/]/).pop());
}

function androidPrerequisites() {
  const missing = [];
  const sdk = process.env.ANDROID_HOME || process.env.ANDROID_SDK_ROOT;
  if (!sdk || !existsSync(sdk)) missing.push('the Android SDK (install Android Studio, then set ANDROID_HOME)');
  const ndk = process.env.NDK_HOME || (sdk && existsSync(join(sdk, 'ndk')) ? join(sdk, 'ndk') : null);
  if (!ndk || !existsSync(ndk)) missing.push('the Android NDK (Android Studio > SDK Manager > SDK Tools > NDK, then set NDK_HOME)');
  if (!process.env.JAVA_HOME) missing.push('Java (set JAVA_HOME to Android Studio\'s "jbr" folder)');
  return missing;
}

function buildAndroid() {
  const missing = androidPrerequisites();
  if (missing.length) {
    problems.push(
      'Android build skipped. Missing:\n    - ' +
        missing.join('\n    - ') +
        '\n  Setup guide: https://v2.tauri.app/start/prerequisites/#android'
    );
    return;
  }
  // The Rust toolchain for 64-bit ARM phones (no-op once installed). That's every
  // phone from the last several years; building all four CPU types quadruples the
  // build time and size for phones nobody tests on.
  run('rustup target add aarch64-linux-android');
  // An optimized (release) build: a debug build is ~700 MB, this is ~20 MB.
  run('npx tauri android build --apk --target aarch64');
  const unsigned = newest(join(root, 'src-tauri', 'gen', 'android', 'app', 'build', 'outputs', 'apk'), /release.*\.apk$/i);
  if (!unsigned) {
    problems.push('Android build ran, but no release .apk was found.');
    return;
  }
  // Sign it with the Android debug key (made by the SDK, password "android") so any
  // phone installs it for testing. A Play Store release needs your own keystore.
  const sdk = process.env.ANDROID_HOME || process.env.ANDROID_SDK_ROOT;
  const buildTools = join(sdk, 'build-tools');
  const version = readdirSync(buildTools).sort().pop();
  const apksigner = join(buildTools, version, process.platform === 'win32' ? 'apksigner.bat' : 'apksigner');
  const keystore = join(homedir(), '.android', 'debug.keystore');
  if (!existsSync(keystore)) {
    run(
      `keytool -genkeypair -v -keystore "${keystore}" -storepass android -alias androiddebugkey -keypass android ` +
        `-keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US"`
    );
  }
  mkdirSync(releaseDir, { recursive: true });
  const out = join(releaseDir, 'sending-stone.apk');
  run(
    `"${apksigner}" sign --ks "${keystore}" --ks-pass pass:android --ks-key-alias androiddebugkey ` +
      `--key-pass pass:android --out "${out}" "${unsigned}"`
  );
  console.log('  -> release/sending-stone.apk');
}

try {
  if (which === 'all' || which === 'windows') buildWindows();
  if (which === 'all' || which === 'android') buildAndroid();
} catch (e) {
  problems.push(`Build failed: ${e.message.split('\n')[0]}`);
}

if (problems.length) {
  console.error('\n' + problems.join('\n\n'));
  process.exit(1);
}
console.log(`\nDone. Apps are in ${releaseDir}`);
