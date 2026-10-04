<script lang="ts">
  import { goto } from '$app/navigation';
  import { onMount } from 'svelte';
  import { addActivity, getDB } from '$lib/db';

  type PlanRow = {
    check_in_enabled: number;
    check_in_interval: string;
    grace_period: string;
    alert_method: string;
    include_location: number;
    updated_at: string;
  };

  let checkInEnabled = true;
  let checkInInterval = '30';
  let gracePeriod = '5';
  let alertMethod = 'Text message';
  let includeLocation = false;

  let message = '';
  let savedPlanSummary = '';
  let savedAt = '';
  let loading = true;

  const intervals = [
    { value: '15', label: 'Every 15 minutes' },
    { value: '30', label: 'Every 30 minutes' },
    { value: '60', label: 'Every hour' },
    { value: '120', label: 'Every 2 hours' }
  ];

  const gracePeriods = [
    { value: '2', label: '2 minutes' },
    { value: '5', label: '5 minutes' },
    { value: '10', label: '10 minutes' },
    { value: '15', label: '15 minutes' }
  ];

  function backToDashboard() {
    goto('/private');
  }

  function buildPlanSummary() {
    const checkInText = checkInEnabled
      ? `Check in every ${checkInInterval} minutes.`
      : 'Scheduled check-ins are off.';

    const responseText = `After a missed check-in, wait ${gracePeriod} minutes, then use ${alertMethod.toLowerCase()}.`;

    const locationText = includeLocation
      ? 'Location is included in the demo preview.'
      : 'Location is not included in the demo preview.';

    return `${checkInText} ${responseText} ${locationText}`;
  }

  function formatDate(value: string) {
    if (!value) return '';

    const date = new Date(value.replace(' ', 'T') + 'Z');

    if (Number.isNaN(date.getTime())) {
      return value;
    }

    return date.toLocaleString();
  }

  async function loadPlan() {
    const db = await getDB();

    const plans = await db.select<PlanRow[]>(`
      SELECT
        check_in_enabled,
        check_in_interval,
        grace_period,
        alert_method,
        include_location,
        updated_at
      FROM safety_plan
      WHERE id = 1
    `);

    const plan = plans[0];

    if (!plan) {
      return;
    }

    checkInEnabled = plan.check_in_enabled === 1;
    checkInInterval = plan.check_in_interval;
    gracePeriod = plan.grace_period;
    alertMethod = plan.alert_method;
    includeLocation = plan.include_location === 1;
    savedAt = plan.updated_at;
    savedPlanSummary = buildPlanSummary();
  }

  onMount(async () => {
    try {
      await loadPlan();
    } catch (error) {
      console.error('Could not load safety plan:', error);

      message = `Could not load saved plan: ${
        error instanceof Error ? error.message : String(error)
      }`;
    } finally {
      loading = false;
    }
  });

  async function savePlan() {
    try {
      const db = await getDB();

      await db.execute(
        `
          UPDATE safety_plan
          SET
            check_in_enabled = $1,
            check_in_interval = $2,
            grace_period = $3,
            alert_method = $4,
            include_location = $5,
            updated_at = CURRENT_TIMESTAMP
          WHERE id = 1
        `,
        [
          checkInEnabled ? 1 : 0,
          checkInInterval,
          gracePeriod,
          alertMethod,
          includeLocation ? 1 : 0
        ]
      );

      await addActivity(
        'Alert plan updated',
        checkInEnabled
          ? `Check-ins every ${checkInInterval} minutes. Missed check-ins wait ${gracePeriod} minutes before ${alertMethod.toLowerCase()}.`
          : 'Check-ins were turned off.'
      );

      savedPlanSummary = buildPlanSummary();
      savedAt = new Date().toISOString();
      message = 'Plan saved locally on this device.';
    } catch (error) {
      console.error('Could not save safety plan:', error);

      message = `Could not save plan: ${
        error instanceof Error ? error.message : String(error)
      }`;
    }
  }

  function previewPlan() {
    const locationText = includeLocation
      ? 'Location would be included in this demo alert.'
      : 'Location would not be included in this demo alert.';

    const checkInText = checkInEnabled
      ? `A check-in is due every ${checkInInterval} minutes.`
      : 'Scheduled check-ins are currently off.';

    message = `${checkInText} After a missed check-in, the app waits ${gracePeriod} minutes, then prepares a ${alertMethod.toLowerCase()} alert. ${locationText}`;
  }
