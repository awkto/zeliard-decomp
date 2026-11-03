#!/bin/bash
# sync_roundtrip.sh -- #55 steps 1-2 end to end: the real zeliard binary (libcurl)
# against a LOCAL copy of the zeliard-wasm saves-api.py.  Never touches a live
# server: it binds 127.0.0.1 on a random port with a throw-away data dir.
#
# Usage: tools/sync_roundtrip.sh        (run from port/; needs `make` done)
# Env:   SAVES_API=path/to/saves-api.py   (default ~/git/zeliard-wasm/docker/saves-api.py)
#        GAME_SRC=path/to/game folder     (default ../zeliard; only the ZELRES*.SAR are used)
# Skips (exit 0, loudly) when the binary has no libcurl or the game files / server are missing.
set -u
cd "$(dirname "$0")/.."
BIN=${BIN:-./zeliard}
API=${SAVES_API:-$HOME/git/zeliard-wasm/docker/saves-api.py}
GAME_SRC=${GAME_SRC:-../zeliard}

skip() { echo "SKIP: $*"; exit 0; }
[ -x "$BIN" ] || skip "$BIN not built"
[ -f "$API" ] || skip "saves-api.py not found at $API (set SAVES_API)"
for f in ZELRES1.SAR ZELRES2.SAR ZELRES3.SAR; do
    [ -f "$GAME_SRC/$f" ] || [ -f "$GAME_SRC/${f,,}" ] || skip "game files missing in $GAME_SRC (--dir needs the ZELRES archives)"
done
"$BIN" --sync status --sync-base http://127.0.0.1:1/ 2>&1 | grep -q "built without network support" \
    && skip "this binary was built without libcurl"

T=$(mktemp -d /tmp/zsync-rt-XXXXXX)
SRV_PID=
cleanup() { [ -n "$SRV_PID" ] && kill "$SRV_PID" 2>/dev/null && wait "$SRV_PID" 2>/dev/null; rm -rf "$T"; }
trap cleanup EXIT

fails=0
ok()   { echo "  ok   $*"; }
bad()  { echo "  FAIL $*"; fails=$((fails + 1)); }
check() { local msg=$1; shift; if "$@"; then ok "$msg"; else bad "$msg"; fi; }

# ---- the server: random free port, temp data dir, loopback only
PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()')
mkdir -p "$T/data"
SAVE_DIR="$T/data" SAVES_API_PORT=$PORT python3 "$API" >"$T/server.log" 2>&1 &
SRV_PID=$!
BASE="http://127.0.0.1:$PORT/"
for i in $(seq 1 50); do curl -fs "${BASE}api/health" >/dev/null 2>&1 && break; sleep 0.1; done
curl -fs "${BASE}api/health" | grep -q '"ok": true' || { echo "server did not start"; cat "$T/server.log"; exit 1; }
echo "server on $BASE (data $T/data)"

KEY=RTRP
H=(-H "X-Client-Id: $KEY")
api() { python3 -I - "$@" <<'PY'
import sys, json, urllib.request
base, key, method, path = sys.argv[1:5]
req = urllib.request.Request(base + path, method=method, headers={"X-Client-Id": key})
with urllib.request.urlopen(req) as r:
    sys.stdout.buffer.write(r.read())
PY
}
history_len() { api "$BASE" "$KEY" GET api/saves/auto/history | python3 -c 'import sys,json; print(len(json.load(sys.stdin)))'; }

# ---- seed: what the web app would have stored (foreign entries + one save)
python3 -I - "$T/seed.zip" <<'PY'
import sys, zipfile, os
with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr(".jsdos/dosbox.conf", "[cpu]\ncycles=max\n")
    z.writestr("dosbox.conf", "[autoexec]\nzeliard\n")
    zi = zipfile.ZipInfo("save/1.sav", (2026, 1, 1, 0, 0, 0)); zi.compress_type = zipfile.ZIP_STORED
    z.writestr(zi, os.urandom(20000))
    z.writestr("ZELIARD.EXE", bytes(range(256)) * 20)
    zi = zipfile.ZipInfo("WEBHERO.USR", (2026, 1, 2, 0, 0, 0)); z.writestr(zi, os.urandom(256))
PY
NOW_MS=$(( $(date +%s) * 1000 ))
OLD_MS=$(( NOW_MS - 86400000 ))
curl -fs -X PUT "${H[@]}" -H "X-Save-Modified: $OLD_MS" --data-binary @"$T/seed.zip" "${BASE}api/saves/auto" >/dev/null
check "seeded the server" test "$(history_len)" = 0

