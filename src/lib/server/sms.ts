import twilio from 'twilio';
import {
  TWILIO_ACCOUNT_SID,
  TWILIO_AUTH_TOKEN,
  TWILIO_FROM_NUMBER,
  MY_PHONE_NUMBER
} from '$env/static/private';

// Find your Account SID and Auth Token at twilio.com/console
// and set the environment variables. See http://twil.io/secure
const client = twilio(TWILIO_ACCOUNT_SID, TWILIO_AUTH_TOKEN);

async function createMessage() {
  const message = await client.messages.create({
    body: "sms_internal_alerts",
    from: "+17372583478",
    to: "+14404944133",
  });

  console.log(message.sid);
}

export async function sendSMS(){
  createMessage();
}