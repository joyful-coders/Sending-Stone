<script lang="ts">
  import { goto } from '$app/navigation';

  function returnToChat() {
    goto('/');
  }
</script>

<svelte:head>
  <title>Private</title>
</svelte:head>

<main class="min-h-screen bg-slate-950 p-5 text-white sm:p-8">
  <section class="mx-auto max-w-2xl">
    <header class="flex items-center justify-between gap-4">
      <div>
        <p class="text-xs font-bold uppercase tracking-[0.2em] text-indigo-300">
          Private area
        </p>
        <h1 class="mt-1 text-3xl font-bold">Dashboard</h1>
      </div>

      <button
        class="rounded-lg bg-slate-800 px-3 py-2 text-sm font-medium text-slate-100 hover:bg-slate-700"
        onclick={returnToChat}
      >
        Return
      </button>
    </header>

    <section class="mt-8 rounded-2xl border border-slate-800 bg-slate-900 p-5">
      <h2 class="text-lg font-semibold">Prototype setup</h2>
      <p class="mt-2 text-sm leading-6 text-slate-300">
        This page is intentionally blank for now. It does not record audio,
        access location, send alerts, or contact anyone.
      </p>
    </section>
  </section>
</main>
