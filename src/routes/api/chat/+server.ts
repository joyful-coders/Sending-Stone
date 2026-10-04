import { json, type RequestHandler } from '@sveltejs/kit';
import { env } from '$env/dynamic/private';

type ChatMessage = {
  role: 'user' | 'assistant';
  text: string;
};

const MAX_MESSAGES = 20;
const MAX_MESSAGE_LENGTH = 4000;

export const POST: RequestHandler = async ({ request }) => {
  const geminiApiKey = env.GEMINI_API_KEY;
  if (!geminiApiKey) {
    return json({ message: 'Gemini is not configured. Add GEMINI_API_KEY to the server environment.' }, { status: 503 });
  }

  let body: { messages?: unknown };
  try {
    body = await request.json();
  } catch {
    return json({ message: 'Request body must be valid JSON.' }, { status: 400 });
  }

  if (!Array.isArray(body.messages) || body.messages.length === 0 || body.messages.length > MAX_MESSAGES) {
    return json({ message: `Send between 1 and ${MAX_MESSAGES} chat messages.` }, { status: 400 });
  }

  const messages: ChatMessage[] = [];
  for (const message of body.messages) {
    if (
      !message ||
      (message.role !== 'user' && message.role !== 'assistant') ||
      typeof message.text !== 'string' ||
      !message.text.trim() ||
      message.text.length > MAX_MESSAGE_LENGTH
    ) {
      return json({ message: 'Each message must have a valid role and text under 4,000 characters.' }, { status: 400 });
    }
    messages.push({ role: message.role, text: message.text.trim() });
  }

  try {
    const response = await fetch('https://generativelanguage.googleapis.com/v1beta/models/gemini-3.8-flash:generateContent', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'x-goog-api-key': geminiApiKey
      },
      body: JSON.stringify({
        systemInstruction: {
          parts: [{
            text: 'You are the Sending Stone assistant, a kind, calm, and practical conversational helper. Offer supportive conversation and simple information. Do not claim to monitor the user, contact emergency services, or provide professional crisis, legal, or medical care. If someone describes immediate danger, encourage them to contact local emergency services or a trusted person when it is safe to do so.'
          }]
        },
        contents: messages.map((message) => ({
          role: message.role === 'assistant' ? 'model' : 'user',
          parts: [{ text: message.text }]
        })),
        generationConfig: { maxOutputTokens: 1024 }
      })
    });

    if (!response.ok) {
      console.error('Gemini API request failed with status', response.status);
      return json({ message: 'The assistant service is unavailable right now. Please try again.' }, { status: 502 });
    }

    const result = await response.json();
    const text = result.candidates?.[0]?.content?.parts
      ?.map((part: { text?: string }) => part.text ?? '')
      .join('')
      .trim();

    if (!text) {
      return json({ message: 'The assistant returned an empty response. Please try again.' }, { status: 502 });
    }

    return json({ text });
  } catch (error) {
    console.error('Gemini API request failed', error);
    return json({ message: 'Could not reach the assistant service. Check your connection and try again.' }, { status: 502 });
  }
};
