// Claude Anywhere — Cloudflare Worker relay
//
// Deploy this to forward requests to api.anthropic.com from a domain/IP
// that isn't blocked in your region. Useful in countries where
// api.anthropic.com itself is blocked (e.g. China).
//
// Deploy:
//   1. https://dash.cloudflare.com -> Workers & Pages -> Create -> paste this file
//   2. Deploy. Note the URL, e.g. https://your-worker.your-subdomain.workers.dev
//   3. On the device, during WiFi setup, set the "Claude API endpoint" field to:
//        https://your-worker.your-subdomain.workers.dev/v1/messages
//
// Note: Cloudflare Workers get inconsistently blocked by some firewalls
// (domain/SNI filtering). If this Worker URL gets blocked, a self-hosted
// VPS relay outside the censored region is more reliable — same logic,
// just run as a small Node/Python HTTP server instead.

export default {
  async fetch(request) {
    const url = new URL(request.url);
    const target = "https://api.anthropic.com" + url.pathname + url.search;

    const headers = new Headers(request.headers);
    headers.delete("host");

    const resp = await fetch(target, {
      method: request.method,
      headers,
      body: request.method === "GET" || request.method === "HEAD" ? undefined : request.body,
    });

    const respHeaders = new Headers(resp.headers);
    respHeaders.set("Access-Control-Allow-Origin", "*");
    return new Response(resp.body, { status: resp.status, headers: respHeaders });
  },
};