</script>

<svelte:head>
  <title>Alert Plan</title>
</svelte:head>

<main class="min-h-screen bg-slate-950 p-4 text-white sm:p-6">
  <section class="mx-auto max-w-2xl">
    <header class="flex items-start justify-between gap-4">
      <div>
        <p class="text-xs font-bold uppercase tracking-[0.2em] text-indigo-300">
          Safety plan
        </p>

        <h1 class="mt-1 text-3xl font-bold">Alert plan</h1>

        <p class="mt-2 max-w-xl text-sm leading-6 text-slate-300">
          Set the timing and preview what would happen after a missed check-in.
          Saving stores this plan locally on this device.
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

    {#if loading}
      <p class="mt-6 rounded-xl border border-slate-800 bg-slate-900 p-4 text-sm text-slate-400">
        Loading saved plan…
      </p>
    {:else}
      <section class="mt-6 rounded-2xl border border-slate-800 bg-slate-900 p-5">
        <div class="flex items-start justify-between gap-4">
          <div>
            <h2 class="font-semibold">Check-ins</h2>

            <p class="mt-1 text-sm leading-6 text-slate-400">
              Ask the user to confirm they are okay on a regular schedule.
            </p>
          </div>

          <button
            type="button"
            class={`relative h-8 w-14 shrink-0 rounded-full transition ${
              checkInEnabled ? 'bg-indigo-600' : 'bg-slate-700'
            }`}
            aria-pressed={checkInEnabled}
            aria-label="Toggle check-ins"
            onclick={() => (checkInEnabled = !checkInEnabled)}
          >
            <span
              class={`absolute left-1 top-1 h-6 w-6 rounded-full bg-white transition-transform ${
                checkInEnabled ? 'translate-x-6' : 'translate-x-0'
              }`}
            ></span>
          </button>
        </div>

        {#if checkInEnabled}
          <div class="mt-5 grid gap-4 sm:grid-cols-2">
            <label class="block">
              <span class="text-sm font-medium text-slate-200">
                Check-in interval
              </span>

              <select
                bind:value={checkInInterval}
                class="mt-2 w-full rounded-xl border border-slate-700 bg-slate-950 px-3 py-3 text-sm text-white outline-none focus:border-indigo-400"
              >
                {#each intervals as interval}
                  <option value={interval.value}>{interval.label}</option>
                {/each}
              </select>
            </label>

            <label class="block">
              <span class="text-sm font-medium text-slate-200">
                Grace period
              </span>

              <select
                bind:value={gracePeriod}
                class="mt-2 w-full rounded-xl border border-slate-700 bg-slate-950 px-3 py-3 text-sm text-white outline-none focus:border-indigo-400"
              >
                {#each gracePeriods as period}
                  <option value={period.value}>{period.label}</option>
                {/each}
              </select>
            </label>
          </div>
        {:else}
          <p class="mt-5 rounded-xl border border-slate-700 bg-slate-950 p-4 text-sm text-slate-400">
            Check-ins are disabled. You can still edit the escalation settings below.
          </p>
        {/if}
      </section>

      <section class="mt-4 rounded-2xl border border-slate-800 bg-slate-900 p-5">
        <h2 class="font-semibold">If a check-in is missed</h2>

        <p class="mt-1 text-sm leading-6 text-slate-400">
          Choose the action the prototype would use after the grace period ends.
        </p>

        <fieldset class="mt-5 grid gap-3">
          <legend class="sr-only">Alert method</legend>

          <label
            class="flex cursor-pointer items-center gap-3 rounded-xl border border-slate-700 bg-slate-950 p-4 hover:border-indigo-400"
          >
            <input
              type="radio"
              name="alert-method"
              value="Text message"
              bind:group={alertMethod}
              class="h-4 w-4 accent-indigo-500"
            />

            <span>
              <span class="block font-medium">Text message</span>

              <span class="mt-1 block text-sm text-slate-400">
                Draft a message for trusted contacts.
              </span>
            </span>
          </label>

          <label
            class="flex cursor-pointer items-center gap-3 rounded-xl border border-slate-700 bg-slate-950 p-4 hover:border-indigo-400"
          >
            <input
              type="radio"
              name="alert-method"
              value="Phone call"
              bind:group={alertMethod}
              class="h-4 w-4 accent-indigo-500"
            />

            <span>
              <span class="block font-medium">Phone call</span>

              <span class="mt-1 block text-sm text-slate-400">
                Show a phone-call escalation flow in the prototype.
              </span>
            </span>
          </label>

          <label
            class="flex cursor-pointer items-center gap-3 rounded-xl border border-slate-700 bg-slate-950 p-4 hover:border-indigo-400"
          >
            <input
              type="radio"
              name="alert-method"
              value="In-app alert"
              bind:group={alertMethod}
              class="h-4 w-4 accent-indigo-500"
            />

            <span>
              <span class="block font-medium">In-app alert</span>

              <span class="mt-1 block text-sm text-slate-400">
                Keep the alert inside the prototype only.
              </span>
            </span>
          </label>
        </fieldset>

        <label
          class="mt-5 flex cursor-pointer items-start gap-3 rounded-xl border border-slate-700 bg-slate-950 p-4 hover:border-indigo-400"
        >
          <input
            type="checkbox"
            bind:checked={includeLocation}
            class="mt-1 h-4 w-4 accent-indigo-500"
          />

          <span>
            <span class="block font-medium">
              Include location in preview
            </span>

            <span class="mt-1 block text-sm leading-6 text-slate-400">
              This only changes the on-screen preview. It does not request,
              collect, store, or share location data.
            </span>
          </span>
        </label>
      </section>

      {#if message}
        <p
          class="mt-4 rounded-xl border border-indigo-500/30 bg-indigo-500/10 p-4 text-sm leading-6 text-indigo-100"
          aria-live="polite"
        >
          {message}
        </p>
      {/if}

      {#if savedPlanSummary}
        <section class="mt-4 rounded-2xl border border-emerald-500/30 bg-emerald-500/10 p-5">
          <p class="text-xs font-bold uppercase tracking-[0.16em] text-emerald-300">
            Saved plan
          </p>

          <p class="mt-2 text-sm leading-6 text-emerald-50">
            {savedPlanSummary}
          </p>

          {#if savedAt}
            <p class="mt-3 text-xs leading-5 text-emerald-200/70">
              Last saved: {formatDate(savedAt)}
            </p>
          {/if}
        </section>
      {/if}

      <section class="mt-6 grid gap-3 sm:grid-cols-2">
        <button
          type="button"
          class="rounded-xl border border-indigo-400 px-4 py-3 font-semibold text-indigo-200 hover:bg-indigo-500/10"
          onclick={previewPlan}
        >
          Preview plan
        </button>

        <button
          type="button"
          class="rounded-xl bg-indigo-600 px-4 py-3 font-semibold text-white hover:bg-indigo-700"
          onclick={savePlan}
        >
          Save plan
        </button>
      </section>
    {/if}

    <p class="mt-6 text-center text-xs leading-5 text-slate-500">
      Prototype only. Saving does not enable alerts, send messages, call anyone,
      access device sensors, or share location.
    </p>
  </section>
</main>