# ---- two "devices": a game folder + private config/data dirs each
mkdev() {   # name
    local d=$T/$1; mkdir -p "$d/game" "$d/cfg" "$d/data"
    for f in ZELRES1.SAR ZELRES2.SAR ZELRES3.SAR; do
        if [ -f "$GAME_SRC/$f" ]; then ln -s "$(readlink -f "$GAME_SRC/$f")" "$d/game/$f"; else ln -s "$(readlink -f "$GAME_SRC/${f,,}")" "$d/game/$f"; fi
    done
}
mkdev A; mkdev B
dev() { local n=$1; shift; XDG_CONFIG_HOME="$T/$n/cfg" XDG_DATA_HOME="$T/$n/data" HOME="$T/$n" "$BIN" --dir "$T/$n/game" "$@" 2>>"$T/$n.log"; }

# folder A holds two saves written "now"
head -c 256 /dev/urandom > "$T/A/game/hero.usr"
head -c 256 /dev/urandom > "$T/A/game/zoe.usr"
touch -d "@$(( NOW_MS / 1000 ))" "$T/A/game/hero.usr" "$T/A/game/zoe.usr"

# 1. unlinked key with remote data: refused, server untouched
before=$(api "$BASE" "$KEY" GET api/saves/auto | sha256sum)
dev A --sync push --sync-key $KEY --sync-base "$BASE"; rc=$?
check "unlinked push is refused (exit 3)" test $rc = 3
check "...and the server copy is unchanged" test "$(api "$BASE" "$KEY" GET api/saves/auto | sha256sum)" = "$before"
dev B --sync pull --sync-key $KEY --sync-base "$BASE"; rc=$?
check "unlinked pull is refused (exit 3)" test $rc = 3
check "...and folder B is still empty" test -z "$(ls "$T/B/game" | grep -i '\.usr$')"

# 2. --force push from A
dev A --sync push --force --sync-base "$BASE"; rc=$?     # key remembered from the first run
check "push --force succeeds" test $rc = 0
check "history holds the replaced seed (1)" test "$(history_len)" = 1
dev A --sync status --sync-base "$BASE" > "$T/status.txt"
check "status shows a linked device" grep -q "linked, last synced" "$T/status.txt"

# 3. pull --force into the empty folder B, byte-identical
dev B --sync pull --force --sync-key $KEY --sync-base "$BASE"; rc=$?
check "pull --force succeeds" test $rc = 0
check "hero.usr is byte-identical" cmp -s "$T/A/game/hero.usr" "$T/B/game/hero.usr"
check "zoe.usr is byte-identical" cmp -s "$T/A/game/zoe.usr" "$T/B/game/zoe.usr"
check "the web-only save came across (as lower case)" test -f "$T/B/game/webhero.usr"

# 4. the foreign entries survived the push byte for byte (data and storage method)
python3 -I - "$T/seed.zip" "$T/data/$KEY/auto.bin" <<'PY'
import sys, zipfile
a, b = zipfile.ZipFile(sys.argv[1]), zipfile.ZipFile(sys.argv[2])
for n in [".jsdos/dosbox.conf", "dosbox.conf", "save/1.sav", "ZELIARD.EXE"]:
    assert a.read(n) == b.read(n), n
    assert a.getinfo(n).compress_type == b.getinfo(n).compress_type, n
assert a.read("WEBHERO.USR") == b.read("WEBHERO.USR")      # carried save (re-deflated, same bytes)
assert len(b.read("HERO.USR")) == 256 and len(b.read("ZOE.USR")) == 256
PY
check "foreign entries (save/1.sav, .jsdos/*, game files) are byte-identical in the pushed bundle" test $? = 0

# 5. a second push (linked, newer) grows the history
head -c 256 /dev/urandom > "$T/A/game/hero.usr"
touch -d "@$(( NOW_MS / 1000 + 600 ))" "$T/A/game/hero.usr"
dev A --sync push --sync-base "$BASE"; rc=$?
check "second push succeeds" test $rc = 0
check "history grew to 2 (server-side undo)" test "$(history_len)" = 2

# 6. B pulls the update (linked, remote newer): backup of its old copy, identical afterwards
dev B --sync pull --sync-base "$BASE"; rc=$?
check "linked pull succeeds" test $rc = 0
check "hero.usr updated and identical" cmp -s "$T/A/game/hero.usr" "$T/B/game/hero.usr"
check "the overwrite made a local backup" test -n "$(ls "$T/B/cfg/zeliard/backups" "$T/B/data/awkto/zeliard/backups" 2>/dev/null | grep '\.zip$')"
dev B --sync pull --sync-base "$BASE"; rc=$?
check "an immediate second pull is a no-op" test $rc = 0

