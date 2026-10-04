<script lang="ts">
  import { goto } from '$app/navigation';
  import { onMount } from 'svelte';
  import { getDB } from '$lib/db';

  type Counts = {
    plans: number;
    contacts: number;
    activity: number;
  };

  let counts: Counts = {
    plans: 0,
    contacts: 0,
    activity: 0
  };

  let message = '';
  let loading = true;
  let showClearConfirmation = false;

  function backToDashboard() {
    goto('/private');
  }

  async function loadCounts() {
    const db = await getDB();

    const planRows = await db.select<{ count: number }[]>(
      `SELECT COUNT(*) AS count FROM safety_plan`
    );

    const contactRows = await db.select<{ count: number }[]>(
      `SELECT COUNT(*) AS count FROM trusted_contacts`
    );

    const activityRows = await db.select<{ count: number }[]>(
      `SELECT COUNT(*) AS count FROM activity_events`
    );

    counts = {
      plans: planRows[0]?.count ?? 0,
      contacts: contactRows[0]?.count ?? 0,
      activity: activityRows[0]?.count ?? 0
    };
  }

  onMount(async () => {
    try {
      await loadCounts();
    } catch (error) {
      console.error('Could not load privacy data:', error);

      message = `Could not load local data summary: ${
        error instanceof Error ? error.message : String(error)
      }`;
    } finally {
      loading = false;
    }
  });

  function openClearConfirmation() {
    showClearConfirmation = true;
  }

  function closeClearConfirmation() {
    showClearConfirmation = false;
  }

  async function clearDemoData() {
    try {
      const db = await getDB();

      await db.execute(`DELETE FROM trusted_contacts`);
      await db.execute(`DELETE FROM activity_events`);

      await db.execute(`
        UPDATE safety_plan
        SET
          check_in_enabled = 1,
          check_in_interval = '30',
          grace_period = '5',
          alert_method = 'Text message',
          include_location = 0,
          updated_at = CURRENT_TIMESTAMP
        WHERE id = 1
      `);

      await loadCounts();

      message =
        'Demo data cleared. Your plan was reset to the default local settings.';

      closeClearConfirmation();
    } catch (error) {
      console.error('Could not clear demo data:', error);

      message = `Could not clear data: ${
        error instanceof Error ? error.message : String(error)
      }`;
    }
  }
</script>

<svelte:head>
  <title>Privacy & Data</title>
</svelte:head>

