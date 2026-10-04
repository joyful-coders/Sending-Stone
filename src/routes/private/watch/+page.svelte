<script lang="ts">
  import { goto } from '$app/navigation';
  import { onDestroy } from 'svelte';
  import { watch } from '$lib/watch/watchClient.svelte';
  import { DEFAULT_DEVICE_NAME } from '$lib/watch/protocol';
  import {
    audioUrl,
    describeTrigger,
    listSavedEvents,
    sessionDownloads,
    type SavedEvent
  } from '$lib/watch/storage';

  let events = $state<SavedEvent[]>([]);
  let playing = $state<{ folder: string; url: string } | null>(null);
  let message = $state('');

  const statusLabel: Record<string, string> = {
    idle: 'Not connected',
    scanning: 'Searching',
    connecting: 'Connecting',
    connected: 'Connected',
    reconnecting: 'Reconnecting',
    error: 'Problem'
  };

  const statusColor: Record<string, string> = {
    idle: 'bg-slate-500',
    scanning: 'bg-amber-400',
    connecting: 'bg-amber-400',
    connected: 'bg-emerald-400',
    reconnecting: 'bg-amber-400',
    error: 'bg-rose-500'
  };

  // Reload the list whenever an event is saved (watch.savedCount changes).
  $effect(() => {
    void watch.savedCount;
    listSavedEvents()
      .then((list) => (events = list))
      .catch((e) => (message = `Could not load recordings: ${e instanceof Error ? e.message : String(e)}`));
  });

  async function play(event: SavedEvent) {
    stopPlaying();
    try {
      playing = { folder: event.folder, url: await audioUrl(event.folder) };
    } catch (e) {
      message = e instanceof Error ? e.message : String(e);
    }
  }

  function stopPlaying() {
    if (playing) URL.revokeObjectURL(playing.url);
    playing = null;
  }

  onDestroy(stopPlaying);

  function formatTime(iso: string) {
    const date = new Date(iso);
    return Number.isNaN(date.getTime()) ? iso : date.toLocaleString();
  }

  function percent(t: { received: number; size: number }) {
    return Math.floor((100 * t.received) / Math.max(t.size, 1));
  }
</script>

<svelte:head>
  <title>Watch</title>
</svelte:head>

