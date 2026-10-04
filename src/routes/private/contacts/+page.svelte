<script lang="ts">
  import { goto } from '$app/navigation';

  type TrustedContact = {
    id: number;
    name: string;
    phone: string;
    enabled: boolean;
  };

  let contacts: TrustedContact[] = [
    {
      id: 1,
      name: 'Alex Morgan',
      phone: '(555) 010-1234',
      enabled: true
    },
    {
      id: 2,
      name: 'Jordan Lee',
      phone: '(555) 010-5678',
      enabled: true
    }
  ];

  let name = '';
  let phone = '';
  let nextId = 3;
  let message = '';

  function returnToDashboard() {
    goto('/private');
  }

  function addContact() {
    const cleanedName = name.trim();
    const cleanedPhone = phone.trim();

    if (!cleanedName || !cleanedPhone) {
      message = 'Enter both a name and a phone number.';
      return;
    }

    contacts = [
      ...contacts,
      {
        id: nextId++,
        name: cleanedName,
        phone: cleanedPhone,
        enabled: true
      }
    ];

    name = '';
    phone = '';
    message = `${cleanedName} was added locally for this prototype.`;
  }

  function toggleContact(id: number) {
    contacts = contacts.map((contact) =>
      contact.id === id
        ? { ...contact, enabled: !contact.enabled }
        : contact
    );
  }

  function removeContact(id: number) {
    const contact = contacts.find((item) => item.id === id);

    contacts = contacts.filter((item) => item.id !== id);

    if (contact) {
      message = `${contact.name} was removed locally.`;
    }
  }
</script>

<svelte:head>
  <title>Trusted contacts</title>
</svelte:head>

<main class="min-h-screen bg-slate-950 p-4 text-white sm:p-6">
  <section class="mx-auto max-w-2xl">
    <header class="flex items-start justify-between gap-4">
      <div>
        <p class="text-xs font-bold uppercase tracking-[0.2em] text-indigo-300">
          Safety plan
        </p>
        <h1 class="mt-1 text-3xl font-bold">Trusted contacts</h1>
        <p class="mt-2 max-w-xl text-sm leading-6 text-slate-300">
          Add people who may receive an alert if you choose to activate your
          safety plan.
        </p>
      </div>

      <button
        type="button"
        class="shrink-0 rounded-lg bg-slate-800 px-3 py-2 text-sm font-medium text-slate-100 hover:bg-slate-700"
        onclick={returnToDashboard}
      >
        Back
      </button>
    </header>

    <section class="mt-7 rounded-2xl border border-slate-800 bg-slate-900 p-5">
      <h2 class="text-lg font-semibold">Add a contact</h2>

      <form
        class="mt-4 grid gap-3 sm:grid-cols-[1fr_1fr_auto]"
        onsubmit={(event) => {
          event.preventDefault();
          addContact();
        }}
      >
        <label class="grid gap-1.5">
          <span class="text-sm text-slate-300">Name</span>
          <input
            bind:value={name}
            class="rounded-xl border border-slate-700 bg-slate-950 px-3 py-2.5 text-sm text-white outline-none placeholder:text-slate-500 focus:border-indigo-400 focus:ring-2 focus:ring-indigo-500/30"
            placeholder="Name"
            autocomplete="name"
          />
        </label>

        <label class="grid gap-1.5">
          <span class="text-sm text-slate-300">Phone</span>
          <input
            bind:value={phone}
            class="rounded-xl border border-slate-700 bg-slate-950 px-3 py-2.5 text-sm text-white outline-none placeholder:text-slate-500 focus:border-indigo-400 focus:ring-2 focus:ring-indigo-500/30"
            placeholder="Phone number"
            autocomplete="tel"
            inputmode="tel"
          />
        </label>

        <button
          type="submit"
          class="self-end rounded-xl bg-indigo-600 px-4 py-2.5 text-sm font-semibold text-white hover:bg-indigo-700"
        >
          Add
        </button>
      </form>

      {#if message}
        <p class="mt-4 text-sm text-indigo-200" aria-live="polite">
          {message}
        </p>
      {/if}
    </section>

    <section class="mt-5">
      <div class="flex items-center justify-between gap-3">
        <h2 class="text-lg font-semibold">Alert list</h2>
        <span class="text-sm text-slate-400">
          {contacts.filter((contact) => contact.enabled).length} active
        </span>
      </div>

      {#if contacts.length === 0}
        <div class="mt-3 rounded-2xl border border-dashed border-slate-700 p-6 text-center text-sm text-slate-400">
          No trusted contacts have been added.
        </div>
      {:else}
        <ul class="mt-3 grid gap-3">
          {#each contacts as contact (contact.id)}
            <li
              class="flex items-center justify-between gap-3 rounded-2xl border border-slate-800 bg-slate-900 p-4"
            >
              <div class="min-w-0">
                <p class="truncate font-semibold">{contact.name}</p>
                <p class="mt-1 truncate text-sm text-slate-400">
                  {contact.phone}
                </p>
              </div>

              <div class="flex shrink-0 items-center gap-2">
                <button
                  type="button"
                  class:!bg-emerald-600={contact.enabled}
                  class:!text-white={contact.enabled}
                  class="rounded-lg bg-slate-800 px-3 py-2 text-xs font-semibold text-slate-300 hover:bg-slate-700"
                  onclick={() => toggleContact(contact.id)}
                  aria-pressed={contact.enabled}
                >
                  {contact.enabled ? 'Active' : 'Paused'}
                </button>

                <button
                  type="button"
                  class="rounded-lg px-3 py-2 text-xs font-semibold text-rose-300 hover:bg-rose-950 hover:text-rose-200"
                  onclick={() => removeContact(contact.id)}
                >
                  Remove
                </button>
              </div>
            </li>
          {/each}
        </ul>
      {/if}
    </section>

    <p class="mt-7 text-center text-xs leading-5 text-slate-500">
      Prototype only. Contacts are stored in memory and disappear when the
      app reloads. No text message, call, location share, or external alert is
      sent.
    </p>
  </section>
</main>
