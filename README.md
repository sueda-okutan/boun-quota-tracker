# BOUN Quota Tracker

A C++17 / Qt 6 desktop and WebAssembly application that monitors Boğaziçi
University course quota availability and visibly notifies you when a seat opens.

> Only quota availability is monitored periodically. Registration is not automated! No log in, register, enroll etc.

---

## Features

- **Input:** Course name(s) in the format of [ABBRV]<CODE>.<SECTION_NO> (e.g. TK221.01) (one course per line).
- Periodic polling (configurable (minimum 30 seconds)) plus manual refresh.
- Visible UI notification on a `full` to `available` transition: highlighted row, status label, window title change, and `QApplication::beep()` (plus an optional system tray notification on native desktop).
- Native desktop build and Qt WebAssembly / GitHub Pages deployment.

### Output

Each monitored course is one row in the results table, with the following columns:

| Column | Meaning |
| --- | --- |
| **Course** | The normalized course label. |
| **Department** | Department(s) the quota row(s) apply to (e.g. `ALL`). |
| **Status** | Quota status/restriction (e.g. `ALL`, `RESTRICTED`). |
| **Quota** | Total quota, or `Unlimited` for an unrestricted course. |
| **Current** | Number currently enrolled. |
| **Available** | Free seats (`Quota − Current`), or `Unlimited`. |
| **State** | `AVAILABLE`, `FULL`, or `ERROR` (see below). |
| **Last Checked** | Time of the most recent poll for this course. |
| **Error** | Error message if the State is `ERROR` (hover for the full text). |

### State colors

| State | Color | Meaning |
| --- | --- | --- |
| `AVAILABLE` | 🟩 green | `Quota > Current`, or `Unlimited` |
| `FULL` | 🟥 red | Course found but no free seats. |
| `ERROR` | 🟨 amber | `ERROR` |

### Errors

The **Error** column explanations and their causes shown on a failed query are as the following:

| Error | Cause |
| --- | --- |
| `Course not offered in this semester.` | (BOUN returns "No Such Course"). |
| `No quota rows found in response.` | A response with no parseable quota rows. |
| `Network request failed: …` | the HTTP request failed (host unreachable, timeout, etc.). |
| `Network request failed. … CORS restrictions …` | WASM/GitHub Pages build: blocked by the browser — see the CORS section. |
| `HTTP error: <code>` | The server returned a non-200 status. |
| `Empty quota response (HTTP …, … bytes)` | A 200 response with an empty body. |

### Status bar

The **bottom-left corner** bar reports the app's current action:

| Message | When |
| --- | --- |
| `Idle.` | On startup, before any monitoring. |
| `Monitoring N course(s) every Ss.` | After pressing **Start**. |
| `Refreshing...` | After pressing **Refresh now**. |
| `Stopped.` | After pressing **Stop**. |
| `Ignored invalid line(s): …` | Some input lines could not be parsed. |
| `No valid courses to monitor.` | Start/Refresh pressed with no parseable courses. |
| `Seat available: COURSE (N seat(s))` | A monitored course just went from full to available — also changes the window title (prefixed with ✅) and beeps. |

---

## Project layout

```
boun-quota-tracker/
├── CMakeLists.txt        # build config (incl. BOUN_QUOTA_PROXY_URL)
├── README.md
├── src/
│   ├── main.cpp          # entry point
│   ├── mainwindow.*      # UI only
│   ├── models.h          # shared data structures
│   ├── parser.*          # HTML parsing only
│   ├── network.*         # HTTP only
│   └── logic.*           # monitoring/orchestration only
├── tests/                # parser + POST-body tests (no network)
├── proxy/                # Cloudflare Worker CORS proxy for the WASM build
│   ├── worker.js
│   ├── wrangler.toml
│   └── README.md
└── .github/workflows/    # build.yml, deploy-wasm.yml
```

---

## Course input format

Enter one course per line. The following forms are all accepted and normalized:

```
TK 221.01      ->  TK 221.01
TK221.01       ->  TK 221.01
TK 221 01      ->  TK 221.01
HTR 312.01     ->  HTR 312.01
CMPE 150.02    ->  CMPE 150.02
cmpe150.2      ->  CMPE 150.02
tk 221 1       ->  TK 221.01
```
- Whitespace is trimmed. The department code is uppercased (for both display and the POST body). The section is normalized to two digits.
- Courses that have no match are reported in the status bar.

