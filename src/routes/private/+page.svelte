<script lang="ts">
  import { goto } from '$app/navigation';
  import { onDestroy, onMount } from 'svelte';
  import { addActivity, getDB } from '$lib/db';
  import { watch } from '$lib/watch/watchClient.svelte';

  const watchStatusText: Record<string, string> = {
    idle: 'Not connected',
    scanning: 'Searching…',
    connecting: 'Connecting…',
    connected: 'Connected',
    reconnecting: 'Reconnecting…',
    error: 'Needs attention'
  };

  type ActivityEvent = {
    id: number;
    title: string;
    detail: string;
    created_at: string;
  };

  type Contact = {
    id: number;
    name: string;
    relationship: string;
    phone: string;
  };

  type Plan = {
    check_in_enabled: number;
    check_in_interval: string;
    grace_period: string;
    alert_method: string;
    include_location: number;
  };

  let showTestAlert = false;
  let testingAlert = false;
  let testAlertSent = false;

  let showCheckIn = false;
  let checkInStatus: 'idle' | 'pending' | 'safe' | 'help_requested' | 'missed' = 'idle';
  let checkInSeconds = 30;
  let checkInTimer: ReturnType<typeof setInterval> | undefined;

  let message = '';
  let activity: ActivityEvent[] = [];
  let contacts: Contact[] = [];
  let plan: Plan | null = null;
  let loading = true;

  function returnToChat() {
    goto('/');
  }

  function openContacts() {
    goto('/private/contacts');
  }

  function openAlertPlan() {
    goto('/private/plan');
  }

  function formatDate(value: string) {
    const date = new Date(value.replace(' ', 'T') + 'Z');

    if (Number.isNaN(date.getTime())) {
      return value;
    }

    return date.toLocaleString();
  }

  async function loadDashboardData() {
    const db = await getDB();

    activity = await db.select<ActivityEvent[]>(`
      SELECT id, title, detail, created_at
      FROM activity_events
      ORDER BY created_at DESC, id DESC
      LIMIT 6
    `);

    contacts = await db.select<Contact[]>(`
      SELECT id, name, relationship, phone
      FROM trusted_contacts
      ORDER BY created_at DESC, id DESC
    `);

    const plans = await db.select<Plan[]>(`
      SELECT
        check_in_enabled,
        check_in_interval,
        grace_period,
        alert_method,
        include_location
      FROM safety_plan
      WHERE id = 1
    `);

    plan = plans[0] ?? null;
  }

  onMount(async () => {
    try {
      await loadDashboardData();
    } catch (error) {
      console.error('Could not load dashboard data:', error);

      message = `Could not load dashboard data: ${
        error instanceof Error ? error.message : String(error)
      }`;
    } finally {
      loading = false;
    }
  });

  onDestroy(() => {
    clearCheckInTimer();
  });

  function openTestAlert() {
    testAlertSent = false;
    showTestAlert = true;
  }

  function closeTestAlert() {
    showTestAlert = false;
    testingAlert = false;
  }

  async function runTestAlert() {
    if (!plan) {
      message = 'Set up and save an alert plan before running a test alert.';
      closeTestAlert();
      return;
    }

    if (contacts.length === 0) {
      message = 'Add at least one trusted contact before running a test alert.';
      closeTestAlert();
      return;
    }

    testingAlert = true;

    try {
      await new Promise((resolve) => setTimeout(resolve, 900));

      await addActivity(
        'Test alert previewed',
        `A ${plan.alert_method.toLowerCase()} alert was simulated for ${contacts.length} trusted ${
          contacts.length === 1 ? 'contact' : 'contacts'
        }.`
      );

      await loadDashboardData();

      testAlertSent = true;
    } catch (error) {
      console.error('Could not run test alert:', error);

      message = `Could not run test alert: ${
        error instanceof Error ? error.message : String(error)
      }`;

      closeTestAlert();
    } finally {
      testingAlert = false;
    }
  }

  function clearCheckInTimer() {
    if (checkInTimer) {
      clearInterval(checkInTimer);
      checkInTimer = undefined;
    }
  }

  function openCheckIn() {
    clearCheckInTimer();
    checkInStatus = 'idle';
    checkInSeconds = 30;
    showCheckIn = true;
  }

  function closeCheckIn() {
    clearCheckInTimer();
    showCheckIn = false;
  }

  async function startCheckIn() {
    checkInStatus = 'pending';
    checkInSeconds = 30;

    try {
      await addActivity(
        'Check-in started',
        'A 30-second prototype check-in was started.'
      );

      await loadDashboardData();

      checkInTimer = setInterval(() => {
        checkInSeconds -= 1;

        if (checkInSeconds <= 0) {
          clearCheckInTimer();
          void markCheckInMissed();
        }
      }, 1000);
    } catch (error) {
      console.error('Could not start check-in:', error);

      message = `Could not start check-in: ${
        error instanceof Error ? error.message : String(error)
      }`;

      closeCheckIn();
    }
  }

  async function markCheckInMissed() {
    checkInStatus = 'missed';

    try {
      await addActivity(
        'Check-in missed',
        `No response was received. A simulated ${
          plan?.alert_method?.toLowerCase() ?? 'support'
        } alert would be prepared for trusted contacts.`
      );

      await loadDashboardData();
    } catch (error) {
      console.error('Could not save missed check-in:', error);
    }
  }

  async function confirmSafe() {
    clearCheckInTimer();
    checkInStatus = 'safe';

    try {
      await addActivity(
        'Check-in completed',
        'The user confirmed they were okay. No alert was started.'
      );

      await loadDashboardData();
    } catch (error) {
      console.error('Could not save completed check-in:', error);
    }
  }

  async function requestHelp() {
    clearCheckInTimer();
    checkInStatus = 'help_requested';

    try {
      await addActivity(
        'Help requested',
        `The user requested help. A simulated ${
          plan?.alert_method?.toLowerCase() ?? 'support'
        } alert would be prepared for trusted contacts.`
      );

      await loadDashboardData();
    } catch (error) {
      console.error('Could not save help request:', error);
    }
  }

  function showPrototypeMessage(text: string) {
    message = text;
  }
