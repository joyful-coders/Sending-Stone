<script lang="ts">
    import { json } from "@sveltejs/kit";
    import { onMount } from "svelte";

    let sending = $state(false);
  let loading = $state(false);
  let transcript = $state('');
  let chunks: Blob[] = [];

  async function transcribe(blob: Blob) {
    loading = true;
    const response = await fetch(
     "https://storage.googleapis.com/eleven-public-cdn/audio/marketing/nicole.mp3"
    );

    const audioBlob = new Blob([await response.arrayBuffer()], { type: "audio/mp3" });

    const res = await fetch('/api/transcribe', { method: 'POST',
    headers: {
        'Content-Type': 'audio/webm', // Set the appropriate MIME type for your audio
    },
    body: audioBlob });

    const data = await res.json();
    transcript = data.text;
    loading = false;
  }

  async function sendSMS(message: string) {
    sending = true;
    status = '';
      const res = await fetch('/api/notify', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ message })
      });

      if (!res.ok) throw new Error(`Request failed: ${res.status}`);

      const data = await res.json();
      status = `Sent (${data.status})`;
  }

</script>

{#if loading}<p>Transcribing…</p>{/if}
<p>{transcript}</p>
<button onclick={() => sendSMS("TEST MSG")}>TEST SMS</button>