## Semester
The app shows the current term in a
read-only **Semester** label (format `YYYY/YYYY-T`, e.g. `2025/2026-3`). The value is parsed from the server's first response.

## Polling interval

- Default interval: **60 seconds**
- Minimum interval: **30 seconds**

You can also press **Refresh now** for an immediate check. A notification is raised **only** when a course goes from unavailable to available - it does not fire on the first poll, if available already.

---

## Network flow

The quota form popup is opened in the browser with:

```
GET https://registration.boun.edu.tr/quotaentry.htm
```

The actual quota search is a POST:

```
POST https://registration.boun.edu.tr/scripts/quotasearch.asp
```

### POST Body

```
abbr=<UPPERCASE department>&code=<course number>&section=<two-digit section>
```

For example:

```
TK 221.01    ->  abbr=TK&code=221&section=01
HTR 312.01   ->  abbr=HTR&code=312&section=01
IE 495.01    ->  abbr=IE&code=495&section=01
```

>**Case-sensitive abbreviations:** the department code must be sent **UPPERCASE**. The server's stored department code is case-sensitive. Lowercase happens to match for some courses (e.g. `tk`, `htr`) but **not** others (e.g. `ie`), which return "No Such Course In This Semester...".

No cookies or session IDs are sent or stored.

For the full list of endpoint quirks see [ENDPOINT_NOTES.md](ENDPOINT_NOTES.md).

### How the response is read

The result seen on the browser is the **HTML response body of the POST request**. The app reads that same HTML via `QNetworkReply::readAll()` and parses quota rows.

---

## GitHub Pages / WebAssembly CORS limitation

The WebAssembly build runs entirely inside the browser, therefore a `POST` request from a GitHub Pages origin to `registration.boun.edu.tr`
is **blocked**, because BOUN returns no `Access-Control-Allow-Origin` header. Because of this a proxy
grants permission. The **native desktop app is unaffected** and always queries
BOUN directly.

To make the deployed WASM build work, queries are routed through a small proxy — a Cloudflare Worker in [proxy/](proxy/). The proxy forwards the request to BOUN server-side and returns the response with CORS headers added.

It is not an open proxy: it forwards only to the one fixed BOUN quota endpoint, for origins you allow.

The WASM build's quota endpoint is set at compile time via the
`BOUN_QUOTA_PROXY_URL` CMake variable, which **defaults** to the deployed proxy URL in [CMakeLists.txt](CMakeLists.txt) — so the GitHub Pages build works out of
the box. To point it at a different proxy, either:

- override at configure time:
  `-DBOUN_QUOTA_PROXY_URL="https://<your-worker>.workers.dev"`, or
- set a repository **variable** named `BOUN_QUOTA_PROXY_URL` (Settings → Secrets
  and variables → Actions → Variables); the deploy workflow passes it and it
  overrides the default.

See [proxy/README.md](proxy/README.md) for updating the Worker. 

The system tray notification (`QSystemTrayIcon`) is disabled in WebAssembly; only the changes in UI (row highlight, status, title, beep) are used as notification.

---

## Building — native desktop

Requirements: CMake ≥ 3.20 and Qt 6 (Core, Widgets, Network; Test for the test
suite).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure   # run the tests
./build/boun_quota_tracker                    # run the app
```

## Building — Qt WebAssembly

Requirements: a Qt for WebAssembly installation and a matching Emscripten SDK.

```bash
source /path/to/emsdk/emsdk_env.sh
/path/to/qt-wasm/bin/qt-cmake -G Ninja -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm --parallel
```

This produces `boun_quota_tracker.html` (+ `.js`/`.wasm`) which can be served over HTTP. The `deploy-wasm.yml` workflow builds this and publishes it to GitHub Pages (the entry page is renamed to `index.html`).

---

## Continuous integration

- **`.github/workflows/build.yml`** — configures, builds, and runs the tests on Ubuntu for every push and pull request.
- **`.github/workflows/deploy-wasm.yml`** — builds the WebAssembly target and deploys it to GitHub Pages on pushes to `main`.

---

## Tests

The tests live in `tests/` and never contact the live BOUN website. They cover:

- parsing a full course, an available course, and multiple quota rows,
- handling responses with no quota rows,
- parsing the real captured response HTML (with inline `<p align=center>` markup),
- POST-body construction (`abbr` uppercase, two-digit `section`, no semester),
- free-text course-line normalization,
- the `quota > current` availability rule.

---
