// Cloudflare Worker — CORS proxy for the BOUN quota endpoint.
//
// WHY THIS EXISTS
// The WebAssembly / GitHub Pages build runs inside the browser, so every request
// is subject to the browser's CORS policy. A direct POST from the Pages origin to
// registration.boun.edu.tr is blocked because BOUN does not send
// Access-Control-Allow-Origin headers. This Worker sits in between: the browser
// app calls the Worker (same trust boundary you control), the Worker forwards the
// request to BOUN server-side (no CORS in server-to-server calls), and returns
// BOUN's response with CORS headers added so the browser accepts it.
//
// IMPORTANT INVARIANTS
//  - The BOUN quota page is charset=iso8859-9 (Latin-5). We forward the response
//    BODY AS RAW BYTES and DO NOT re-encode it, so the app's decodeBody() still
//    sees the original bytes and decodes them correctly. Re-encoding to UTF-8
//    here would corrupt Turkish characters.
//  - We forward exactly the fields the parser depends on (abbr/code/section) with
//    the Content-Type, Origin and Referer BOUN expects. abbr must be UPPERCASE
//    (the app already uppercases it before sending).
//  - This proxy ONLY forwards to the single fixed BOUN quota endpoint. It is not
//    an open proxy.

const BOUN_ENDPOINT =
  "https://registration.boun.edu.tr/scripts/quotasearch.asp";

// Restrict who may call this Worker. Add your GitHub Pages origin here.
// Use ["*"] only for quick testing; prefer an explicit allowlist in production.
const ALLOWED_ORIGINS = [
  "https://sueda-okutan.github.io",
  "http://localhost:8000", // local `python -m http.server` testing of the WASM build
];

function corsHeaders(requestOrigin) {
  // Reflect the origin if it is allowed; otherwise fall back to the first
  // allowed origin (the browser will then block a disallowed caller).
  const allowed =
    ALLOWED_ORIGINS.includes(requestOrigin) ? requestOrigin : ALLOWED_ORIGINS[0];
  return {
    "Access-Control-Allow-Origin": allowed,
    "Access-Control-Allow-Methods": "POST, OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type",
    "Access-Control-Max-Age": "86400",
    "Vary": "Origin",
  };
}

export default {
  async fetch(request) {
    const origin = request.headers.get("Origin") || "";

    // Preflight: the browser sends OPTIONS before the real POST because the
    // request carries a non-simple Content-Type. Answer it with CORS headers.
    if (request.method === "OPTIONS") {
      return new Response(null, { status: 204, headers: corsHeaders(origin) });
    }

    if (request.method !== "POST") {
      return new Response("Only POST is supported.", {
        status: 405,
        headers: corsHeaders(origin),
      });
    }

    // Read the form body the app sent (abbr=...&code=...&section=...).
    const body = await request.text();

    let bounResponse;
    try {
      bounResponse = await fetch(BOUN_ENDPOINT, {
        method: "POST",
        headers: {
          "Content-Type": "application/x-www-form-urlencoded",
          "Origin": "https://registration.boun.edu.tr",
          "Referer": "https://registration.boun.edu.tr/quotaentry.htm",
          "User-Agent": "BOUN-Quota-Tracker/1.0 (proxy)",
          "Accept":
            "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
        },
        body,
        redirect: "follow",
      });
    } catch (err) {
      return new Response("Upstream request to BOUN failed: " + err, {
        status: 502,
        headers: corsHeaders(origin),
      });
    }

    // Pass the raw bytes through untouched to preserve the iso8859-9 encoding.
    const rawBytes = await bounResponse.arrayBuffer();

    const headers = corsHeaders(origin);
    // Preserve the upstream Content-Type (which carries charset=iso8859-9) so the
    // client decodes correctly; default to it explicitly if missing.
    headers["Content-Type"] =
      bounResponse.headers.get("Content-Type") ||
      "text/html; charset=iso8859-9";

    return new Response(rawBytes, {
      status: bounResponse.status,
      headers,
    });
  },
};
