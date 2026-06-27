# BOUN Quota Endpoint

## Endpoint

```
POST https://registration.boun.edu.tr/scripts/quotasearch.asp
Content-Type: application/x-www-form-urlencoded
```

Response: `HTTP/1.1 200 OK`, `Content-Type: text/html`, charset **`iso8859-9`**
(Latin-5), served by Microsoft-IIS. The body is returned **directly** — there is
no redirect and no separate "fetch the result" GET. A session cookie
(`ASPSESSIONID...`) is set but is **not required** for the query to succeed.

### POST body

```
abbr=<UPPERCASE department>&code=<course number>&section=<two-digit section>
```

| Course      | Body                                  |
| ----------- | ------------------------------------- |
| TK 221.01   | `abbr=TK&code=221&section=01`         |
| HTR 312.01  | `abbr=HTR&code=312&section=01`        |

---

## How the data is returned: the response body IS an HTML page

`quotasearch.asp` is an old-style **server-rendered ASP page**, not a JSON/REST
API. There is no "data" endpoint that returns clean structured values. When a request is POSTed, the server:

1. Runs a SQL query against its course database. (It even leaks the query in an
   HTML comment in the response, ex:
   `<!-- select ders,section from dersbilgileri ... where ders='IE  495' ... -->`.)
2. Formats the result rows into a **complete HTML page** — a `<table>` with
   `<tr class='schtd'>` rows, a `<strong>` course label, a title, etc.
3. Sends that finished HTML document back as the response body, with
   `Content-Type: text/html`.

So **the data *is* the HTML.** The parser parses it.

In a real browser, the quota popup is an `<iframe>`. Clicking "search" POSTs the
form and the server's HTML response is **rendered into that iframe**.

This app does the exact same POST, but instead of rendering the HTML it reads the
raw response body as a string and scrapes it:

```cpp
// src/network.cpp — reply is the QNetworkReply* from networkManager->post(...)
const QByteArray raw = reply->readAll();   // the raw HTML bytes
const QString html = decodeBody(raw, reply);
...
emit quotaHtmlFetched(request, html);      // hand the HTML to the parser
```

```cpp
// src/logic.cpp — the parser turns that HTML back into structured data
CourseQuotaResult result = QuotaParser::parseQuotaHtml(html, request);
```

`reply->readAll()` returns the same bytes a browser would have rendered. The
parser's job is therefore to **undo the server's HTML formatting** — reverse out
the structured numbers from a page that was built for human eyes. That is why
[src/parser.cpp](src/parser.cpp) is regex/string based, and why it is inherently
sensitive to layout changes: it is screen-scraping a rendered page, not consuming
an API.

- **Layout changes break parsing, not the request.** If BOUN restructures the
  result table, the POST will still succeed and return HTML, but the regexes in
  `parseQuotaHtml` may stop matching. The request layer and the parse layer fail
  independently — keep them separate (they are: `network.*` vs `parser.*`).
---

## `abbr` MUST be UPPERCASE

The server's stored department code is **case-sensitive and uppercase**.
Lowercase coincidentally matches *some* departments but not others:

| Request                          | Live result                          |
| -------------------------------- | ------------------------------------ |
| `abbr=tk&code=221&section=01`    | quota row returned  (works by luck) |
| `abbr=TK&code=221&section=01`    | quota row returned                 |
| `abbr=ie&code=495&section=01`    | **"No Such Course In This Semester..."** |
| `abbr=IE&code=495&section=01`    | quota row returned |

**Always send `abbr` uppercase.** See `buildPostBody()` in
[src/network.cpp](src/network.cpp).

The server echoes its internal query in an HTML comment:

```html
<!-- select ders,section from dersbilgileri WITH (NOLOCK)
     where ders='IE  495' and section='01' and donem='2025/2026-3' -->
```

Note the department is space-padded to a fixed width (`'IE  495'`), but that
padding is done **server-side**; the client only needs to send `abbr=IE`.

---

## "No Such Course"

When the course/section is **not offered in the active semester**, the server
returns HTTP 200 with a body containing:

```html
<center>No Such Course In This Semester...</center>
````
The parser detects the
`No Such Course` marker and reports a distinct message ("Course not offered in this semester.") 

Because the app cannot override the server's active `donem`, you can only see
courses offered in the term the registration system currently treats as active.

---

## Unlimited quota: the Quota cell is the word "Unlimited"

A course with an unrestricted quota returns a real `schtd` row whose **Quota
cell contains the literal text `Unlimited`** while the **Current cell stays
numeric**:

```html
<tr class='schtd'>
  <td><p align=center>ALL&nbsp;</td>
  <td><p align=center>ALL&nbsp;</td>
  <td><p align=center>Unlimited&nbsp;</td>   <!-- quota -->
  <td><p align=center>102&nbsp;</td>         <!-- current -->
