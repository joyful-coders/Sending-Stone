<script lang="ts">
  import { goto } from '$app/navigation';

  type Message = {
    id: number;
    role: 'user' | 'assistant';
    text: string;
  };

  let input = '';
  let nextId = 2;
  let isSending = false;
  let chatContainer: HTMLElement;
  let masonAudio: HTMLAudioElement | undefined;
  let masonScareActive = false;
  let masonScareTimeout: ReturnType<typeof setTimeout> | undefined;

  let messages: Message[] = [
    {
      id: 1,
      role: 'assistant',
      text: 'Hi! What would you like to talk about?'
    }
  ];

  function playMasonSound() {
    masonAudio ??= new Audio('/mason-jumpscare.mp3');
    masonAudio.currentTime = 0;
    void masonAudio.play().catch((error: unknown) => {
      console.warn('Could not play the Mason sound file:', error);
    });
  }

  function triggerMasonScare() {
    masonScareActive = true;
    playMasonSound();
    if (masonScareTimeout) clearTimeout(masonScareTimeout);
    masonScareTimeout = setTimeout(() => {
      masonScareActive = false;
      masonScareTimeout = undefined;
    }, 1000);
  }

  function dismissMasonScare() {
    masonScareActive = false;
    if (masonScareTimeout) clearTimeout(masonScareTimeout);
    masonScareTimeout = undefined;
  }

  function handleWindowKeydown(event: KeyboardEvent) {
    if (event.key === 'Escape' && masonScareActive) dismissMasonScare();
  }

  async function sendMessage() {
    const text = input.trim();

    if (!text || isSending) return;
    
    if (text.toLowerCase() === '/private' || text.toLowerCase() === '/open') {
      input = '';
      await goto('/private');
      return;
    }

    if (text.toLowerCase() === '/mason') {
      input = '';
      triggerMasonScare();
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
    isSending = true;

    try {
      const response = await fetch('/api/chat', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          messages: messages
            .filter((message) => message.id !== 1)
            .map(({ role, text: messageText }) => ({ role, text: messageText }))
        })
      });
      const result = await response.json();

      if (!response.ok) {
        throw new Error(result.message ?? 'The assistant could not reply. Please try again.');
      }

      messages = [...messages, { id: nextId++, role: 'assistant', text: result.text }];
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Something went wrong. Please try again.';
      messages = [...messages, { id: nextId++, role: 'assistant', text: message }];
    } finally {
      isSending = false;
    }
  }

  $: if (chatContainer) {
    messages;
    isSending;
    chatContainer.scrollTop = chatContainer.scrollHeight;
  }
</script>

<svelte:window onkeydown={handleWindowKeydown} />

<svelte:head>
  <title>Sending Stone</title>
  <meta name="description" content="Sending Stone assistant" />
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
        S
      </div>

      <div>
        <h1 class="font-semibold">Sending Stone</h1>
        <p class="text-sm text-slate-500">Gemini assistant</p>
      </div>
    </header>

    <section bind:this={chatContainer} class="flex flex-1 flex-col justify-end gap-3 overflow-y-auto p-4" aria-label="Chat messages" aria-live="polite">
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
      {#if isSending}
        <div class="max-w-[82%] rounded-2xl rounded-bl-md bg-slate-100 px-4 py-3 text-sm text-slate-500" role="status">Thinking…</div>
      {/if}
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
        disabled={!input.trim() || isSending}
      >
        Send
      </button>
    </form>
  </section>
</main>

{#if masonScareActive}
  <div class="mason-scare" role="dialog" aria-modal="true" aria-label="Surprise!">
    <img src="/mason-placeholder.png" alt="A silly spooky face popping up as a surprise" />
    <button type="button" onclick={dismissMasonScare} aria-label="Dismiss surprise">×</button>
  </div>
{/if}

<style>
  .mason-scare {
    position: fixed;
    inset: 0;
    z-index: 100;
    display: grid;
    place-items: center;
    overflow: hidden;
    background: #100d18;
  }

  .mason-scare img {
    position: absolute;
    width: min(82vw, 520px);
    height: min(82vw, 520px);
    object-fit: contain;
  }

  .mason-scare button {
    position: absolute;
    top: 1rem;
    right: 1rem;
    display: grid;
    width: 2.75rem;
    height: 2.75rem;
    place-items: center;
    border: 1px solid #ffffff66;
    border-radius: 999px;
    background: #ffffff22;
    color: white;
    font-size: 1.75rem;
    cursor: pointer;
  }

</style>