<main class="min-h-screen bg-slate-950 p-4 text-white sm:p-6">
  <section class="mx-auto max-w-2xl">
    <header class="flex items-center justify-between gap-4">
      <div>
        <p class="text-xs font-bold uppercase tracking-[0.2em] text-indigo-300">Private area</p>
        <h1 class="mt-1 text-3xl font-bold">Watch</h1>
      </div>

      <button
        type="button"
        class="rounded-lg bg-slate-800 px-3 py-2 text-sm font-medium text-slate-100 hover:bg-slate-700"
        onclick={() => goto('/private')}
      >
        Back
      </button>
    </header>

    <!-- Connection -->
    <section class="mt-6 rounded-2xl border border-slate-800 bg-slate-900 p-5">
      <div class="flex items-start justify-between gap-4">
        <div>
          <p class="text-xs font-bold uppercase tracking-[0.16em] text-slate-400">Connection</p>
          <h2 class="mt-1 flex items-center gap-2 text-xl font-semibold">
            <span class="inline-block h-2.5 w-2.5 rounded-full {statusColor[watch.status]}"></span>
            {statusLabel[watch.status]}
          </h2>
          <p class="mt-1 text-sm text-slate-300">
            {watch.detail ||
              (watch.device
                ? `Uses ${watch.device.name}.`
                : 'Search for your watch to pair it with this app.')}
          </p>
        </div>
        <span class="shrink-0 rounded-lg bg-slate-800 px-2 py-1 text-xs text-slate-400">
          {watch.transport.kind === 'native' ? 'App Bluetooth' : 'Web Bluetooth'}
        </span>
      </div>

      {#if watch.transfer}
        <div class="mt-4">
          <div class="flex justify-between text-xs text-slate-400">
            <span>Downloading recording {watch.transfer.eventId}</span>
            <span>{percent(watch.transfer)}%</span>
          </div>
          <div class="mt-1 h-2 overflow-hidden rounded-full bg-slate-800">
            <div class="h-full bg-indigo-500 transition-all" style="width: {percent(watch.transfer)}%"></div>
          </div>
        </div>
      {/if}

      <div class="mt-5 flex flex-wrap gap-3">
        {#if watch.device && (watch.status === 'connected' || watch.status === 'connecting' || watch.status === 'reconnecting')}
          <button
            type="button"
            class="rounded-xl border border-slate-700 px-4 py-2 text-sm font-semibold hover:bg-slate-800"
            onclick={() => watch.stop()}
          >
            Disconnect
          </button>
          {#if watch.status === 'connected'}
            <button
              type="button"
              class="rounded-xl border border-slate-700 px-4 py-2 text-sm font-semibold hover:bg-slate-800"
              onclick={() => watch.sendTestTrigger()}
            >
              Test recording
            </button>
          {/if}
        {:else}
          {#if watch.device}
            <button
              type="button"
              class="rounded-xl bg-indigo-600 px-4 py-2 text-sm font-semibold hover:bg-indigo-700"
              onclick={() => (watch.transport.kind === 'web' ? watch.scan() : watch.start())}
            >
              Connect to {watch.device.name}
            </button>
          {/if}
          <button
            type="button"
            class="rounded-xl {watch.device
              ? 'border border-slate-700 hover:bg-slate-800'
              : 'bg-indigo-600 hover:bg-indigo-700'} px-4 py-2 text-sm font-semibold disabled:opacity-60"
            onclick={() => watch.scan()}
            disabled={watch.status === 'scanning'}
          >
            {watch.status === 'scanning' ? 'Searching…' : watch.device ? 'Use a different watch' : 'Find my watch'}
          </button>
        {/if}
        {#if watch.device}
          <button
            type="button"
            class="rounded-xl px-4 py-2 text-sm font-medium text-slate-400 hover:bg-slate-800"
            onclick={() => watch.forget()}
          >
            Forget watch
          </button>
        {/if}
      </div>

      {#if watch.transport.scanShowsList && (watch.status === 'scanning' || watch.found.length > 0) && watch.status !== 'connected'}
        <ul class="mt-4 grid gap-2">
          {#each watch.found as device (device.id)}
            <li>
              <button
                type="button"
                class="flex w-full items-center justify-between rounded-xl border border-slate-800 bg-slate-950 p-3 text-left hover:border-indigo-400"
                onclick={() => watch.use(device)}
              >
                <span>
                  <span class="block font-medium">{device.name}</span>
                  <span class="block text-xs text-slate-500">{device.id}</span>
                </span>
                {#if device.rssi !== null}
                  <span class="text-xs text-slate-400">{device.rssi} dBm</span>
                {/if}
              </button>
            </li>
          {:else}
            <li class="text-sm text-slate-400">Searching for watches nearby…</li>
          {/each}
        </ul>
      {/if}
    </section>

    {#if message}
      <p class="mt-4 rounded-xl border border-indigo-500/30 bg-indigo-500/10 p-4 text-sm text-indigo-100">{message}</p>
    {/if}

    <!-- Recordings -->
    <section class="mt-7">
      <p class="text-xs font-bold uppercase tracking-[0.16em] text-slate-400">Recordings</p>
      <h2 class="mt-1 text-xl font-semibold">From the watch</h2>

      {#if events.length === 0}
        <div class="mt-3 rounded-2xl border border-dashed border-slate-700 bg-slate-900 p-5">
          <p class="font-medium">No recordings yet</p>
          <p class="mt-1 text-sm text-slate-400">
            When the watch is triggered (a jolt, the keyword, or a test), it records the 10 seconds before and
            50 seconds after, then sends it here.
          </p>
        </div>
      {:else}
        <ul class="mt-3 grid gap-3">
          {#each events as event (event.folder)}
            <li class="rounded-2xl border border-slate-800 bg-slate-900 p-4">
              <div class="flex items-start justify-between gap-4">
                <div>
                  <p class="font-semibold">{describeTrigger(event.trigger, event.label)}</p>
                  <p class="mt-1 text-sm text-slate-400">
                    {event.audioSeconds.toFixed(0)} s audio • {event.motionSamples} motion samples
                    {#if event.triggerCount > 1}• {event.triggerCount} triggers{/if}
                  </p>
                </div>
                <time class="shrink-0 text-right text-xs text-slate-500">{formatTime(event.triggeredAt)}</time>
              </div>

              {#if playing?.folder === event.folder}
                <!-- svelte-ignore a11y_media_has_caption -->
                <audio class="mt-3 w-full" src={playing.url} controls autoplay></audio>
              {:else}
                <button
                  type="button"
                  class="mt-3 rounded-lg bg-slate-800 px-3 py-1.5 text-sm hover:bg-slate-700"
                  onclick={() => play(event)}
                >
                  ▶ Play audio
                </button>
              {/if}

              {#if watch.transport.kind === 'web'}
                <div class="mt-2 flex flex-wrap gap-3 text-xs">
                  {#each sessionDownloads(event.folder) as file (file.name)}
                    <a class="text-indigo-300 underline" href={file.url} download="{event.folder}_{file.name}">{file.name}</a>
                  {/each}
                </div>
              {/if}
            </li>
          {/each}
        </ul>
      {/if}
    </section>

    <!-- Log -->
    <details class="mt-7 rounded-2xl border border-slate-800 bg-slate-900 p-4">
      <summary class="cursor-pointer text-sm font-semibold text-slate-300">Connection log</summary>
      {#if watch.log.length === 0}
        <p class="mt-2 text-xs text-slate-500">Nothing yet.</p>
      {:else}
        <pre class="mt-2 max-h-64 overflow-auto whitespace-pre-wrap text-xs leading-5 text-slate-400">{watch.log.join('\n')}</pre>
      {/if}
    </details>

    <p class="mt-7 text-center text-xs leading-5 text-slate-500">
      Works with any watch running the {DEFAULT_DEVICE_NAME} firmware. Recordings stay on this device.
    </p>
  </section>
</main>
