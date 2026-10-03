<script lang="ts">
  import { goto } from '$app/navigation';

  let message = '';

  function sendMessage() {
    const text = message.trim();

    if (!text) return;

    if (text === '/open') {
      message = '';
      goto('/private');
      return;
    }

    console.log('Normal message: ', text);

    message = '';
  }
</script>

<main class="min-h-screen bg-slate-50 p-5 text-slate-900">
  <section class="mx-auto flex min-h-[80vh] max-w-xl flex-col rounded-2xl bg-white shadow-sm">
    <header class="border-b border-slate-200 px-5 py-4">
      <h1 class="text-lg font-semibold">untitled</h1>
      <p class="text-sm text-slate-500">Local assistant</p>
    </header>

    <div class="flex-1 p-5">
      <div class="max-w-[80%] rounded-2xl rounded-bl-md bg-slate-100 px-4 py-3 text-sm">
        Hi! What would you like to talk about?
      </div>
    </div>

    <form class="flex gap-2 border-t border-slate-200 p-3"
      onsubmit={(event) => {
        event.preventDefault();
        sendMessage();
      }}
    >
      <input
        bind:value={message}
        class="min-w-0 flex-1 rounded-xl border border-slate-300 px-3 py-2 outline-none focus:border-blue-500 focus:ring-2 focus:ring-blue-100"
        placeholder="Message"
      />
      <button
        class="rounded-xl bg-blue-600 px-4 py-2 font-medium text-white hover:bg-blue-700"
        type="submit">
        Send
      </button>
    </form>
  </section>
</main>
