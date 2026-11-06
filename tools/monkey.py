#!/usr/bin/env python3
"""monkey.py - random-input ("monkey") tester for the headless Zeliard port.

Generates seeded random-but-plausible scripts and start states, runs
`port/zeliard --headless` on each, flags crashes / hangs / sanitizer reports /
malformed or insane final status lines, then delta-debugs the script of every
failure down to a minimal repro and writes a report.

  tools/monkey.py --cases 50 --frames 600 --jobs 2 --seed 1 --san --out /tmp/monkey-out

With --san the sources in port/ are copied to <out>/build-san and built there
with SAN=1, so a parallel edit/build in port/ is never disturbed.  Stdlib only.
"""
import argparse, concurrent.futures as cf, json, os, random, re, shutil, signal
import subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = os.path.join(ROOT, "port")

STATUS_RE = re.compile(r"stopped after (\d+) frames: hero map \((-?\d+),(-?\d+)\), LIFE (\d+)/(\d+), "
                       r"EXP (\d+), GOLD (\d+), (\d+) hazard frames, (\d+) deaths")
BANNER_RE = re.compile(r"\b(MP\w+)(?::| \() ?cavern \d+, (\d+) col")   # start banner and "[door] entered MPxx (cavern n, W cols"
LOC_RE = re.compile(r"^\[(town|cavern)\]", re.M)
SAN_RE = re.compile(r"ERROR: (Address|Leak|Undefined)Sanitizer|runtime error:|SUMMARY: \w+Sanitizer")

# Known-good door/entry cells from port/playthrough.c (map -> [(col,row)]).  Maps
# not listed (or pos=None) use the port's own default: the map header's start cell.
KNOWN_POS = {
    0: [(61, 7), (141, 32)],           # MP10: MURALLA door, door to MP1D
    1: [(47, 14)],                     # MP1D (Cangrejo's room)
    2: [(190, 47), (171, 55)],         # MP20
    3: [(14, 36)],                     # MP21
    5: [(21, 6), (46, 15)],            # MP30
    6: [(18, 50), (87, 7), (152, 44)], # MP31
}
NMAPS = 0x1F
NTOWNS = 10


# ---------------------------------------------------------------- generation
def gen_script(rng, budget):
    """Return a list of tokens whose hold frames sum to ~budget."""
    toks, used = [], 0
    facing = rng.choice("LR")
    while used < budget:
        r = rng.random()
        hold = min(rng.choice([1, 1, 2, 3, 5, 8, 12, 20, 30]), 30)
        if r < 0.42:                                   # walk
            if rng.random() < 0.3: facing = rng.choice("LR")
            tok = facing + str(hold)
        elif r < 0.57:                                 # jump (up) / jump-walk
            tok = "U" + (facing if rng.random() < 0.6 else "") + str(rng.randint(1, 12))
        elif r < 0.75:                                 # attack
            tok = ("X" + str(rng.randint(1, 4))) if rng.random() < 0.6 else \
                  (rng.choice(["D", facing]) + "X" + str(rng.randint(1, 6)))
        elif r < 0.82:                                 # crouch / climb
            tok = rng.choice("UD") + str(rng.randint(1, 20))
        elif r < 0.88:
            tok = "M" + str(rng.randint(1, 4))
        elif r < 0.97:
            tok = "." + str(rng.randint(1, 15))
        elif r < 0.985:
            tok = "E" + str(rng.randint(1, 3))
        else:
            tok = "K" + str(rng.randint(1, 3))
        toks.append(tok)
        used += int((re.search(r"\d+", tok) or [1])[0]) if re.search(r"\d+", tok) else 1
    return toks


VIDEOS = ["mcga", "cga", "cga2", "ega", "hgc", "tandy"]
AUDIOS = ["adlib", "speaker", "tandy"]


