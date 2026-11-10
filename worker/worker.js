const corsHeaders = {
  "Access-Control-Allow-Origin": "http://192.168.4.1",
  "Access-Control-Allow-Methods": "POST, OPTIONS",
  "Access-Control-Allow-Headers": "Content-Type",
  "Vary": "Origin"
};

export default {
  async fetch(request, env) {
    const origin = request.headers.get("Origin");
    if (origin !== "http://192.168.4.1") {
      return new Response("Forbidden origin", { status: 403 });
    }
    if (request.method === "OPTIONS") {
      return new Response(null, { status: 204, headers: corsHeaders });
    }
    if (request.method !== "POST") {
      return Response.json({ error: "POST required" }, { status: 405, headers: corsHeaders });
    }
    if (!env.OPENAI_API_KEY) {
      return Response.json({ error: "Server API key is not configured" }, { status: 503, headers: corsHeaders });
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
        return Response.json({ error: "Conversation is too long; clear the chat and retry." }, { status: 413, headers: corsHeaders });
      }

      const upstream = await fetch("https://api.openai.com/v1/responses", {
        method: "POST",
        headers: {
          "Authorization": "Bearer " + env.OPENAI_API_KEY,
          "Content-Type": "application/json"
        },
        body: JSON.stringify({
          model: "gpt-4.1-mini",
          instructions: "You are Lucy, a friendly, clear, helpful AI companion. Reply in the language used by the user. Keep answers concise unless asked for detail.",
          input: messages,
          max_output_tokens: 700
        })
      });
      const data = await upstream.json();
      if (!upstream.ok) {
        console.error("OpenAI upstream error", upstream.status, data?.error?.type || "");
        return Response.json({ error: "AI provider error (" + upstream.status + "). Check API billing and model access." }, { status: 502, headers: corsHeaders });
      }
      const reply = (data.output || []).flatMap(x => x.content || [])
        .filter(x => x.type === "output_text").map(x => x.text || "").join("");
      if (!reply) return Response.json({ error: "AI returned an empty response" }, { status: 502, headers: corsHeaders });
      return Response.json({ reply }, { headers: { ...corsHeaders, "Cache-Control": "no-store" } });
    } catch (err) {
      console.error("Worker error", err);
      return Response.json({ error: "Invalid request or temporary server error" }, { status: 500, headers: corsHeaders });
    }
  }
};
