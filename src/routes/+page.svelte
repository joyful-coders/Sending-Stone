<script lang="ts">
  import { goto } from '$app/navigation';

  type Message = {
    id: number;
    role: 'user' | 'assistant';
    text: string;
  };

  let input = '';
  let nextId = 2;

  let messages: Message[] = [
    {
      id: 1,
      role: 'assistant',
      text: 'Hi! What would you like to talk about?'
    }
  ];

  function genericReply(text: string): string {
    const normalized = text.toLowerCase();

    if (normalized.includes('hello') || normalized.includes('hi')) {
      return 'Hi! What can I help you think through?';
    }

    if (normalized.includes('how are you')) {
      return 'I’m doing well. What is on your mind?';
    }

    if (normalized.includes('thank')) {
      return 'You’re welcome.';
    }

    if (normalized.includes('homework') || normalized.includes('study')) {
      return 'Try breaking the task into one small next step. What are you working on?';
    }

    if (normalized.includes('weather')) {
      return 'I do not have live weather data in this local prototype.';
    }

    return 'That is interesting. Tell me a little more about it.';
  }

  async function sendMessage() {
    const text = input.trim();

    if (!text) return;

    // Development-only route trigger.
    // Later, make this user-configured and keep it out of visible help text.
    if (text === '/open') {
      input = '';
      await goto('/private');
      return;
    }

    messages = [
      ...messages,
      {
        id: nextId++,
        role: 'user',
        text
      }
    ];

    input = '';

    // Small delay so the reply feels like a response instead of instantly appearing.
    window.setTimeout(() => {
      messages = [
        ...messages,
        {
          id: nextId++,
          role: 'assistant',
          text: genericReply(text)
        }
      ];
    }, 350);
  }
</script>

<svelte:head>
  <title>untitled</title>
  <meta name="description" content="Local chat prototype" />
</svelte:head>

<main class="min-h-screen bg-slate-100 p-4 text-slate-900 sm:p-6">
  <section
    class="mx-auto flex min-h-[calc(100vh-2rem)] max-w-2xl flex-col overflow-hidden rounded-2xl border border-slate-200 bg-white shadow-sm sm:min-h-[calc(100vh-3rem)]"
  >
    <header class="flex items-center gap-3 border-b border-slate-200 px-4 py-4">
      <div
        class="grid h-10 w-10 place-items-center rounded-full bg-indigo-600 text-sm font-bold text-white"
        aria-hidden="true"
      >
        U
      </div>

      <div>
        <h1 class="font-semibold">untitled</h1>
        <p class="text-sm text-slate-500">Local assistant</p>
      </div>
    </header>

    <section class="flex flex-1 flex-col justify-end gap-3 overflow-y-auto p-4" aria-label="Chat messages">
      {#each messages as message (message.id)}
        <div
          class:ml-auto={message.role === 'user'}
          class:max-w-[82%]={true}
          class="rounded-2xl px-4 py-3 text-sm leading-6 shadow-sm"
          class:bg-indigo-600={message.role === 'user'}
          class:text-white={message.role === 'user'}
          class:rounded-br-md={message.role === 'user'}
          class:bg-slate-100={message.role === 'assistant'}
          class:text-slate-800={message.role === 'assistant'}
          class:rounded-bl-md={message.role === 'assistant'}
        >
          {message.text}
        </div>
      {/each}
    </section>

    <form
      class="flex gap-2 border-t border-slate-200 bg-white p-3"
      onsubmit={(event) => {
        event.preventDefault();
        sendMessage();
      }}
    >
      <input
        bind:value={input}
        class="min-w-0 flex-1 rounded-xl border border-slate-300 bg-white px-3 py-2.5 text-sm outline-none placeholder:text-slate-400 focus:border-indigo-500 focus:ring-2 focus:ring-indigo-100"
        placeholder="Message"
        autocomplete="off"
        aria-label="Message"
      />

      <button
        class="rounded-xl bg-indigo-600 px-4 py-2.5 text-sm font-semibold text-white transition hover:bg-indigo-700 focus:outline-none focus:ring-2 focus:ring-indigo-300 disabled:cursor-not-allowed disabled:bg-slate-300"
        type="submit"
        disabled={!input.trim()}
      >
        Send
      </button>
    </form>
  </section>
</main>