def load_scenarios(gamedir):
    """-> [(name, load, cave, prefix_tokens, magic)] from tests/scenarios/*.zs whose start save exists."""
    out = []
    sdir = os.path.join(ROOT, "tests", "scenarios")
    for fn in sorted(os.listdir(sdir)):
        if not fn.endswith(".zs"): continue
        txt = open(os.path.join(sdir, fn)).read().splitlines()
        hdr = lambda k: next((l.split(":", 1)[1].strip() for l in txt if l.startswith("# %s:" % k)), "")
        save = hdr("start")
        if not save or not os.path.exists(os.path.join(gamedir, save)): continue
        script = next((l for l in txt if l.strip() and not l.startswith("#")), "")
        mk = hdr("make")
        mg = re.search(r"--magic (\S+)", mk)
        out.append((fn[:-3], save.rsplit(".", 1)[0], hdr("cave"), script.split(), mg.group(1) if mg else None))
    return out


def gen_case_v2(seed, idx, frames, gamedir):
    """soak profile: town / cavern / save-load / scenario-prefix (boss, ending) starts, random
    --video and --audio, random-tail script."""
    rng = random.Random(seed * 1000003 + idx)
    c = {"id": idx, "seed": seed}
    args, prefix = [], []
    r = rng.random()
    if r < 0.15:
        c["kind"] = "town"
        args += ["--town", str(rng.randrange(NTOWNS)), "--town-col", str(rng.randrange(4, 280))]
    elif r < 0.45:
        c["kind"] = "cavern"
        m = rng.randrange(NMAPS)
        args += ["--map", str(m)]
        if m in KNOWN_POS and rng.random() < 0.5:
            args += ["--pos", *map(str, rng.choice(KNOWN_POS[m]))]
    elif r < 0.55:
        c["kind"] = "ending"
        args += ["--ending"]
    else:
        sc = rng.choice(load_scenarios(gamedir))
        c["kind"] = "scenario:" + sc[0]
        args += ["--load", sc[1]]
        if sc[2]: args += ["--cave", sc[2]]
        if sc[4] and rng.random() < 0.5: args += ["--magic", sc[4]]
        if rng.random() < 0.5: args += ["--life", str(rng.choice([800, 2000, 4000, 9000]))]   # survive long enough to soak
        if rng.random() < 0.8 and sc[3]:        # replay a prefix of the scenario (reaches the boss / door), then monkey
            n = rng.randint(max(1, len(sc[3]) // 3), len(sc[3]))
            prefix = sc[3][:n]
            if rng.random() < 0.4: prefix = sc[3]
    if c["kind"] in ("town", "cavern") and rng.random() < 0.7:
        args += ["--level", str(rng.randint(5, 60)), "--life", str(rng.randint(300, 2000)),
                 "--sword", str(rng.randint(1, 6)), "--shield", str(rng.randint(0, 4)),
                 "--gold", str(rng.choice([0, 500, 5000]))]
        if rng.random() < 0.5:
            args += ["--spells", ",".join(map(str, rng.sample(range(1, 8), rng.randint(1, 4))))]
        if rng.random() < 0.4:
            args += ["--magic", "%d=%d" % (rng.randint(1, 7), rng.choice([1, 5, 9]))]
    args += ["--video", rng.choice(VIDEOS), "--audio", rng.choice(AUDIOS)]
    c["start_args"] = args
    c["script"] = prefix + gen_script(rng, frames)
    c["prefix_len"] = len(prefix)
    return c


def gen_case(seed, idx, frames, only=None):
    rng = random.Random(seed * 1000003 + idx)
    c = {"id": idx, "seed": seed}
    kind = only or rng.choices(["cavern", "town"], [0.8, 0.2])[0]
    args = []
    if kind == "town":
        args += ["--town", str(rng.randrange(NTOWNS))]
    else:
        m = rng.randrange(NMAPS)
        args += ["--map", str(m)]
        if m in KNOWN_POS and rng.random() < 0.5:
            args += ["--pos", *map(str, rng.choice(KNOWN_POS[m]))]
    if rng.random() < 0.7:                              # boosted hero
        args += ["--level", str(rng.randint(5, 40)), "--life", str(rng.randint(300, 900)),
                 "--sword", str(rng.randint(1, 4)), "--shield", str(rng.randint(1, 4)),
                 "--gold", str(rng.choice([0, 500, 5000]))]
        if rng.random() < 0.5:
            args += ["--spells", ",".join(map(str, rng.sample(range(1, 8), rng.randint(1, 4))))]
    c["kind"], c["start_args"] = kind, args
    c["script"] = gen_script(rng, frames)
    return c


# ----------------------------------------------------------------- running
def cmdline(binary, gamedir, case, script=None, frames=None):
    toks = case["script"] if script is None else script
    scr = " ".join(toks) if toks else ".1"
    return [binary, "--dir", gamedir, "--headless", *case["start_args"],
            "--frames", str(frames or case["frames_cap"]), "--script", scr]


def check_trace(path):
    """Per-frame invariants over a --trace frames.jsonl (cavern frames, site 717F).  -> message or None.
    Each one is something the original also guarantees; a hit is a port bug unless proven otherwise."""
    prev = None
    try:
        fh = open(os.path.join(path, "frames.jsonl"))
    except OSError:
        return None
    with fh:
        for line in fh:
            try: d = json.loads(line)
            except ValueError: return "frames.jsonl: unparsable line"
            f, n = d.get("fields"), d.get("f")
            if not f: continue
            if "hp" not in f: continue
            hp, mx = f["hp"], f["max_hp"]
            gold = (f["gold_hi"] << 16) | f["gold_lo"]
            cur = (f.get("cur_map"), f.get("town_map"), "obj.n" in f)
            bad = []
            if hp < 0 or hp > 9999 + 1: bad.append("hp %d" % hp)
            if mx > 9999: bad.append("max_hp %d" % mx)
            if hp > mx + 1: bad.append("hp %d > max_hp %d" % (hp, mx))
            if f["level"] > 99: bad.append("level %d" % f["level"])
            if f.get("obj.n", 0) > 128: bad.append("obj.n %d" % f["obj.n"])
            if f.get("shot.n", 0) > 32: bad.append("shot.n %d" % f["shot.n"])
            mc, mm = bytes.fromhex(f.get("magic_count", "")), bytes.fromhex(f.get("magic_max", ""))
            for i in range(min(len(mc), len(mm))):
                if mc[i] > mm[i] and mm[i]: bad.append("magic_count[%d]=%d > max %d" % (i, mc[i], mm[i]))
            if "obj.n" in f and (f["hero_scr_row"] > 24 or f["hero_scr_col"] > 40): bad.append("hero screen pos (%d,%d)" % (f["hero_scr_col"], f["hero_scr_row"]))
            if gold > 0x7FFFFFF or f["exp"] > 0x7FFFFFF: bad.append("gold/exp %d/%d" % (gold, f["exp"]))
            if d.get("site") != "717F":          # shops, the sage, inns and the bank legitimately move gold/EXP/LIFE
                prev = None
                if bad: return "frame %s: %s" % (n, "; ".join(bad))
                continue
            if prev and prev[0] == cur and not f.get("hero_dead"):
                if hp - prev[1] > 10 and not prev[4]: bad.append("hp jumped %d -> %d" % (prev[1], hp))
                if gold < prev[2] - 0 and f["exp"] >= prev[3]: bad.append("gold fell %d -> %d in a cavern" % (prev[2], gold))
                if f["exp"] < prev[3]: bad.append("exp fell %d -> %d" % (prev[3], f["exp"]))
            if bad: return "frame %s: %s" % (n, "; ".join(bad))
            prev = (cur, hp, gold, f["exp"], f.get("hero_dead"))
    return None


def run_one(cmd, timeout, check=False):
    """-> dict(rc, sig, timeout, err, out)"""
    if check:
        import tempfile
        td = tempfile.mkdtemp(prefix="monkey-tr-", dir=os.environ.get("MONKEY_TMP"))
        try:
            res = run_one(cmd + ["--trace", td], timeout)
            msg = check_trace(td)
            if msg: res["err"] += "\nINVARIANT: " + msg + "\n"
            return res
        finally:
            shutil.rmtree(td, ignore_errors=True)
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:abort_on_error=0",
               UBSAN_OPTIONS="print_stacktrace=1", SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, stdin=subprocess.DEVNULL,
                         env=env, start_new_session=True)
    try:
        out, err = p.communicate(timeout=timeout)
        to = False
    except subprocess.TimeoutExpired:
        os.killpg(p.pid, signal.SIGKILL)
        out, err = p.communicate()
        to = True
    rc = p.returncode
    return {"rc": rc, "sig": -rc if rc is not None and rc < 0 and not to else None, "timeout": to,
            "err": err.decode("utf8", "replace"), "out": out.decode("utf8", "replace")}


def classify(res, town=False):
    town = town is True or town == "town" or str(town).startswith(("town", "ending", "scenario"))
    """-> (class, detail) or (None, '') when clean."""
    err = res["err"]
    if res["timeout"]:
        return "hang", "wall-clock timeout"
    if SAN_RE.search(err):
        m = re.search(r"(ERROR: \w+Sanitizer: [^\n]*|[^\n]*runtime error:[^\n]*)", err)
        return "sanitizer", (m.group(1) if m else "report")[:200]
    if res["sig"]:
        try: n = signal.Signals(res["sig"]).name
        except ValueError: n = str(res["sig"])
        return "crash", n
    mi = re.search(r"^INVARIANT: (.*)$", err, re.M)
    if mi and res["rc"] == 0:
        return "invariant", re.sub(r"\d+", "N", mi.group(1))[:120]
    if res["rc"] != 0:
        return "nonzero-exit", "rc=%s: %s" % (res["rc"], (err.strip().splitlines() or [""])[-1][:160])
    m = STATUS_RE.search(err)
    if not m and "[intro] " in err:         # --ending / cutscene runs print no status line
        return None, ""
    if not m:
        return "no-status-line", (err.strip().splitlines() or ["<empty stderr>"])[-1][:160]
    frames, col, row, hp, mx, exp, gold, haz, deaths = map(int, m.groups())
    bad = []
    # the original's regeneration (fight.bin 71B1..71B7: cmp hp,max / jnc / add hp,2, no clamp) takes an odd
    # max_hp-1 to max_hp+1, so LIFE == max+1 is original behaviour, not a port bug
    if hp > mx + 1: bad.append("LIFE %d > max %d" % (hp, mx))
    if mx > 9999 or mx == 0: bad.append("absurd max LIFE %d" % mx)
    if town: col = row = 0                  # hero map cell is meaningless in town (and frame_no counts cavern frames only)
    if col < 0 or row < 0: bad.append("negative hero pos (%d,%d)" % (col, row))
    locs = LOC_RE.findall(err)                  # death/door hand-offs can leave us in a town at exit
    if locs and locs[-1] == "town": town = True
    bs = BANNER_RE.findall(err)
    b = bs[-1] if bs else None
    if b and not town and col >= int(b[1]) + 2: bad.append("hero col %d outside %s width %s" % (col, b[0], b[1]))
    if row > 200: bad.append("absurd hero row %d" % row)
    if exp > 0x7FFFFFF or gold > 0x7FFFFFF: bad.append("absurd EXP/GOLD %d/%d" % (exp, gold))
    if haz > frames: bad.append("hazard frames %d > frames %d" % (haz, frames))
    if frames == 0 and not town: bad.append("zero frames run")
    if bad:
        return "bad-state", "; ".join(bad)
    return None, ""


def shrink(case, binary, gamedir, klass, timeout, max_runs=80, budget_s=300, check=False):
    """ddmin over script tokens, keeping failure class (bounded by runs and wall time)."""
    toks = list(case["script"])
    runs = [0]
    t_end = time.time() + budget_s
    timeout = min(timeout, 20) if klass == "hang" else timeout   # a hang stays a hang; don't wait minutes per probe
    def fails(t):
        if runs[0] >= max_runs or time.time() > t_end: return False
        runs[0] += 1
        c, _ = classify(run_one(cmdline(binary, gamedir, case, t, case["frames_cap"]), timeout, check), case["kind"] != "cavern")
        return c == klass
    n = 2
    while len(toks) >= 2 and runs[0] < max_runs:
        size = max(1, len(toks) // n)
        chunks = [toks[i:i + size] for i in range(0, len(toks), size)]
        reduced = False
        for i in range(len(chunks)):                    # try dropping one chunk
            cand = [t for j, ch in enumerate(chunks) if j != i for t in ch]
            if cand and fails(cand):
                toks, n, reduced = cand, max(n - 1, 2), True
                break
        if not reduced:
            if size == 1: break
            n = min(len(toks), n * 2)
    if len(toks) == 1 and runs[0] < max_runs and fails([]) :
        toks = []
    return toks, runs[0]


def do_case(case, binary, gamedir, timeout, do_shrink, check=False):
    cmd = cmdline(binary, gamedir, case)
    t0 = time.time()
    res = run_one(cmd, timeout, check)
    klass, detail = classify(res, case["kind"] != "cavern")
    rec = {"id": case["id"], "seed": case["seed"], "kind": case["kind"], "start_args": case["start_args"],
           "secs": round(time.time() - t0, 2), "class": klass, "detail": detail}
    m = STATUS_RE.search(res["err"])
    if m: rec["status"] = m.group(0)
    if klass:
        rec["cmd"] = " ".join(map(shell_quote, cmd))
        rec["stderr_tail"] = "\n".join(res["err"].strip().splitlines()[-25:])
        if do_shrink:
            mt, nruns = shrink(case, binary, gamedir, klass, timeout, check=check)
            rec["min_script"] = " ".join(mt)
            rec["min_cmd"] = " ".join(map(shell_quote, cmdline(binary, gamedir, case, mt)))
            rec["shrink_runs"] = nruns
            rec["orig_tokens"], rec["min_tokens"] = len(case["script"]), len(mt)
    return rec


def shell_quote(s):
    return s if re.fullmatch(r"[\w./=,:@%+-]+", s) else "'" + s.replace("'", "'\\''") + "'"


# ------------------------------------------------------------------- build
def build(out, san, jobs=4):
    if not san:
        dst = PORT
    else:
        dst = os.path.join(out, "build-san")
        os.makedirs(dst, exist_ok=True)
        for f in os.listdir(PORT):
            p = os.path.join(PORT, f)
            if f.endswith((".c", ".h", ".mk")) or f == "Makefile":
                shutil.copy2(p, dst)
        for d in ("compat",):
            if os.path.isdir(os.path.join(PORT, d)):
                shutil.copytree(os.path.join(PORT, d), os.path.join(dst, d), dirs_exist_ok=True)
    cmd = ["nice", "-n", "19", "ionice", "-c3", "make", "-C", dst, "-j%d" % min(jobs, 8), "zeliard"]
    if san: cmd.append("SAN=1")
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if r.returncode:
        sys.exit("build failed:\n" + r.stdout.decode()[-3000:])
    return os.path.join(dst, "zeliard")


# ------------------------------------------------------------------- main
def report(out, args, recs, elapsed):
    fails = [r for r in recs if r["class"]]
    byc = {}
    for r in fails: byc[r["class"]] = byc.get(r["class"], 0) + 1
    with open(os.path.join(out, "report.json"), "w") as f:
        json.dump({"args": vars(args), "cases": len(recs), "failures": len(fails), "by_class": byc,
                   "elapsed_s": round(elapsed, 1), "results": recs}, f, indent=1)
    L = ["# monkey report", "",
         "- cases: %d, frames/case: %d, seed: %d, san: %s, elapsed: %.0fs" % (len(recs), args.frames, args.seed, args.san, elapsed),
         "- failures: %d  %s" % (len(fails), byc or ""), ""]
    # group by (class, detail) so a repeated bug shows once
    groups = {}
    for r in fails: groups.setdefault((r["class"], r["detail"]), []).append(r)
    for (k, d), rs in groups.items():
        best = min(rs, key=lambda r: r.get("min_tokens", 1 << 30))
        L += ["## %s: %s  (x%d, cases %s)" % (k, d, len(rs), ",".join(str(r["id"]) for r in rs)), "",
              "Minimal repro (%s tokens, from %s):" % (best.get("min_tokens", "?"), best.get("orig_tokens", "?")), "",
              "```", best.get("min_cmd", best["cmd"]), "```", "",
              "Original: `%s`" % best["cmd"][:1500], "", "```", best["stderr_tail"][-1500:], "```", ""]
    L += ["## all cases", "", "| id | kind | start | secs | result |", "|--|--|--|--|--|"]
    for r in recs:
        L.append("| %d | %s | `%s` | %s | %s |" % (r["id"], r["kind"], " ".join(r["start_args"][:4]), r["secs"],
                                                  (r["class"] + ": " + r["detail"][:60]) if r["class"] else "ok"))
    open(os.path.join(out, "report.md"), "w").write("\n".join(L) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cases", type=int, default=20)
    ap.add_argument("--frames", type=int, default=600, help="script length budget in frames")
    ap.add_argument("--jobs", type=int, default=2, help="parallel runs (capped at 2; 6 with --soak)")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--san", action="store_true", help="build/run with ASan+UBSan (separate build dir)")
    ap.add_argument("--bin", help="use this zeliard binary instead of building")
    ap.add_argument("--gamedir", default=os.path.join(ROOT, "zeliard"))
    ap.add_argument("--timeout", type=float, default=0, help="per-run wall seconds (default 60, 180 with --san)")
    ap.add_argument("--soak", action="store_true", help="v2 profile: saves, scenario prefixes (bosses), ending, --video/--audio")
    ap.add_argument("--check", action="store_true", help="also run with --trace and check per-frame invariants (slower)")
    ap.add_argument("--first", type=int, default=0, help="first case index (to split a seed across runs)")
    ap.add_argument("--only", choices=["cavern", "town"])
    ap.add_argument("--no-shrink", action="store_true")
    ap.add_argument("--out", default="/tmp/monkey-out")
    args = ap.parse_args()
    args.jobs = max(1, min(args.jobs, 6 if args.soak else 2))
    timeout = args.timeout or (180 if args.san else 60)
    os.makedirs(args.out, exist_ok=True)
    binary = args.bin or build(args.out, args.san)
    t0 = time.time()
    cases = []
    for i in range(args.first, args.first + args.cases):
        c = gen_case_v2(args.seed, i, args.frames, args.gamedir) if args.soak else gen_case(args.seed, i, args.frames, args.only)
        c["frames"] = args.frames
        c["frames_cap"] = args.frames * 2 + 100       # the script ends the run; this is only a backstop
        cases.append(c)
    recs = []
    with cf.ThreadPoolExecutor(args.jobs) as ex:
        futs = [ex.submit(do_case, c, binary, args.gamedir, timeout, not args.no_shrink, args.check) for c in cases]
        for f in futs:
            r = f.result()
            recs.append(r)
            print("case %3d %-6s %-28s %s" % (r["id"], r["kind"], " ".join(r["start_args"][:4]),
                  ("FAIL " + r["class"] + ": " + r["detail"][:70]) if r["class"] else "ok"), flush=True)
    report(args.out, args, recs, time.time() - t0)
    nf = sum(1 for r in recs if r["class"])
    print("%d cases, %d failures; report: %s/report.md" % (len(recs), nf, args.out))
    sys.exit(1 if nf else 0)


if __name__ == "__main__":
    main()
