<script lang="ts">
  import { goto } from '$app/navigation';

  let showTestAlert = false;
  let message = '';

  const recentActivity = [
    {
      id: 1,
      title: 'Watch paired',
      detail: 'Connected to this phone',
      time: 'Just now'
    },
    {
      id: 2,
      title: 'Check-in dismissed',
      detail: 'Prototype event — no alert was sent',
      time: 'Earlier today'
    }
  ];

  function returnToChat() {
    goto('/');
  }

  function openContacts() {
    goto('/private/contacts');
  }

  function openAlertPlan() {
    goto('/private/plan');
  }

  function showPrototypeMessage(text: string) {
    message = text;
  }

  function openTestAlert() {
    showTestAlert = true;
  }

  function closeTestAlert() {
    showTestAlert = false;
  }

  function handleKeydown(event: KeyboardEvent) {
    if (event.key === 'Escape') {
      if (showTestAlert) {
        closeTestAlert();
        return;
      }

      returnToChat();
    }
  }
</script>

<svelte:window onkeydown={handleKeydown} />

<svelte:head>
  <title>Dashboard</title>
</svelte:head>

<main class="min-h-screen bg-slate-950 p-4 text-white sm:p-6">
  <section class="mx-auto max-w-2xl">
    <header class="flex items-center justify-between gap-4">
      <div>
        <p class="text-xs font-bold uppercase tracking-[0.2em] text-indigo-300">
          Private area
        </p>
        <h1 class="mt-1 text-3xl font-bold">Dashboard</h1>
      </div>

      <button
        type="button"
        class="rounded-lg bg-slate-800 px-3 py-2 text-sm font-medium text-slate-100 hover:bg-slate-700"
        onclick={returnToChat}
      >
        Return
      </button>
    </header>

    <section class="mt-6 rounded-2xl border border-slate-800 bg-slate-900 p-5">
      <div class="flex items-start justify-between gap-4">
        <div>
          <p class="text-xs font-bold uppercase tracking-[0.16em] text-slate-400">
            Watch
          </p>
          <h2 class="mt-1 text-xl font-semibold">Connected</h2>
          <p class="mt-1 text-sm text-slate-300">
            Monitoring active • Last sync: just now
          </p>
        </div>

        <div class="rounded-xl bg-slate-800 px-3 py-2 text-right">
          <p class="text-xs text-slate-400">Battery</p>
          <p class="text-sm font-bold text-emerald-300">82%</p>
        </div>
      </div>
    </section>

    <section class="mt-5 grid grid-cols-1 gap-3 sm:grid-cols-2">
      <button
        type="button"
        class="rounded-2xl border border-slate-800 bg-slate-900 p-5 text-left transition hover:border-indigo-400 hover:bg-slate-800"
        onclick={openContacts}
      >
        <span class="text-2xl" aria-hidden="true">👥</span>
        <span class="mt-4 block font-semibold">Trusted contacts</span>
        <span class="mt-1 block text-sm text-slate-400">
          Add and manage alert recipients
        </span>
      </button>

      <button
        type="button"
        class="rounded-2xl border border-slate-800 bg-slate-900 p-5 text-left transition hover:border-indigo-400 hover:bg-slate-800"
        onclick={openAlertPlan}
      >
        <span class="text-2xl" aria-hidden="true">🛡️</span>
        <span class="mt-4 block font-semibold">Alert plan</span>
        <span class="mt-1 block text-sm text-slate-400">
          Configure check-ins and escalation
        </span>
      </button>

      <button
        type="button"
        class="rounded-2xl border border-slate-800 bg-slate-900 p-5 text-left transition hover:border-indigo-400 hover:bg-slate-800"
        onclick={openTestAlert}
      >
        <span class="text-2xl" aria-hidden="true">✉️</span>
        <span class="mt-4 block font-semibold">Test alert</span>
        <span class="mt-1 block text-sm text-slate-400">
          Preview the alert flow safely
        </span>
      </button>

      <button
        type="button"
        class="rounded-2xl border border-slate-800 bg-slate-900 p-5 text-left transition hover:border-indigo-400 hover:bg-slate-800"
        onclick={() =>
          showPrototypeMessage(
            'Privacy & data settings are a prototype. No data is being stored or shared.'
          )}
      >
        <span class="text-2xl" aria-hidden="true">⚙️</span>
        <span class="mt-4 block font-semibold">Privacy & data</span>
        <span class="mt-1 block text-sm text-slate-400">
          Review permissions and data use
        </span>
      </button>
    </section>

    {#if message}
      <p
        class="mt-4 rounded-xl border border-indigo-500/30 bg-indigo-500/10 p-3 text-sm text-indigo-100"
        aria-live="polite"
      >
        {message}
      </p>
    {/if}

    <section class="mt-7">
      <div class="flex items-center justify-between gap-3">
        <div>
          <p class="text-xs font-bold uppercase tracking-[0.16em] text-slate-400">
            Activity
          </p>
          <h2 class="mt-1 text-xl font-semibold">Recent events</h2>
        </div>

        <button
          type="button"
          class="rounded-lg px-3 py-2 text-sm font-medium text-slate-300 hover:bg-slate-800"
          onclick={() =>
            showPrototypeMessage(
              'Alert history would be managed here. This prototype has no saved event data.'
            )}
        >
          View all
        </button>
      </div>

      <ul class="mt-3 grid gap-3">
        {#each recentActivity as event (event.id)}
          <li class="rounded-2xl border border-slate-800 bg-slate-900 p-4">
            <div class="flex items-start justify-between gap-4">
              <div>
                <p class="font-semibold">{event.title}</p>
                <p class="mt-1 text-sm text-slate-400">{event.detail}</p>
              </div>

              <time class="shrink-0 text-xs text-slate-500">{event.time}</time>
            </div>
          </li>
        {/each}
      </ul>
    </section>

    <p class="mt-7 text-center text-xs leading-5 text-slate-500">
      Prototype only. No alerts, calls, messages, location sharing, microphone
      access, or data uploads are active.
    </p>
  </section>
</main>

{#if showTestAlert}
  <dialog
    open
    class="fixed left-1/2 top-1/2 z-50 m-0 w-[calc(100%-2rem)] max-w-md -translate-x-1/2 -translate-y-1/2 rounded-2xl border-0 bg-white p-5 text-slate-900 shadow-2xl"
    aria-labelledby="test-alert-title"
  >
    <p class="text-xs font-bold uppercase tracking-[0.16em] text-indigo-600">
      Test alert
    </p>

    <h2 id="test-alert-title" class="mt-1 text-xl font-bold">
      Preview only
    </h2>

    <p class="mt-4 rounded-xl bg-slate-100 p-4 text-sm leading-6">
      This is where the user would review their alert message before any
      actual contact is notified.
    </p>

    <p class="mt-3 text-sm leading-6 text-slate-600">
      No text message, call, location, or emergency request is sent by this
      prototype.
    </p>

    <button
      type="button"
      class="mt-5 w-full rounded-xl bg-indigo-600 px-4 py-3 font-semibold text-white hover:bg-indigo-700"
      onclick={closeTestAlert}
    >
      Close
    </button>
  </dialog>

  <div
    class="fixed inset-0 z-40 bg-black/60"
    role="presentation"
    onclick={closeTestAlert}
  ></div>
{/if}