</script>

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
            Safety plan
          </p>

          <h2 class="mt-1 text-xl font-semibold">
            {plan?.check_in_enabled ? 'Check-ins active' : 'Check-ins off'}
          </h2>

          <p class="mt-1 text-sm text-slate-300">
            {#if plan?.check_in_enabled}
              Every {plan.check_in_interval} minutes • Grace period: {plan.grace_period} minutes
            {:else}
              Configure your alert plan to enable check-ins.
            {/if}
          </p>
        </div>

        <div class="rounded-xl bg-slate-800 px-3 py-2 text-right">
          <p class="text-xs text-slate-400">Contacts</p>

          <p class="text-sm font-bold text-indigo-300">{contacts.length}</p>
        </div>
      </div>
    </section>

    <section class="mt-5 grid grid-cols-1 gap-3 sm:grid-cols-2">
      <button
        type="button"
        class="rounded-2xl border border-slate-800 bg-slate-900 p-5 text-left transition hover:border-indigo-400 hover:bg-slate-800 sm:col-span-2"
        onclick={() => goto('/private/watch')}
      >
        <span class="text-2xl" aria-hidden="true">⌚</span>

        <span class="mt-4 block font-semibold">Watch</span>

        <span class="mt-1 block text-sm text-slate-400">
          {watch.device
            ? `${watch.device.name}: ${watchStatusText[watch.status]}`
            : 'Pair your watch to receive recordings'}
          {#if watch.transfer}
            • downloading a recording
          {/if}
        </span>
      </button>

      <button
        type="button"
        class="rounded-2xl border border-slate-800 bg-slate-900 p-5 text-left transition hover:border-indigo-400 hover:bg-slate-800"
        onclick={openContacts}
      >
        <span class="text-2xl" aria-hidden="true">👥</span>

        <span class="mt-4 block font-semibold">Trusted contacts</span>

        <span class="mt-1 block text-sm text-slate-400">
          {contacts.length === 0
            ? 'Add alert recipients'
            : `${contacts.length} saved locally`}
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
        onclick={openCheckIn}
      >
        <span class="text-2xl" aria-hidden="true">✓</span>

        <span class="mt-4 block font-semibold">Check-in</span>

        <span class="mt-1 block text-sm text-slate-400">
          Start a timed safety check-in
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
          Preview your saved alert flow
        </span>
      </button>

      <button
        type="button"
        class="rounded-2xl border border-slate-800 bg-slate-900 p-5 text-left transition hover:border-indigo-400 hover:bg-slate-800 sm:col-span-2"
        onclick={() => goto('/private/privacy')}
      >
        <span class="text-2xl" aria-hidden="true">⚙️</span>

        <span class="mt-4 block font-semibold">Privacy & data</span>

        <span class="mt-1 block text-sm text-slate-400">
          Review local data and permissions
        </span>
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
              'The dashboard displays the six most recent locally stored safety events.'
            )}
        >
          View all
        </button>
      </div>

      {#if loading}
        <p class="mt-3 rounded-2xl border border-slate-800 bg-slate-900 p-4 text-sm text-slate-400">
          Loading activity…
        </p>
      {:else if activity.length === 0}
        <div class="mt-3 rounded-2xl border border-dashed border-slate-700 bg-slate-900 p-5">
          <p class="font-medium">No activity yet</p>

          <p class="mt-1 text-sm text-slate-400">
            Save an alert plan, add a contact, run a check-in, or run a test alert to create activity.
          </p>
        </div>
      {:else}
        <ul class="mt-3 grid gap-3">
          {#each activity as event (event.id)}
            <li class="rounded-2xl border border-slate-800 bg-slate-900 p-4">
              <div class="flex items-start justify-between gap-4">
                <div>
                  <p class="font-semibold">{event.title}</p>

                  <p class="mt-1 text-sm text-slate-400">{event.detail}</p>
                </div>

                <time class="shrink-0 text-right text-xs text-slate-500">
                  {formatDate(event.created_at)}
                </time>
              </div>
            </li>
          {/each}
        </ul>
      {/if}
    </section>

    <p class="mt-7 text-center text-xs leading-5 text-slate-500">
      Prototype only. Check-ins and test alerts are simulated and do not send
      texts, calls, location, or emergency requests.
    </p>
  </section>
</main>

{#if showTestAlert}
  <div class="fixed inset-0 z-40 bg-black/60"></div>

  <dialog
    open
    class="fixed left-1/2 top-1/2 z-50 m-0 w-[calc(100%-2rem)] max-w-md -translate-x-1/2 -translate-y-1/2 rounded-2xl border-0 bg-white p-5 text-slate-900 shadow-2xl"
    aria-labelledby="test-alert-title"
  >
    {#if testAlertSent}
      <p class="text-xs font-bold uppercase tracking-[0.16em] text-emerald-600">
        Test complete
      </p>

      <h2 id="test-alert-title" class="mt-1 text-xl font-bold">
        Alert simulated
      </h2>

      <p class="mt-4 rounded-xl bg-emerald-50 p-4 text-sm leading-6 text-emerald-900">
        A simulated {plan?.alert_method.toLowerCase()} alert was prepared for
        {contacts.length} trusted {contacts.length === 1 ? 'contact' : 'contacts'}.
        No message or call was actually sent.
      </p>

      <button
        type="button"
        class="mt-5 w-full rounded-xl bg-indigo-600 px-4 py-3 font-semibold text-white hover:bg-indigo-700"
        onclick={closeTestAlert}
      >
        Done
      </button>
    {:else}
      <p class="text-xs font-bold uppercase tracking-[0.16em] text-indigo-600">
        Test alert
      </p>

      <h2 id="test-alert-title" class="mt-1 text-xl font-bold">
        Review your alert flow
      </h2>

      <div class="mt-4 rounded-xl bg-slate-100 p-4 text-sm leading-6">
        <p>
          <span class="font-semibold">Method:</span>
          {plan?.alert_method ?? 'No saved plan'}
        </p>

        <p class="mt-2">
          <span class="font-semibold">Recipients:</span>
          {contacts.length === 0
            ? 'No trusted contacts saved'
            : contacts.map((contact) => contact.name).join(', ')}
        </p>

        <p class="mt-2">
          <span class="font-semibold">Location:</span>
          {plan?.include_location ? 'Included in demo preview' : 'Not included'}
        </p>
      </div>

      <p class="mt-3 text-sm leading-6 text-slate-600">
        This adds a simulated event to your local activity history. It does not
        send a message, place a call, or share location.
      </p>

      <div class="mt-5 grid gap-3 sm:grid-cols-2">
        <button
          type="button"
          class="rounded-xl border border-slate-300 px-4 py-3 font-semibold text-slate-700 hover:bg-slate-100"
          onclick={closeTestAlert}
          disabled={testingAlert}
        >
          Cancel
        </button>

        <button
          type="button"
          class="rounded-xl bg-indigo-600 px-4 py-3 font-semibold text-white hover:bg-indigo-700 disabled:cursor-not-allowed disabled:opacity-60"
          onclick={runTestAlert}
          disabled={testingAlert}
        >
          {testingAlert ? 'Simulating…' : 'Run test alert'}
        </button>
      </div>
    {/if}
  </dialog>
{/if}

{#if showCheckIn}
  <div class="fixed inset-0 z-40 bg-black/60"></div>

  <dialog
    open
    class="fixed left-1/2 top-1/2 z-50 m-0 w-[calc(100%-2rem)] max-w-md -translate-x-1/2 -translate-y-1/2 rounded-2xl border-0 bg-white p-5 text-slate-900 shadow-2xl"
    aria-labelledby="check-in-title"
  >
    {#if checkInStatus === 'idle'}
      <p class="text-xs font-bold uppercase tracking-[0.16em] text-indigo-600">
        Safety check-in
      </p>

      <h2 id="check-in-title" class="mt-1 text-xl font-bold">
        Start a check-in
      </h2>

      <p class="mt-3 text-sm leading-6 text-slate-600">
        This demo starts a 30-second timer. If there is no response, the app
        prepares a simulated support alert. No real message, call, or location
        sharing occurs.
      </p>

      <div class="mt-5 grid gap-3 sm:grid-cols-2">
        <button
          type="button"
          class="rounded-xl border border-slate-300 px-4 py-3 font-semibold text-slate-700 hover:bg-slate-100"
          onclick={closeCheckIn}
        >
          Cancel
        </button>

        <button
          type="button"
          class="rounded-xl bg-indigo-600 px-4 py-3 font-semibold text-white hover:bg-indigo-700"
          onclick={startCheckIn}
        >
          Start check-in
        </button>
      </div>
    {:else if checkInStatus === 'pending'}
      <p class="text-xs font-bold uppercase tracking-[0.16em] text-indigo-600">
        Check-in active
      </p>

      <h2 id="check-in-title" class="mt-1 text-xl font-bold">
        Are you okay?
      </h2>

      <p class="mt-3 text-sm leading-6 text-slate-600">
        Reply before the timer ends to cancel the planned support alert.
      </p>

      <div class="mt-4 rounded-xl bg-indigo-50 p-4 text-center">
        <p class="text-xs font-bold uppercase tracking-[0.16em] text-indigo-600">
          Time remaining
        </p>

        <p class="mt-1 text-4xl font-bold text-indigo-900">
          00:{String(checkInSeconds).padStart(2, '0')}
        </p>
      </div>

      <div class="mt-5 grid gap-3 sm:grid-cols-2">
        <button
          type="button"
          class="rounded-xl border border-slate-300 px-4 py-3 font-semibold text-slate-700 hover:bg-slate-100"
          onclick={requestHelp}
        >
          I need help
        </button>

        <button
          type="button"
          class="rounded-xl bg-emerald-600 px-4 py-3 font-semibold text-white hover:bg-emerald-700"
          onclick={confirmSafe}
        >
          I’m okay
        </button>
      </div>
    {:else if checkInStatus === 'safe'}
      <p class="text-xs font-bold uppercase tracking-[0.16em] text-emerald-600">
        Check-in complete
      </p>

      <h2 id="check-in-title" class="mt-1 text-xl font-bold">
        You’re checked in
      </h2>

      <p class="mt-3 rounded-xl bg-emerald-50 p-4 text-sm leading-6 text-emerald-900">
        You confirmed that you are okay. No alert was started.
      </p>

      <button
        type="button"
        class="mt-5 w-full rounded-xl bg-indigo-600 px-4 py-3 font-semibold text-white hover:bg-indigo-700"
        onclick={closeCheckIn}
      >
        Done
      </button>
    {:else}
      <p class="text-xs font-bold uppercase tracking-[0.16em] text-amber-600">
        Support alert ready
      </p>

      <h2 id="check-in-title" class="mt-1 text-xl font-bold">
        {checkInStatus === 'help_requested' ? 'Help requested' : 'Check-in missed'}
      </h2>

      <p class="mt-3 rounded-xl bg-amber-50 p-4 text-sm leading-6 text-amber-900">
        A simulated {plan?.alert_method?.toLowerCase() ?? 'support'} alert is
        ready for {contacts.length} trusted
        {contacts.length === 1 ? 'contact' : 'contacts'}. No message, call, or
        location was actually sent.
      </p>

      <button
        type="button"
        class="mt-5 w-full rounded-xl bg-indigo-600 px-4 py-3 font-semibold text-white hover:bg-indigo-700"
        onclick={closeCheckIn}
      >
        Done
      </button>
    {/if}
  </dialog>
{/if}
