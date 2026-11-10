const ALLOWED_ORIGINS = new Set([
  "http://192.168.4.1",
  "http://edsoft.local"
]);

export default {
  async fetch(request, env) {
    const origin = request.headers.get("Origin") || "";
    if (!ALLOWED_ORIGINS.has(origin)) {
      return new Response("Forbidden origin", { status: 403 });
    }
    const corsHeaders = {
      "Access-Control-Allow-Origin": origin,
      "Access-Control-Allow-Methods": "POST, OPTIONS",
      "Access-Control-Allow-Headers": "Content-Type",
      "Vary": "Origin"
    };
    if (request.method === "OPTIONS") {
      return new Response(null, { status: 204, headers: corsHeaders });
    }
    if (request.method !== "POST") {
      return Response.json({ error: "POST required" }, { status: 405, headers: corsHeaders });
    }
    if (!env.OPENROUTER_API_KEY) {
      return Response.json({ error: "OpenRouter server secret is not configured" }, { status: 503, headers: corsHeaders });
    }

    try {
      const body = await request.json();
      const messages = body?.messages;
      if (!Array.isArray(messages) || messages.length < 1 || messages.length > 16 ||
          messages.some(m => !m || !["user", "assistant"].includes(m.role) ||
          typeof m.content !== "string" || m.content.length > 2000)) {
        return Response.json({ error: "Invalid chat messages" }, { status: 400, headers: corsHeaders });
      }
      const total = messages.reduce((n, m) => n + m.content.length, 0);
      if (total > 12000) {
        return Response.json({ error: "Conversation too long; clear the chat and retry." }, { status: 413, headers: corsHeaders });
      }

      const upstream = await fetch("https://openrouter.ai/api/v1/chat/completions", {
        method: "POST",
        headers: {
          "Authorization": "Bearer " + env.OPENROUTER_API_KEY,
          "Content-Type": "application/json",
          "HTTP-Referer": "https://lucy-ai-esp32.pages.dev",
          "X-Title": "Lucy AI ESP32"
        },
        body: JSON.stringify({
          model: env.OPENROUTER_MODEL || "openai/gpt-4.1-mini",
          messages: [
            { role: "system", content: "You are Lucy, a friendly, clear and helpful AI companion. Reply in the language used by the user. Be concise unless asked for detail." },
            ...messages
          ],
          max_tokens: 700
        })
      });

      const data = await upstream.json();
      if (!upstream.ok) {
        console.error("OpenRouter error", upstream.status, data?.error?.code || data?.error?.message || "unknown");
        return Response.json({ error: "OpenRouter provider error (" + upstream.status + "). Check your secret, credits and model access." }, { status: 502, headers: corsHeaders });
      }
      const reply = data?.choices?.[0]?.message?.content;
      if (typeof reply !== "string" || !reply.trim()) {
        return Response.json({ error: "OpenRouter returned an empty or unsupported response" }, { status: 502, headers: corsHeaders });
      }
      return Response.json({ reply }, { headers: { ...corsHeaders, "Cache-Control": "no-store" } });
    } catch (err) {
      console.error("Worker request failed", err?.name || "Error");
      return Response.json({ error: "Invalid request or temporary server error" }, { status: 500, headers: corsHeaders });
    }
  }
};
