import { json } from '@sveltejs/kit';
import { sendSMS } from '$lib/server/sms';

export async function POST({ request }) {
    const { message } = await request.json();
    await sendSMS();
    return json({ status: "SENT" });
}