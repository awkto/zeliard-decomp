#!/bin/bash
# sync_roundtrip.sh -- #55 step 1 end to end: the real zeliard binary (libcurl)
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

echo
if [ $fails -eq 0 ]; then echo "sync_roundtrip: all checks passed"; else echo "sync_roundtrip: $fails FAILED (logs in $T)"; trap - EXIT; kill "$SRV_PID" 2>/dev/null; exit 1; fi
