# Save sync: how the existing clients work, and how the port joins them

Research for issue #55 (parity: save sync). Everything below was read from the
`zeliard-wasm` repo (`~/git/zeliard-wasm`, cited as `wasm:`) unless marked
**[inferred]**. Nothing here is implemented in the port yet.

Code locations:

- Server: `wasm:docker/saves-api.py` (Python stdlib, 360 lines), fronted by
  `wasm:docker/default.conf` (nginx proxies `/api/` to 127.0.0.1:8080). Baked
  into the image by `wasm:Dockerfile:23`. There is no separate sync repo.
- Web client: `wasm:js/app.js` (sync section from line 896), `wasm:js/sync-config.js`.
- Desktop client (Go): `wasm:native/launcher/core/sync.go`, `link.go`,
  `states.go`; CLI in `wasm:native/launcher/cli.go`.
- Android client (Kotlin): `wasm:android-native/app/src/main/java/com/awkto/zeliard/SaveSync.kt`.
- Deployment on pro: container `zeliard` (image `awkto/zeliard-wasm:latest`,
  127.0.0.1:5025), volume `zeliard-saves` mounted at `/saves`, nginx vhost
  `/etc/nginx/sites-available/zeliard.pro.dnsif.ca`. Live check:
  `GET /api/health` returned `{"ok": true, "auth": "none"}` (no `SYNC_TOKEN` set
  there). Another deployment is named in `wasm:js/sync-config.js:10`
  (`https://zeliard.box.dnsif.ca/`, only a comment).

## 1. Endpoints

All under the server base URL (same origin for the web build; a configurable
`sync_base` for native, `wasm:native/launcher/core/settings.go:25`). Base is
normalised to end in `/` (`wasm:native/launcher/core/sync.go:106-111`).

Auth, common to every `/api/saves*` route:

- `X-Client-Id: <key>` is the sync key. Must match `^[A-Za-z0-9_-]{1,64}$`, else
  400 "bad client id" (`saves-api.py:51,192-194,229-231`). Note the regex allows
  `-`, which is why dashed web keys (`ABCD-EFGH`) are legal.
- `Authorization: Bearer <token>` only when the server runs with `SYNC_TOKEN`;
  otherwise open (`saves-api.py:43-47,196-200`). Wrong or missing token: 401
  "token required" (text/plain). Clients send the header only if a token is set
  (`wasm:js/app.js:920-925`, `sync.go:201-211`).
- Slot names match `^[A-Za-z0-9_-]{1,32}$`, else 400 "bad slot" (`saves-api.py:52`).
  No dots, so the DOSBox version cannot appear in a slot name (`states.go:84-91`).
- CORS is wide open (`Access-Control-Allow-Origin: *`, OPTIONS handled) because
  the Android WebView is cross-origin (`saves-api.py:179-181,212-219`). Every
  response has `Cache-Control: no-store`.

| Method | Path | Auth | Request | Response |
|---|---|---|---|---|
| GET | `/api/health` | none (public even with a token) | - | 200 `{"ok": true, "auth": "token"\|"none"}` (`saves-api.py:222-224`). Anything other than exactly `ok === true` means "not our server" to the web client (`wasm:js/app.js:990-1002`). A 404 means a static host, feature hidden. |
| GET | `/api/saves` | key (+token) | - | 200 JSON array `[{"slot","modified","size"}]`, sorted by filename, empty array for an unknown key (`saves-api.py:233-249`). `modified` is epoch ms. |
| GET | `/api/saves/<slot>` | key | - | 200 raw bytes (`application/octet-stream`) plus header `X-Save-Modified: <ms>` (exposed to CORS); 404 "no such slot" (`saves-api.py:254-263`) |
| PUT | `/api/saves/<slot>` | key | body = raw bytes, `Content-Length` required, 1..16 MiB (env `SAVES_MAX_BYTES`); header `X-Save-Modified: <epoch ms>` (non-numeric becomes 0) | 200 `{"slot","modified","size"}`; 413 "bad body size" if length is 0 or too big (`saves-api.py:314-328`). Atomic write (tmp + rename, `saves-api.py:155-165`). nginx allows `client_max_body_size 16m` (`default.conf:21`). |
| DELETE | `/api/saves/<slot>` | key | - | 200 `{"deleted": slot}` (also when absent); archived to history first (`saves-api.py:330-346`) |
| GET | `/api/saves/<slot>/history` | key | - | 200 JSON `[{"id","modified","size","replaced"}]`, newest-arrived first (`saves-api.py:265-266,133-152`). `replaced` is a server-clock ms, display only. |
| GET | `/api/saves/<slot>/history/<id>` | key | `<id>` matches `^\d{1,20}-\d{1,6}$` | 200 raw bytes + `X-Save-Modified`; 400 "bad history id"; 404 "no such version" (`saves-api.py:268-278`) |
| PUT | `/api/saves/<slot>/restore/<id>` | key | empty body | 200 `{"slot","restored","modified","size"}`; 404 if the version is gone (`saves-api.py:295-313`) |

