<script lang="ts">
  import { goto } from '$app/navigation';
  import { onMount } from 'svelte';
  import { addActivity, getDB } from '$lib/db';

  type Contact = {
    id: number;
    name: string;
    relationship: string;
    phone: string;
    created_at: string;
  };

  let contacts: Contact[] = [];
  let loading = true;
  let message = '';

  let showContactForm = false;
  let editingContactId: number | null = null;

  let name = '';
  let relationship = '';
  let phone = '';

  function backToDashboard() {
    showContactForm = false;
    goto('/private');
  }

  function resetForm() {
    editingContactId = null;
    name = '';
    relationship = '';
    phone = '';
  }

  function openAddContact() {
    message = '';
    resetForm();
    showContactForm = true;
  }

  function openEditContact(contact: Contact) {
    message = '';
    editingContactId = contact.id;
    name = contact.name;
    relationship = contact.relationship;
    phone = formatPhoneNumber(contact.phone);
    showContactForm = true;
  }

  function closeContactForm() {
    showContactForm = false;
    resetForm();
  }

  function normalizePhoneNumber(value: string) {
    return value.replace(/\D/g, '');
  }

  function formatPhoneNumber(value: string) {
    const digits = normalizePhoneNumber(value);

    if (digits.length === 10) {
      return `(${digits.slice(0, 3)}) ${digits.slice(3, 6)}-${digits.slice(6)}`;
    }

    if (digits.length === 11 && digits.startsWith('1')) {
      return `+1 (${digits.slice(1, 4)}) ${digits.slice(4, 7)}-${digits.slice(7)}`;
    }

    return value;
  }

  function formatDate(value: string) {
    const date = new Date(value.replace(' ', 'T') + 'Z');

    if (Number.isNaN(date.getTime())) {
      return value;
    }

    return date.toLocaleString();
  }

  async function loadContacts() {
    const db = await getDB();

    contacts = await db.select<Contact[]>(`
      SELECT id, name, relationship, phone, created_at
      FROM trusted_contacts
      ORDER BY created_at DESC, id DESC
    `);
  }

  onMount(async () => {
    try {
      await loadContacts();
    } catch (error) {
      console.error('Could not load trusted contacts:', error);

      message = `Could not load contacts: ${
        error instanceof Error ? error.message : String(error)
      }`;
    } finally {
      loading = false;
    }
  });

  async function saveContact() {
    const cleanName = name.trim();
    const cleanRelationship = relationship.trim();
    const cleanPhone = normalizePhoneNumber(phone);

    if (!cleanName || !cleanRelationship || !cleanPhone) {
      message = 'Add a name, relationship, and phone number first.';
      return;
    }

    if (cleanPhone.length < 10) {
      message = 'Enter a valid phone number with at least 10 digits.';
      return;
    }

    try {
      const db = await getDB();

      if (editingContactId === null) {
        await db.execute(
          `
            INSERT INTO trusted_contacts (name, relationship, phone)
            VALUES ($1, $2, $3)
          `,
          [cleanName, cleanRelationship, cleanPhone]
        );

        await addActivity(
          'Trusted contact added',
          `${cleanName} was added as a ${cleanRelationship.toLowerCase()}.`
        );

        message = `${cleanName} was saved as a trusted contact.`;
      } else {
        await db.execute(
          `
            UPDATE trusted_contacts
            SET
              name = $1,
              relationship = $2,
              phone = $3
            WHERE id = $4
          `,
          [cleanName, cleanRelationship, cleanPhone, editingContactId]
        );

        await addActivity(
          'Trusted contact updated',
          `${cleanName}'s trusted-contact details were updated.`
        );

        message = `${cleanName}'s contact details were updated.`;
      }

      await loadContacts();
      closeContactForm();
    } catch (error) {
      console.error('Could not save trusted contact:', error);

      message = `Could not save contact: ${
        error instanceof Error ? error.message : String(error)
      }`;
    }
  }

  async function removeContact(contact: Contact) {
    try {
      const db = await getDB();

      await db.execute(
        `DELETE FROM trusted_contacts WHERE id = $1`,
        [contact.id]
      );

      await addActivity(
        'Trusted contact removed',
        `${contact.name} was removed from trusted contacts.`
      );

      await loadContacts();

      message = `${contact.name} was removed.`;
    } catch (error) {
      console.error('Could not remove trusted contact:', error);

      message = `Could not remove contact: ${
        error instanceof Error ? error.message : String(error)
      }`;
    }
  }
</script>