# 7. a key with nothing on the server: push is refused, not created
dev A --sync push --sync-key NONE --sync-base "$BASE"; rc=$?
check "push to an empty key is refused (exit 6)" test $rc = 6
check "...and nothing was created" test "$(api "$BASE" NONE GET api/saves | tr -d ' \n')" = "[]"

# ---- lifecycle (step 2): the interactive game with sync = on, under SDL's dummy
# video driver (no window, no sound).  A device is a game folder plus its own
# zeliard.ini in the pref path ($XDG_DATA_HOME/awkto/zeliard).
play() { local n=$1; shift   # --frames 3: three frames of the town, then a normal exit
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy XDG_CONFIG_HOME="$T/$n/cfg" XDG_DATA_HOME="$T/$n/data" HOME="$T/$n" \
        timeout "${PLAY_TIMEOUT:-25}" "$BIN" --dir "$T/$n/game" --town 1 --frames 3 "$@" >"$T/$n.play" 2>&1; }
ini() { local n=$1 base=$2 key=$3 d="$T/$1/data/awkto/zeliard"; mkdir -p "$d"
    printf 'sync=on\nsync_base=%s\nsync_key=%s\n' "$base" "$key" > "$d/zeliard.ini"; }
if SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HOME="$T/A" XDG_DATA_HOME="$T/A/data" "$BIN" --dir "$T/A/game" --town 1 --frames 1 2>&1 | grep -q "built without SDL2"; then
    echo "  skip lifecycle checks: this binary has no SDL"
else
    echo "lifecycle:"
    KEY2=LIFE
    # server holds a bundle for LIFE, as the web app would have left it
    curl -fs -X PUT -H "X-Client-Id: $KEY2" -H "X-Save-Modified: $OLD_MS" --data-binary @"$T/seed.zip" "${BASE}api/saves/auto" >/dev/null
    # C: empty folder, unlinked key -> the start-up pull takes the server's save, no question
    mkdev C; ini C "$BASE" $KEY2
    play C; rc=$?
    check "empty folder + sync on: the game starts (exit 0)" test $rc = 0
    check "...and the server's save was pulled before the town loaded" test -f "$T/C/game/webhero.usr"
    check "...and the key is linked" grep -q '"LIFE"' "$T/C/data/awkto/zeliard/sync-links.json"
    # D: has its own save, unlinked key + a server save -> the dialog opens and waits (no keys on a dummy
    # display), nothing is touched, until Esc/close; here the watchdog ends it
    mkdev D; ini D "$BASE" $KEY2
    head -c 256 /dev/urandom > "$T/D/game/mine.usr"; sum=$(sha256sum "$T/D/game/mine.usr")
    srv_before=$(api "$BASE" $KEY2 GET api/saves/auto | sha256sum)
    PLAY_TIMEOUT=4 play D; rc=$?
    check "both sides have a save: the question is asked and play waits (watchdog, exit 124)" test $rc = 124
    check "...local save untouched, server untouched, not linked" \
        test "$(sha256sum "$T/D/game/mine.usr")" = "$sum" -a "$(api "$BASE" $KEY2 GET api/saves/auto | sha256sum)" = "$srv_before" -a ! -f "$T/D/game/webhero.usr" -a ! -f "$T/D/data/awkto/zeliard/sync-links.json"
    # E: offline server -> one [sync] line, play goes on, quickly
    mkdev E; ini E "http://127.0.0.1:1/" $KEY2
    head -c 256 /dev/urandom > "$T/E/game/mine.usr"
    s0=$SECONDS; play E; rc=$?
    check "server unreachable: the game still starts and exits 0" test $rc = 0
    check "...with an offline line in the log, within a few seconds" test $(grep -c '^\[sync\] offline' "$T/E.play") = 1 -a $((SECONDS - s0)) -lt 8
    # F: tool runs never sync and never ask, even with sync on and a key that needs a decision
    mkdev F; ini F "$BASE" $KEY2
    head -c 256 /dev/urandom > "$T/F/game/mine.usr"
    for mode in "--headless" "--script R5" "--screenshot 2 $T/F/shot.png"; do
        play F $mode; rc=$?
        check "$mode: exits 0, no [sync] line, nothing pulled" test $rc = 0 -a "$(grep -c '^\[sync\]\|^zeliard: sync' "$T/F.play")" = 0 -a ! -f "$T/F/game/webhero.usr"
    done
fi

echo
if [ $fails -eq 0 ]; then echo "sync_roundtrip: all checks passed"; else echo "sync_roundtrip: $fails FAILED (logs in $T)"; trap - EXIT; kill "$SRV_PID" 2>/dev/null; exit 1; fi