There is no listing across keys, no compare-and-swap, no ETag, no
`If-Unmodified-Since`: every PUT is last-writer-wins on the server
(`saves-api.py:326-328`). All conflict avoidance is client-side (section 4).

Server storage per key: `<SAVE_DIR>/<key>/<slot>.bin` + `<slot>.json`
(`{"modified","size"}`) and `<key>/.history/<slot>/<modified>-<seq>.{bin,json}`
(`saves-api.py:58-79,155-165`). Defaults: `SAVE_DIR=/saves`, history keeps the
last 10 versions per slot (`SAVES_HISTORY`, `saves-api.py:40-49`).

## 2. The key scheme ("web-style keys")

- A key is the only identity. There are no accounts. It is "unguessable by
  being long enough to not be found", not a secret in the cryptographic sense
  (`saves-api.py:4-10`). Anyone who knows it can read, overwrite and delete.
- Generation: 4 characters from `ABCDEFGHJKLMNPQRSTUVWXYZ23456789` (no I, O, 0,
  1), drawn with `crypto.getRandomValues` in the browser
  (`wasm:js/app.js:930-937`) and `crypto/rand` in Go (`sync.go:175-191`).
  Example `K7QF`. That is only 32^4 (about 1M) values, so security rests on
  the server being private or `SYNC_TOKEN` being set. **[inferred]** The comment
  at `saves-api.py:8` calls it "unguessable"; with 4 characters it is not.
- Normalisation (identical in web and Go, `app.js:943-947`, `sync.go:133-155`):
  uppercase, strip everything except A-Z/0-9, accept 4..32 chars, then insert a
  `-` every 4 characters (`ABCD-EFGH`). The normalised form is what goes into
  `X-Client-Id`. Legacy native keys `zn-...` are used verbatim if already in
  the `sync-key` file and are valid on the wire but cannot be typed into the
  browser (`sync.go:128-131,170-173`).
- Storage: web `localStorage["zeliard.syncId"]`, created lazily on first read
  (`app.js:938-942`). Native: `settings.ini` `sync_key` wins, else file
  `<ConfigDir>/sync-key` (mode 0600), else a fresh key is generated and written
  there (`sync.go:157-192`). ConfigDir is `$XDG_CONFIG_HOME/zeliard` or
  `~/.config/zeliard` (`paths_unix.go:89-99`).
- Shown to the user: the Server sync card (`#sync-id`, a Copy button), and
  `zeliard sync key` on the CLI (`cli.go:41`).
- Linking a second device: type or paste the key into "Link another device"
  (`app.js:1156` `linkToKey`) or `zeliard sync key <KEY>` (`cli.go:40`). The
  device then adopts the key and asks before touching either side (section 4).
- Disconnect: "Stop syncing on this device" turns sync off, deletes the
  stored key and the "last synced" stamp, keeps the local save, and keeps the
  server copy (`app.js:1299-1308`). `zeliard sync forget-key` does the same
  natively (`cli.go:42`). `delete-remote --yes` is the only way to remove the
  cloud copy (`cli.go:44`, `sync.go:1009-1024`).
- Optional token: `sync_token` in settings.ini, `Sync token` field in web
  settings (`localStorage["zeliard.syncToken"]`, `app.js:918-919`).
- Sync is opt-in and off by default (`app.js:954-956`); native default is
  `Sync=false` (`settings.go:30-47`, zero value), `SyncStates` defaults true.

