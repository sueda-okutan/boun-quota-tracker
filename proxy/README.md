# BOUN Quota CORS Proxy (Cloudflare Worker)

The WebAssembly / GitHub Pages build runs inside the browser and is subject to
CORS. A direct POST from the Pages origin to `registration.boun.edu.tr` is blocked
because the site does not send an `Access-Control-Allow-Origin` header.

This Worker is a proxy - the browser app calls the Worker, the Worker forwards the request to BOUN server-side (no CORS between servers), and returns BOUN's raw HTML with CORS headers added. The native desktop build does **not** use the proxy, it talks directly.

> **Raw bytes / charset is preserved** BOUN serves `charset=iso8859-9` (Latin-5). The Worker forwards the body as raw bytes and keeps the upstream `Content-Type`.

> **The exact request is sent** `abbr` (UPPERCASE), `code`, `section`, plus the `Content-Type`, `Origin`, and `Referer` BOUN expects.

## Wire the URL into the app

The WASM build sends its quota POST to the proxy URL baked in at compile time via the `BOUN_QUOTA_PROXY_URL` CMake cache variable, which **defaults** to the deployed proxy URL in `CMakeLists.txt`. The GitHub Pages workflow therefore works out of the box.

To point the build at a *different* proxy, override the default:

```bash
qt-cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release \
  -DBOUN_QUOTA_PROXY_URL="https://boun-quota-proxy.<your-subdomain>.workers.dev"
```

For GitHub Pages, set a repository **variable** named `BOUN_QUOTA_PROXY_URL`
(Settings → Secrets and variables → Actions → Variables). When set it overrides
the default; when unset the default applies. See
`.github/workflows/deploy-wasm.yml`. If the proxy URL is ever empty, the WASM
build falls back to a direct POST and hits the CORS error.

## Test the proxy without the app

```bash
curl -i -X POST 'https://boun-quota-proxy.<your-subdomain>.workers.dev' \
  -H 'Content-Type: application/x-www-form-urlencoded' \
  -H 'Origin: https://sueda-okutan.github.io' \
  --data 'abbr=TK&code=221&section=01'
```

You should get HTTP 200, an `Access-Control-Allow-Origin` header, and a body
containing a `<tr class='schtd'>` row.
