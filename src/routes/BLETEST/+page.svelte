<script lang="ts">
  // The BLE test page grew into the Watch page (src/lib/watch/, /private/watch).
  import { goto } from '$app/navigation';
  import { onMount } from 'svelte';

  onMount(() => goto('/private/watch', { replaceState: true }));
</script>