## 3. What is synced

There are two kinds of server slot.

### 3.1 Slot `auto`: the town saves (the cross-play slot)

One slot per key, named `auto` (`app.js:904`, `sync.go:8`). Its blob is a
**bootable `.jsdos` bundle**, i.e. a ZIP, produced by the web app by capturing
the emulator filesystem (`app.js:75-80`). Contents (`sync.go:8-14,313-364`):

| Zip entry | Owner | Notes |
|---|---|---|
| `<NAME>.USR` at the root | **owned**, the town saves | Zeliard's own save files, raw 256-byte images of `BASE:0000` (`docs/ROADMAP.md:55`, `port/README.md:1499`). One zip entry per save "slot", names upper-case (`sync.go:347`). |
| other root files (`ZELIARD.EXE`, `*.SAR`, `*.BIN`, ...) | owned | The shipped install, identical everywhere; only `.USR` carries progress (`sync.go:92-97`). Files starting `ZMENU` are ignored (`sync.go:55-57`). |
| `dosbox.conf`, `.jsdos/dosbox.conf` | foreign | js-dos metadata; the Go client ships a canonical copy under `share/zeliard/jsdos-meta` and writes it only if the remote bundle had none (`sync.go:601-621`). |
| `save/1.sav` (and anything nested) | foreign | The web app's DOSBox-X save state, kept inside the same bundle. Native cannot read or write it and must carry it through every push untouched (`sync.go:313-364,590-600`). |