<main class="min-h-screen bg-slate-950 p-4 text-white sm:p-6">
  <section class="mx-auto max-w-2xl">
    <header class="flex items-start justify-between gap-4">
      <div>
        <p class="text-xs font-bold uppercase tracking-[0.2em] text-indigo-300">
          Privacy controls
        </p>

        <h1 class="mt-1 text-3xl font-bold">Privacy & data</h1>

        <p class="mt-2 max-w-xl text-sm leading-6 text-slate-300">
          Review the demo data stored locally in this desktop app.
        </p>
      </div>

      <button
        type="button"
        class="shrink-0 rounded-lg bg-slate-800 px-3 py-2 text-sm font-medium text-slate-100 hover:bg-slate-700"
        onclick={backToDashboard}
      >
        Back
      </button>
    </header>

    <section class="mt-6 rounded-2xl border border-slate-800 bg-slate-900 p-5">
      <h2 class="font-semibold">Where data is stored</h2>

      <p class="mt-2 text-sm leading-6 text-slate-400">
        This prototype stores its demo data locally on this device in an SQLite
        database. It does not upload your plan, contacts, or activity history
        to a cloud service.
      </p>

      <div class="mt-4 rounded-xl border border-slate-700 bg-slate-950 p-4">
        <p class="text-xs font-bold uppercase tracking-[0.16em] text-slate-500">
          Local database
        </p>

        <code class="mt-2 block break-all text-sm text-indigo-200">
          safety-plan.db
        </code>
      </div>
    </section>

    <section class="mt-4 rounded-2xl border border-slate-800 bg-slate-900 p-5">
      <h2 class="font-semibold">Stored demo data</h2>

      {#if loading}
        <p class="mt-4 text-sm text-slate-400">
          Loading local data summary…
        </p>
      {:else}
        <div class="mt-4 grid gap-3 sm:grid-cols-3">
          <div class="rounded-xl border border-slate-700 bg-slate-950 p-4">
            <p class="text-2xl font-bold text-indigo-300">{counts.plans}</p>
            <p class="mt-1 text-sm text-slate-400">
              Saved alert plan
            </p>
          </div>

          <div class="rounded-xl border border-slate-700 bg-slate-950 p-4">
            <p class="text-2xl font-bold text-indigo-300">{counts.contacts}</p>
            <p class="mt-1 text-sm text-slate-400">
              Trusted contacts
            </p>
          </div>

          <div class="rounded-xl border border-slate-700 bg-slate-950 p-4">
            <p class="text-2xl font-bold text-indigo-300">{counts.activity}</p>
            <p class="mt-1 text-sm text-slate-400">
              Activity events
            </p>
          </div>
        </div>
      {/if}
    </section>

    <section class="mt-4 rounded-2xl border border-rose-500/30 bg-rose-500/5 p-5">
      <h2 class="font-semibold text-rose-100">Clear demo data</h2>

      <p class="mt-2 text-sm leading-6 text-rose-100/70">
        This removes all locally saved trusted contacts and activity history.
        Your alert plan returns to the prototype defaults.
      </p>

      <button
        type="button"
        class="mt-4 rounded-xl border border-rose-400 px-4 py-3 text-sm font-semibold text-rose-200 hover:bg-rose-500/10"
        onclick={openClearConfirmation}
      >
        Clear local demo data
      </button>
    </section>

    {#if message}
      <p
        class="mt-4 rounded-xl border border-indigo-500/30 bg-indigo-500/10 p-4 text-sm leading-6 text-indigo-100"
        aria-live="polite"
      >
        {message}
      </p>
    {/if}

    <p class="mt-6 text-center text-xs leading-5 text-slate-500">
      Prototype disclosure: this demo uses local SQLite storage. Production
      safety data would require encryption, authentication, and a defined data-retention policy.
    </p>
  </section>
</main>

{#if showClearConfirmation}
  <button
    type="button"
    class="fixed inset-0 z-40 cursor-default bg-black/60"
    aria-label="Close clear-data confirmation"
    onclick={closeClearConfirmation}
  ></button>

  <dialog
    open
    class="fixed left-1/2 top-1/2 z-50 m-0 w-[calc(100%-2rem)] max-w-md -translate-x-1/2 -translate-y-1/2 rounded-2xl border-0 bg-white p-5 text-slate-900 shadow-2xl"
    aria-labelledby="clear-data-title"
  >
    <p class="text-xs font-bold uppercase tracking-[0.16em] text-rose-600">
      Confirm reset
    </p>

    <h2 id="clear-data-title" class="mt-1 text-xl font-bold">
      Clear local demo data?
    </h2>

    <p class="mt-4 text-sm leading-6 text-slate-600">
      This permanently removes trusted contacts and activity history from this
      device. The safety plan will reset to its prototype defaults.
    </p>

    <div class="mt-5 grid gap-3 sm:grid-cols-2">
      <button
        type="button"
        class="rounded-xl border border-slate-300 px-4 py-3 font-semibold text-slate-700 hover:bg-slate-100"
        onclick={closeClearConfirmation}
      >
        Cancel
      </button>

      <button
        type="button"
        class="rounded-xl bg-rose-600 px-4 py-3 font-semibold text-white hover:bg-rose-700"
        onclick={clearDemoData}
      >
        Clear data
      </button>
    </div>
  </dialog>
{/if}
