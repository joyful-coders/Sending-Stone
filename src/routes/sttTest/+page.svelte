<script lang="ts">
    import { onMount } from "svelte";

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
</script>

{#if loading}<p>Transcribing…</p>{/if}
<p>{transcript}</p>