<script lang="ts">
  import '../app.css';
  import { onMount } from 'svelte';
  import { goto } from '$app/navigation';
  import { page } from '$app/state';
  import { watch } from '$lib/watch/watchClient.svelte';

  let { children } = $props();

  // In the app, reconnect to the remembered watch on launch and stay connected
  // on every page. (A browser needs a tap before it may connect: see /private/watch.)
  onMount(() => {
    if (watch.transport.kind === 'native') watch.start();
  });

  // Only inside the private area, so nothing shows over the chat screen.
  const showAlert = $derived(watch.alert !== null && page.url.pathname.startsWith('/private'));
</script>

{#if showAlert && watch.alert}
  <div
    class="fixed inset-x-0 top-0 z-50 flex items-center justify-between gap-3 bg-rose-600 px-4 py-3 text-sm font-semibold text-white shadow-lg"
    role="alert"
  >
    <span>
      Watch alert: {watch.alert.trigger} trigger at {watch.alert.at.toLocaleTimeString()}. Recording now.
    </span>
    <span class="flex shrink-0 gap-2">
      <button type="button" class="rounded-lg bg-white/20 px-3 py-1 hover:bg-white/30" onclick={() => goto('/private/watch')}>
        View
      </button>
      <button type="button" class="rounded-lg bg-white/20 px-3 py-1 hover:bg-white/30" onclick={() => watch.dismissAlert()}>
        Dismiss
      </button>
    </span>
  </div>
{/if}

{@render children()}