Metadata: the only server-visible metadata is the whole-bundle `modified`
(epoch ms, client-supplied) and `size`. Per-save timestamps live inside the zip
as each entry's mtime (zip has 2 s resolution, hence `timeSlack = 2 s`,
`sync.go:99-103`). There are no slot display names: a "slot" is a `NAME.USR`
file name chosen in game (the sage's "Record Experience").

So "per-slot town saves" means per `.USR` entry inside `auto`, not separate
server slots. The server cannot see them.

How `modified` is derived:

- Web: `Date.now()` whenever the captured filesystem signature changes
  (`app.js:963-969,975-989`).
- Native/Go: newest mtime of any non-ZMENU file in the game dir
  (`sync.go:266-288`).
- After a pull, the extracted files are stamped with the server's `modified`
  so the pull does not look like a local edit (`sync.go:406-409,780-791`).

### 3.2 Slots `state-<hash>-<n>`: DOSBox-X quicksaves (desktop only)

Each `<n>.sav` in the native states dir is its own slot, e.g.
`state-1a2b3c4d5e-1`. `<hash>` is the first 5 bytes of SHA-256 over the four
DOSBox-X guard entries (`DOSBox-X_Version`, `Program_Name`, `Memory_Size`,
`Machine_Type`) joined by `\n`, hex-encoded (`states.go:84-94`). Result: states
only meet between builds whose emulator would accept them; the OS is not in the
key (`states.go:15-24`). Number is 1-3 digits (`states.go:147-157`). Blobs are
~130 KB zips validated end to end (CRCs) before upload or install, to defeat
the truncated-state crash (`states.go:26-35,96-137`). Per-slot `modified` is the
file mtime (`states.go:266,284`).

The web app's quicksave is **not** a `state-*` slot; it lives inside `auto` as
`save/1.sav` (above). Dead slots `native-game` and `native-states` are deleted
on first contact (`sync.go:414-428`).

## 4. Conflict, undo, never overwrite silently

Clients keep two local stamps per key and compare them with the server's:

- Web: `localModified` and `lastSynced` in localStorage (`app.js:963-969`).
  `syncState()` (`app.js:1018-1037`) classifies: empty, local-only,
  server-only, in-sync, local-dirty (`modified > synced`), server-new
  (`remote.modified > synced`), diverged (both).
- Native: `sync-links.json` in ConfigDir stores `last_synced` per key
  (`link.go:25-60`).

Rules:

1. **Ask before linking.** A key the device has never synced with, that already
   holds a save on the server (`remote != 0 && !IsLinked`), is never resolved
   automatically: "That is not a conflict-resolution rule, it is a coin toss"
   (`link.go:3-16,91-95`). `ShouldPull` and `ShouldPush` both refuse
   (`link.go:100-127`). Native prints how to choose (`cli.go:168-203`):
   `zeliard sync pull --force` (take server; backs up local first via
   `AdoptCloud`, `sync.go:907-943`) or `zeliard sync push --force` (keep local,
   `AdoptLocal`, `sync.go:945-1001`). The settings GUI uses the same calls.
   Web: if both sides have a save, `linkToKey` opens the conflict modal
   "Keep cloud save / Keep this browser"; if only one side has one it syncs
   silently in the obvious direction (`app.js:1156-1197`, `1199-1220`).
   State sync also waits while that decision is pending (`states.go:171-175,279-281`).
2. **Newer-wins once linked.** Pull if `remote > local`, push if
   `local > remote` (`link.go:110,126`). Comparisons are client clock against
   client clock; the server clock is never used for them (`saves-api.py:22-24`).
3. **Never auto-pull right after a session.** `FinalSync` pushes if the session
   is newer; if the server moved ahead while playing it only reports and
   leaves both alone ("another device wrote while you were playing")
   (`sync.go:531-565`). Web equivalent: the Play guard modal for `server-new` and
   `diverged` (`app.js:1316-1346`, text at 1329-1337).
4. **Per-save union, not blanket bundle replace.** `zipBundle` keeps, for each
   `.USR`, whichever copy is newer (with the 2 s slack) and carries through any
   server-side `.USR` this machine has never seen; `unzipBundle` leaves a
   local `.USR` alone if it is newer than the bundle's entry (`sync.go:567-685,730-778`).
   Reason: a stale device wiped six slots on 2026-08-15 (`sync.go:573-579,945-951`).
   Cost: a deletion in game can be undone by another device re-supplying the
   file (`sync.go:580-581`). The upload is stamped with `max(local, newest
   carried)` so the bundle is not under-reported (`sync.go:483-489`).
5. **Never push over a bundle you have not read.** Before PUT, the client
   downloads the current `auto` and records its foreign entries, else it
   skips the push (`sync.go:464-475`).
6. **Backup before irreversible local overwrite.** `BackupGameDir` zips the game
   dir to `<DataDir>/backups/game-<tag>-<timestamp>.zip` (`sync.go:886-905`).
   `zeliard saves backup` is that same function; with `--exit-code` it exits 3
   when there are no saves (`cli.go:432-441`, `430`). `saves clear` is refused
   while sync is on (`cli.go:50`).
7. **Server-side undo.** Every PUT, DELETE and restore first copies the current
   blob to `.history` (`saves-api.py:26-30,90-111,326,340`). `GET .../history`
   lists, `PUT .../restore/<id>` promotes one, stamping it
   `max(now, current+1)` so every other device sees it as newer and pulls it
   instead of pushing over it (`saves-api.py:304-313`). A restore is itself
   undoable. Only the Android client calls these (`SaveSync.kt:662-703`); the
   web and Go clients never do (grep of `app.js` and `core/*.go` finds no
   `history` or `restore` calls). **Per slot:** the `state-*` slots are
   historied too, since the server code is slot-agnostic.
8. **Disconnect is non-destructive**; cloud delete needs explicit confirmation
   and also removes `state-*` slots (`sync.go:1003-1024`, `app.js:1299`).

## 5. Client behaviour: when it pushes and pulls

Web (`app.js`):

| Trigger | Action | Cite |
|---|---|---|
| Page load, server detected | `autoSyncOnStart`: pull only if state is `server-new` or `server-only` (never when diverged) | 2348-2350, 1078-1088 |
| Press Play | `server-only`: pull; `server-new` or `diverged`: ask via modal; else start | 1316-1327 |
| While playing, every 60 s | `syncTick`: capture the emulator FS, push only if the signature changed or `modified > synced` | 240-255, 343 |
| Quicksave | capture and push 700 ms later | 693-697 |
| Return to launcher / exit | capture, push if changed | 349-351 |
| Tab hidden or `pagehide` | best-effort capture + push | 2307-2311 |
| Save import | `modified = now`, push | 866-871 |
| Tap the sync indicator | forced check + push | 2300 |
| Link / choose in modal | pull or push per the choice | 1156-1220 |

Native Go (`wasm:native/launcher/main.go:243-270`, `sync.go:516-565`,
`cli.go:168-203`):

| Trigger | Action |
|---|---|
| Before the emulator starts | `Pull()` then `PullStates()` (never while DOSBox-X runs, so no file races) |
| Every 2 min while the game runs (`syncPeriod`) | `Push()` of `auto` only; never a pull, never states |
| Emulator exited | `FinalSync()`: push if newer, report if the server is ahead; then `PushStates()` |
| `zeliard sync` | pull, pullstates, push, pushstates (skipped if a link decision is pending) |

Offline behaviour (same in all clients): every operation first calls
`/api/health`; on any network error or non-200 the step is silently skipped
and logged, the game runs from local files, and the stamps keep recording
"dirty" so the next successful push picks it up. There is no retry queue and
no timer beyond the next scheduled trigger. HTTP timeout is 120 s on native
(`sync.go:121`). Web shows `Offline` on the indicator (`app.js:251`). The server
being unreachable never blocks start, but the Go launcher waits up to 45 s for
the final push (`main.go:265-270`).

## 6. Proposed mapping for the C port

Everything in this section is **[inferred]**, a proposal, not read from existing
behaviour.

### 6.1 Where files live

- Port town saves: `<name>.usr` in the game folder, `sh->dir`
  (`port/main.c:1451,1636`, `port/shell.c:281`, `player_save_usr` at
  `port/player.h:20`). The F7 browser lists `*.usr` there
  (`port/README.md:112-114`). Case: the port writes lower-case `.usr`; the
  server bundle uses upper-case entry names (`sync.go:347`). Sync must map
  `name.usr` <-> `NAME.USR` case-insensitively and write back in whatever case
  the port's lister expects. The file contents are byte-identical to the
  DOS ones (`port/README.md:1499-1503`), so no conversion is needed.
- Config: `SDL_GetPrefPath("awkto","zeliard")/zeliard.ini`, fallback
  `$XDG_CONFIG_HOME/zeliard/zeliard.ini` or `~/.config/...`
  (`port/main.c:1143-1152`, `port/config.c:195-197`). Put `sync`, `sync_base`,
  `sync_key`, `sync_token` there, with the same names as native's settings.ini.
  Keep the `sync-key` file and `sync-links.json` next to it (`last_synced` per
  key), so a machine that has both the DOSBox build and the port could
  **share the link record**, if the port is pointed at the same config dir.
  Decide whether to (open question 3).
- Realtime states: `<pref>/states/quick.zst`, `slot1.zst`..`slot9.zst`
  (`port/statekeys.c:23-33,49-54`, `port/statekeys.h:49`). Format `ZELSTATE`
  v1, checksummed, engine-only (`port/savestate.h:1-19`).

### 6.2 Port-only state kind

Server slot name `port-state-<ver>-<n>`, where `<ver>` is a short hex of
`SAVESTATE_VERSION` (`port/savestate.h:19`) so a future format bump gets its
own namespace, exactly as `states.go` keys on the emulator guard. `<n>` is `quick` for `quick.zst` or `1..9` for `slotN.zst`. The prefix `port-state-` can never match the Go client's `state-` prefix (`states.go:177-181`), so DOSBox builds ignore these slots. Keep it under 32 chars: `port-state-1-quick` is 18. Rules copied from
`states.go`: pull only before play, push only after exit or at a safe
boundary, validate the blob (magic, version, checksum) before upload and after
download (`savestate_load` already refuses bad ones, `savestate.h:34-39`),
per-slot mtime stamping, wait while a link decision is pending. The DOSBox
builds never touch `port-state-*` (their prefix is `state-`), and `DeleteRemote`
in the Go client deletes only `state-*` (`sync.go:1011-1013`), so cloud delete
from a DOSBox client leaves port states behind; this should be fixed there or
accepted.

### 6.3 HTTP library

- Linux, Windows, macOS: libcurl (easy interface, `CURLOPT_CUSTOMREQUEST` for
  PUT/DELETE, `CURLOPT_HTTPHEADER` for `X-Client-Id`, `X-Save-Modified`,
  `Authorization`; read `X-Save-Modified` via a header callback). On Windows
  the MSYS2 MINGW64 `curl` package works but note the project memory about
  MINGW64 packages vanishing; pin and validate on a CI branch.
- Android: libcurl built into the NDK build, or an `SDL_AndroidGetJNIEnv`
  call to `HttpURLConnection`, which is what `SaveSync.kt` does (a Java thread
  already exists there). libcurl needs CA bundle handling on Android; the JNI
  route avoids it.
- Web (emscripten): `emscripten_fetch` (needs CORS, which the server already
  allows with `*`, `saves-api.py:179-181,212-219`, and sends `X-Save-Modified`
  as an exposed header). Sync must be async: there is no blocking pull "before
  start".
- Isolate behind a tiny `net.h` (`net_get`, `net_put`, `net_delete`) so the
  rest is portable and unit-testable against a fake in `tests/`.
- Zip: the bundle is a deflate ZIP, so the port needs read/write for that.
  `port/png.c:50` emits stored zlib blocks only, so it has no inflate; use
  miniz (single file, public domain/MIT) or zlib. Needed for `auto` only; the
  port-state slots are raw `ZELSTATE` bytes with no zip.

### 6.4 Plan

1. **Protocol core + `auto` pull/push (no UI).** **DONE (#55 step 1).** New `port/sync.c/.h` + `net.h`.
   Key resolve/normalise/generate (section 2), `/api/health`, `/api/saves`,
   `GET/PUT /api/saves/auto`, `link` record, `ShouldPull/ShouldPush` and the
   link-decision refusal. Zip read/write with the foreign-entry carry-through
   and the per-`.USR` union (`sync.go:567-685,730-778`), and a local backup zip
   before any overwrite. Flags: `--sync status|pull|push [--force]`,
   `--sync-key KEY`. Test: round trip against `docker run awkto/zeliard-wasm`
   plus a fixture bundle captured from the web build; assert foreign entries
   (`save/1.sav`, `.jsdos/dosbox.conf`) survive byte for byte.

   **Step 1 as built** (`port/sync.c/.h`, `net.h` + `net_curl.c` / `net_stub.c`,
   vendored `port/third_party/miniz/`, `port/test_sync.c`, `port/tools/sync_roundtrip.sh`;
   keys `sync`, `sync_base`, `sync_key`, `sync_token` in `zeliard.ini`).  Deviations
   from the plan above:
   * **Only `.usr` entries are managed.** Every other bundle entry (`save/1.sav`,
     `.jsdos/*`, `dosbox.conf`, and also the game files `ZELIARD.EXE`, `*.SAR`) is
     foreign and copied compressed-as-stored, so bytes, method and mtime survive.
     Local mtimes are taken from the `.usr` files only (the game files' mtimes mean
     "install time", open question 2).
   * **Open question 1 resolved as "refuse":** a push to a key with no `auto` bundle
     exits 6; the port never creates one (a `.usr`-only bundle could replace the web
     app's whole local bundle on its next pull).
   * `NAME.USR` entries are written upper-case; a pull writes `name.usr`, reusing the
     case of an existing file. Entries with a base name over 8 characters are carried
     but not extracted. Backup (`<config dir>/backups/game-<tag>-<time>.zip`) holds
     the `.usr` files only, made before a pull that would change an existing file
     and always before `pull --force`.
   * `--sync-key KEY` is remembered in `<config dir>/sync-key`, never in `zeliard.ini`;
     the link record is `sync-links.json` beside it (Go schema, so it can be shared).
     `sync_base` has no default; `--sync` works whether or not `sync=1`.
   * Health requires `"ok": true` in the body (the web client's rule), not just a 200.
   * Exit codes: 0 ok, 1 error, 3 unlinked key (decision needed), 4 offline, 5 built
     without libcurl, 6 no bundle on the server.
   * libcurl is optional (pkg-config); without it `--sync` says "built without network
     support".  The Windows build links `net_winhttp.c` (WinHTTP, `make WINHTTP=1`, #57).
     The test round trip uses a local `saves-api.py`, not `docker run`, and a
     generated fixture bundle rather than a capture from the web build.
2. **Lifecycle hooks + ask-before-link UI.** **DONE (#55 step 2, see port/README.md "Save sync").** Pull before the first town load
   (before `shell_init` reads the dir, so nothing races), push after each
   `player_save_usr` (the port writes a `.usr` rarely, so push-on-save replaces
   the 2-minute timer), final push at exit, offline = skip and log. The
   unlinked-key case shows a modal using the existing text-box/restore dialog
   machinery (`port/restore.c`) with the same two choices as the web modal
   (keep cloud / keep this device) and never decides on its own. Acceptance for
   #55: save in the emulated web build, load that `.usr` in the port on another
   machine (F7), and the reverse (open the web app, confirm the `.USR` loads
   in the DOS game via its F7 Restore, as in `docs/DOSBOX_RECIPE.md` section 9).
   **Step 2 as built** (`port/synclife.c/.h`, `syncdlg.c/.h`, glue in `main.c`, one hook
   `player_usr_saved_hook` in `player.c`, `net_set_timeouts` in `net.h`): active only
   in an interactive SDL run with `sync = on`; never in `--script/--trace/--save/
   --screenshot/--headless/--pilot/--state-*` runs.  Pull before `shell_init`
   (3 s connect, 10 s total).  An unlinked key with a server save and local saves
   shows the modal after the window opens and before the first town/cutscene; a
   folder with no saves takes the server's without asking.  Push per save on an SDL
   thread (coalesced, 3 s / 20 s), final push at exit if one is unsent.  Offline:
   one log line, play on.  Hard errors (401) turn sync off for the session.
   The two-machine F7 acceptance run is not done here.
3. **Port-only realtime states + undo.** Push/pull `port-state-*` slots (6.2)
   from the `states/` dir, after #52's `quick.zst` / `slotN.zst`. Add
   `--sync history` and `--sync restore ID`, mirroring `SaveSync.kt:662-703`,
   so the port also has the server-side undo the other clients have. Emscripten
   fetch support lands here or as a separate step.

## Open questions

1. **Can the port ever create a bootable `auto` bundle by itself?** Pushing to
   an empty server key needs the canonical `.jsdos/dosbox.conf` and root
   `dosbox.conf` (`sync.go:601-621`), which the port does not have. Options:
   embed the two small text files in the port; or refuse to create `auto` and
   only push when one exists; or send a bundle with just the `.USR` entries
   (the web client would not boot it, **[inferred]**).
2. **Which clock for `modified`?** Native uses file mtime
   (`sync.go:266-288`). The port should do the same for cross-play, but
   saving into a "fresh install" game dir (whose mtimes mean "install time")
   needs the same bundled-mtime preservation as `syncGameFiles`
   (`wasm:native/launcher/main.go:36,73-76`), otherwise a new install looks
   newer than a real save.
3. **Shared link record with the DOSBox native launcher?** Config dirs both
   resolve to `.../zeliard`, so they could collide on `sync-key`,
   `sync-links.json` and `settings.ini` (the port uses `zeliard.ini`, so that
   one is safe, `port/main.c:1148`). Decide whether sharing is wanted; if the
   Go launcher and the port share `sync-links.json`, the schema must stay
   compatible (`link.go:25-33`).
4. **The 4-character key is guessable** (about 1M values); an open server
   (`auth: none`, as on pro) lets anyone enumerate and overwrite. Out of scope for
   #55 but worth a ticket; mitigation is `SYNC_TOKEN` and the server history,
   which only the Android client exposes.
5. **Does the web client need to merge per `.USR`?** The web app replaces its
   entire local bundle on pull (`app.js:1060-1073`), so a `.USR` that the port
   added to `auto` survives only if the port's push carried the web app's own
   entries through (it does by design, `sync.go:313-364`). Whether js-dos
   really restores a `.USR` added to the bundle by a non-browser writer was not
   tested here **[inferred from the existing Go client doing the same]**.
6. **Port `.usr` filename rules.** Bundle entry names come from DOS 8.3 names;
   the port accepts `--name NAME` (8 chars) (`port/main.c:467`). Confirm
   the port never writes names with characters illegal in a zip root or in DOS
   (the F7 box has an 8-slot name box, `port/README.md:114`).
7. **Server contract gaps to design around:** no compare-and-swap, so two
   devices pushing within the same poll window race and the loser is only
   recoverable through history (`saves-api.py:326-328`); client-clock skew
   between machines is unmitigated except by `timeSlack` (2 s).