<svelte:head>
  <title>Trusted Contacts</title>
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
          Add people who may receive an alert when your safety plan is activated.
          Contacts are stored locally on this device.
        </p>
      </div>

      <button
        type="button"
        class="shrink-0 rounded-lg bg-slate-800 px-3 py-2 text-sm font-medium text-slate-100 hover:bg-slate-700"
        onclick={backToDashboard}
      >
        Back
      </button>
    </header>

    <section class="mt-6 rounded-2xl border border-slate-800 bg-slate-900 p-5">
      <div class="flex items-center justify-between gap-4">
        <div>
          <h2 class="font-semibold">Your contacts</h2>

          <p class="mt-1 text-sm text-slate-400">
            {contacts.length} saved locally
          </p>
        </div>

        <button
          type="button"
          class="shrink-0 rounded-xl bg-indigo-600 px-4 py-3 text-sm font-semibold text-white hover:bg-indigo-700"
          onclick={openAddContact}
        >
          Add contact
        </button>
      </div>

      {#if loading}
        <p class="mt-5 rounded-xl border border-slate-700 bg-slate-950 p-4 text-sm text-slate-400">
          Loading trusted contacts…
        </p>
      {:else if contacts.length === 0}
        <div
          class="mt-5 rounded-xl border border-dashed border-slate-700 bg-slate-950 p-6 text-center"
        >
          <p class="font-medium">No trusted contacts yet</p>

          <p class="mt-1 text-sm leading-6 text-slate-400">
            Add someone who would receive a simulated alert during the demo.
          </p>
        </div>
      {:else}
        <ul class="mt-5 grid gap-3">
          {#each contacts as contact (contact.id)}
            <li class="rounded-xl border border-slate-700 bg-slate-950 p-4">
              <div class="flex items-start justify-between gap-4">
                <div class="min-w-0">
                  <p class="truncate font-semibold">{contact.name}</p>

                  <p class="mt-1 text-sm text-slate-400">
                    {contact.relationship} • {formatPhoneNumber(contact.phone)}
                  </p>

                  <p class="mt-2 text-xs text-slate-500">
                    Added {formatDate(contact.created_at)}
                  </p>
                </div>

                <div class="flex shrink-0 items-center gap-1">
                  <button
                    type="button"
                    class="rounded-lg px-3 py-2 text-sm font-medium text-indigo-300 hover:bg-indigo-500/10"
                    onclick={() => openEditContact(contact)}
                  >
                    Edit
                  </button>

                  <button
                    type="button"
                    class="rounded-lg px-3 py-2 text-sm font-medium text-rose-300 hover:bg-rose-500/10"
                    onclick={() => removeContact(contact)}
                  >
                    Remove
                  </button>
                </div>
              </div>
            </li>
          {/each}
        </ul>
      {/if}
    </section>

    {#if message}
      <p
        class="mt-4 rounded-xl border border-indigo-500/30 bg-indigo-500/10 p-4 text-sm leading-6 text-indigo-100"
        aria-live="polite"
      >
        {message}
      </p>
    {/if}

    <p class="mt-6 text-center text-xs leading-5 text-slate-500">
      Prototype only. Adding, changing, or removing a contact does not send
      messages, make calls, share location, or notify anyone.
    </p>
  </section>
</main>

{#if showContactForm}
  <button
    type="button"
    class="fixed inset-0 z-40 cursor-default bg-black/60"
    aria-label="Close contact form"
    onclick={closeContactForm}
  ></button>

  <dialog
    open
    class="fixed left-1/2 top-1/2 z-50 m-0 w-[calc(100%-2rem)] max-w-md -translate-x-1/2 -translate-y-1/2 rounded-2xl border-0 bg-white p-5 text-slate-900 shadow-2xl"
    aria-labelledby="contact-form-title"
  >
    <p class="text-xs font-bold uppercase tracking-[0.16em] text-indigo-600">
      Trusted contact
    </p>

    <h2 id="contact-form-title" class="mt-1 text-xl font-bold">
      {editingContactId === null ? 'Add contact' : 'Edit contact'}
    </h2>

    <div class="mt-5 grid gap-4">
      <label class="block">
        <span class="text-sm font-medium text-slate-700">Name</span>

        <input
          bind:value={name}
          type="text"
          placeholder="Taylor Reed"
          class="mt-2 w-full rounded-xl border border-slate-300 px-3 py-3 text-sm outline-none focus:border-indigo-500"
        />
      </label>

      <label class="block">
        <span class="text-sm font-medium text-slate-700">Relationship</span>

        <input
          bind:value={relationship}
          type="text"
          placeholder="Friend, parent, roommate..."
          class="mt-2 w-full rounded-xl border border-slate-300 px-3 py-3 text-sm outline-none focus:border-indigo-500"
        />
      </label>

      <label class="block">
        <span class="text-sm font-medium text-slate-700">Phone number</span>

        <input
          bind:value={phone}
          type="tel"
          inputmode="numeric"
          placeholder="(555) 123-4567"
          class="mt-2 w-full rounded-xl border border-slate-300 px-3 py-3 text-sm outline-none focus:border-indigo-500"
        />
      </label>
    </div>

    <div class="mt-5 grid gap-3 sm:grid-cols-2">
      <button
        type="button"
        class="rounded-xl border border-slate-300 px-4 py-3 font-semibold text-slate-700 hover:bg-slate-100"
        onclick={closeContactForm}
      >
        Cancel
      </button>

      <button
        type="button"
        class="rounded-xl bg-indigo-600 px-4 py-3 font-semibold text-white hover:bg-indigo-700"
        onclick={saveContact}
      >
        {editingContactId === null ? 'Save contact' : 'Save changes'}
      </button>
    </div>
  </dialog>
{/if}