</tr>
```

(Confirmed for **IE 495.01**, current = 102.)

Parser handling:

- Require only the **Current** cell to be numeric (it always is on a data row).
  A row with a non-numeric Current cell is a header/label row and is skipped.
- If the **Quota** cell is non-numeric, mark the row `unlimited = true` rather
  than dropping it.
- An unlimited row is **always available** (`hasAvailableSeat() == true`) and
  `availableSeats()` returns the sentinel `-1`, which is excluded from
  `totalAvailableSeats()` so it never corrupts the finite seat sum.

The UI shows `Unlimited` in the Quota and Available columns for such rows.

> Historical note: an unlimited course initially showed an ERROR state because
> the parser required *both* quota and current to be numeric and discarded the
> row otherwise. That, combined with Gotcha 1 (lowercase `abbr` returning
> "No Such Course"), produced the confusing "no quota rows" error for IE 495.01.

---

## Charset is iso8859-9, not UTF-8

The page declares `charset=iso8859-9` (Latin-5). Decoding the bytes as UTF-8
corrupts Turkish characters. The network layer decodes using the declared
charset (HTTP header, then `<meta>` tag) and only falls back to UTF-8 as a last
resort. See `decodeBody()` in [src/network.cpp](src/network.cpp).

---

## Response shapes — quick reference

**Course offered, normal quota:**

```html
<strong>HTR 312.01</strong>
<tr class='schtd'><td>...ALL</td><td>...ALL</td><td>...45</td><td>...46</td></tr>
```

**Course offered, unlimited quota:**

```html
<strong>IE 495.01</strong>
<tr class='schtd'><td>...ALL</td><td>...ALL</td><td>...Unlimited</td><td>...102</td></tr>
```

**Course not offered this term:**

```html
<center>No Such Course In This Semester...</center>
```

---

## HTML quirks the parser must tolerate

The returned HTML is old, hand-written-style markup and is **not** clean or
well-formed by modern standards. The parser ([src/parser.cpp](src/parser.cpp))
currently handles the following:

- **Single-quoted attributes.** Rows are `class='schtd'`, not `class="schtd"`.
  The row regex accepts either quote style: `['"]schtd['"]`.
- **Inner markup inside every cell.** Each `<td>` wraps its value in a
  `<p align=center>` (often unclosed) plus padding, e.g.
  `<td width='10%'><p align=center>45&nbsp;</td>`. `cleanHtmlText()` strips all
  tags (`<[^>]*>`) so only `45` remains.
- **`&nbsp;` padding everywhere.** Almost every cell ends with `&nbsp;`. It is
  converted to a normal space up front (and again per cell) so it never pollutes
  the parsed text or breaks number extraction.
- **Mixed / inconsistent tag case.** Tags appear as `<TITLE>`, `<BODY>`,
  `<strong>`, `<TR>` interchangeably. All regexes use
  `CaseInsensitiveOption`.
- **Values split across lines / whitespace runs.** Regexes use
  `DotMatchesEverythingOption` so `.` spans newlines, and `simplified()`
  collapses internal whitespace.
- **Header rows that look like data rows.** The table has header rows
  (`class='title'`, `class='rectitle'`) with the same `<td>` shape. They are
  excluded two ways: (1) the row regex only matches `class='schtd'`, and (2) as a
  backstop, a row whose *Current* cell is non-numeric is treated as a header and
  skipped (see the "current must be numeric" rule).
- **Non-numeric quota values.** The Quota cell can be the literal word
  `Unlimited` (see the unlimited-quota section). The parser distinguishes
  "is a number" from "is text" rather than forcing every cell to an int.

Because this is regex-based screen-scraping of a rendered page (not API
consumption), it is intentionally narrow: it works precisely because BOUN's
output is simple, machine-generated, and stable. If the table is ever
restructured, these patterns have to be updated. The non-greedy `(.*?)` captures are the main guard against over-matching on the current markup.

---

## Popup / form flow (context)

In the browser the quota search lives in a popup/iframe loaded from:

```
GET https://registration.boun.edu.tr/quotaentry.htm
```

This page only hosts the search **form**; it contains no quota data. The form's
submit is the `POST /scripts/quotasearch.asp` documented above, and the POST's
HTML response is rendered back into the iframe. This app does **not** need to GET
`quotaentry.htm` at all — it POSTs directly. The `Referer` header is set to
`quotaentry.htm` only to mimic the normal browser flow; no session from that page
is required.

## Browser CORS: why the WASM/Pages build needs a proxy

The native app talks to BOUN over the OS network stack — no browser, no
same-origin policy — so direct POSTs work.

The WebAssembly build runs **inside the browser**, where every request is subject
to CORS. The Pages origin (e.g. `https://sueda-okutan.github.io`) and the target
(`registration.boun.edu.tr`) are different origins, and BOUN sends **no**
`Access-Control-Allow-Origin` header. The browser therefore blocks the response,
and the app surfaces it as a "network request failed / CORS" error on **every**
query. It is worse than a simple GET: the POST carries
`Content-Type: application/x-www-form-urlencoded`, so the browser first sends a
**preflight `OPTIONS`** request that BOUN's IIS does not answer.

This is **not fixable from the client.** CORS is enforced by the browser and can
only be granted by the server. The solution is a proxy **you control**:

- The browser app POSTs to your proxy (same trust boundary).
- The proxy forwards to BOUN **server-to-server** (no CORS between servers).
- The proxy returns BOUN's raw bytes plus `Access-Control-Allow-Origin`, so the
  browser accepts the response.

A ready-to-deploy Cloudflare Worker lives in [proxy/](proxy/). It must forward the
body as **raw bytes** to preserve `iso8859-9` (re-encoding to UTF-8 would corrupt
Turkish text) and must reflect/allow the Pages origin. The WASM build's endpoint
is switched to the proxy URL at compile time via `BOUN_QUOTA_PROXY_URL` (see
`quotaEndpoint()` in [src/network.cpp](src/network.cpp) and the CMake variable of
the same name). The WASM build also must **not** set `Origin`/`Referer`/
`User-Agent` headers — those are forbidden request headers in the browser and the
proxy supplies them instead.

---

## Diagnostics

When parsing yields no rows, the network layer writes the raw response body to
`<temp>/boun_quota_last_response.html` (best-effort) and includes the HTTP
status, byte count, and any redirect target in the error message; keep it when debugging future "no rows"
reports.
