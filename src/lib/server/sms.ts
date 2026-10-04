import twilio from 'twilio';
import { env } from '$env/dynamic/private';

// Find your Account SID and Auth Token at twilio.com/console
// and set the environment variables (.env). See http://twil.io/secure
// Read when a message is sent, not at build time, so the app builds without the keys.
function getClient() {
  if (!env.TWILIO_ACCOUNT_SID || !env.TWILIO_AUTH_TOKEN) {
    throw new Error('TWILIO_ACCOUNT_SID and TWILIO_AUTH_TOKEN are not set (.env).');
  }
  return twilio(env.TWILIO_ACCOUNT_SID, env.TWILIO_AUTH_TOKEN);
}

async function createMessage() {
  const message = await getClient().messages.create({
    body: "sms_internal_alerts",
    from: "+17372583478",
    to: "+14404944133",
  });

  console.log(message.sid);
}

export async function sendSMS(){
  createMessage();
}