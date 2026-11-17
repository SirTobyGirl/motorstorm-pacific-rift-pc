#!/usr/bin/env python3
"""Setup wizard: a small local web page that walks through preparing, lifting,
building and starting the port. It drives the repository's own scripts
(doctor.sh, prepare_disc.sh, setup.sh, lift.sh, build.sh, work/run.sh), so
they stay the source of truth; this only runs them and shows the result.

    scripts/setup_gui.py [--root DIR] [--port N] [--no-browser]

Also built as a single executable (scripts/build_gui_exe.sh): it carries the
repository's scripts and sources and unpacks them into a workspace folder
(default ~/MotorStormPR) on first run, so users need no checkout.

Standard library only (no tkinter or GTK needed). It listens on 127.0.0.1
with a random access token and opens your browser. Linux and macOS.
It never installs system packages: it tells you what is missing.
Nothing from your game is sent anywhere.
"""
import argparse, datetime, io, json, os, re, secrets, shutil, signal, subprocess, sys, tarfile, threading, time, urllib.error, urllib.parse, urllib.request, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

FROZEN = bool(getattr(sys, "frozen", False))      # running as the PyInstaller executable
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # replaced by set_root()
TOKEN = secrets.token_urlsafe(24)
HOME = os.path.expanduser("~")
CONFIG_PATH = ""
# Variables a shell may export that make CMake pick up a foreign toolchain.
LEAKY = ["CC", "CXX", "AR", "RANLIB", "LDFLAGS", "CMAKE_SYSROOT", "CMAKE_TOOLCHAIN_FILE", "LD_LIBRARY_PATH"]

cfg = {"prefix": "", "clean_env": True, "jobs": "", "flash": "", "update_repo": "", "update_ref": ""}


def set_root(root):
    """Point everything at the workspace and load its saved settings."""
    global ROOT, CONFIG_PATH
    ROOT = os.path.abspath(root)
    CONFIG_PATH = os.path.join(ROOT, "user", "setup_gui.json")
    try:
        with open(CONFIG_PATH) as f:
            cfg.update(json.load(f))
    except (OSError, ValueError):
        pass


def bundle_dir():
    """The repository files carried inside the executable ('' when not frozen)."""
    base = getattr(sys, "_MEIPASS", "")
    d = os.path.join(base, "payload") if base else ""
    return d if d and os.path.isdir(d) else ""


def ensure_workspace(root):
    """Unpack the bundled scripts and sources into root. Only the bundled
    (tracked) files are written, never the game, saves, builds or settings.
    A git checkout is left alone, and so is a workspace already at this version."""
    src = bundle_dir()
    if not src:
        return
    try:
        with open(os.path.join(src, ".bundle_version")) as f:
            ver = f.read().strip()
    except OSError:
        ver = "unknown"
    # .bundle_origin: the executable's version at the last unpack. .bundle_version:
    # the version of the files now in the workspace (an update moves only this one).
    origin = os.path.join(root, ".bundle_origin")
    have = read_text(origin) or read_text(os.path.join(root, ".bundle_version"))
    if have == ver or os.path.isdir(os.path.join(root, ".git")):
        return
    print("Unpacking the port's scripts and sources into %s ..." % root, flush=True)
    backup = os.path.join(root, ".update-backup", time.strftime("%Y%m%d-%H%M%S"))
    for dp, _, fns in os.walk(src):
        rel = os.path.relpath(dp, src)
        for fn in fns:
            if fn == ".bundle_version":
                continue
            with open(os.path.join(dp, fn), "rb") as f:
                data = f.read()
            place_file(root, os.path.normpath(os.path.join(rel, fn)), data,
                       os.stat(os.path.join(dp, fn)).st_mode & 0o777, backup)
    write_text(os.path.join(root, ".bundle_version"), ver)
    write_text(origin, ver)


def read_text(path):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return ""


def write_text(path, text):
    with open(path, "w") as f:
        f.write(text + "\n")


def place_file(root, rel, data, mode, backup_dir):
    """Write root/rel. A different file already there is copied to backup_dir first."""
    dest = os.path.join(root, rel)
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    if os.path.isfile(dest):
        with open(dest, "rb") as f:
            if f.read() == data:
                return False
        bk = os.path.join(backup_dir, rel)
        os.makedirs(os.path.dirname(bk), exist_ok=True)
        shutil.copy2(dest, bk)
    with open(dest, "wb") as f:
        f.write(data)
    os.chmod(dest, mode or 0o644)
    return True


def pick_root(arg):
    if arg:
        return arg
    if os.environ.get("MSPR_HOME"):
        return os.environ["MSPR_HOME"]
    if not FROZEN:
        return ROOT
    if os.path.isfile(os.path.join(os.getcwd(), "scripts", "doctor.sh")):
        return os.getcwd()
    return os.path.join(HOME, "MotorStormPR")


def save_cfg():
    try:
        os.makedirs(os.path.dirname(CONFIG_PATH), exist_ok=True)
        with open(CONFIG_PATH, "w") as f:
            json.dump(cfg, f)
    except OSError:
        pass


def make_env():
    env = dict(os.environ)
    if FROZEN:
        # PyInstaller points the library path at its own bundle; the build
        # tools we start must see the user's, not ours.
        for var in ("LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH"):
            orig = env.pop(var + "_ORIG", None)
            if orig is not None:
                env[var] = orig
            else:
                env.pop(var, None)
    if cfg["clean_env"]:
        for v in LEAKY:
            env.pop(v, None)
        # Prefer the system's tools over any SDK that sits first in PATH.
        sysdirs = ["/usr/local/bin", "/usr/bin", "/bin", "/opt/homebrew/bin"]
        rest = [p for p in env.get("PATH", "").split(":") if p and p not in sysdirs]
        env["PATH"] = ":".join([d for d in sysdirs if os.path.isdir(d)] + rest)
    pre = cfg["prefix"].strip()
    if pre:
        pre = os.path.expanduser(pre)
        env["CMAKE_PREFIX_PATH"] = pre + (":" + env["CMAKE_PREFIX_PATH"] if env.get("CMAKE_PREFIX_PATH") else "")
        pk = ":".join(os.path.join(pre, d, "pkgconfig") for d in ("lib64", "lib", "share"))
        env["PKG_CONFIG_PATH"] = pk + (":" + env["PKG_CONFIG_PATH"] if env.get("PKG_CONFIG_PATH") else "")
        ll = ":".join(os.path.join(pre, d) for d in ("lib64", "lib"))
        env["LD_LIBRARY_PATH"] = ll + (":" + env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else "")
    return env


def auto_jobs():
    cpus = os.cpu_count() or 4
    try:
        with open("/proc/meminfo") as f:
            kb = int(re.search(r"MemTotal:\s+(\d+)", f.read()).group(1))
        gb = kb // 1048576
    except Exception:
        gb = 16
    return max(1, min(cpus, gb // 5)), max(8, int(gb * 0.85))


# ---------------------------------------------------------------- jobs ----
class Job:
    def __init__(self):
        self.lock = threading.Lock()
        self.proc = None
        self.name = ""
        self.lines = []
        self.base = 0          # absolute index of lines[0]
        self.done = True
        self.code = None
        self.progress = None   # (n, m) from ninja's [n/m]

    def add(self, line):
        with self.lock:
            self.lines.append(line)
            if len(self.lines) > 6000:
                drop = len(self.lines) - 4000
                del self.lines[:drop]
                self.base += drop
            m = re.match(r"\[(\d+)/(\d+)\]", line)
            if m:
                self.progress = (int(m.group(1)), int(m.group(2)))

    def start(self, name, argv, env, watcher=None):
        with self.lock:
            if not self.done:
                return False
            self.name, self.done, self.code, self.progress = name, False, None, None
            self.lines, self.base = [], 0
        self.add("$ " + " ".join(argv))
        try:
            self.proc = subprocess.Popen(argv, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                         text=True, errors="replace", bufsize=1, start_new_session=True)
        except OSError as e:
            self.add("could not start: %s" % e)
            with self.lock:
                self.done, self.code = True, 127
            return True

        def pump():
            for line in self.proc.stdout:
                self.add(line.rstrip("\n"))
            code = self.proc.wait()
            with self.lock:
                self.done, self.code = True, code
            self.add("-- finished with exit code %d" % code)
        threading.Thread(target=pump, daemon=True).start()
        if watcher:
            threading.Thread(target=watcher, daemon=True).start()
        return True

    def stop(self):
        p = self.proc
        if p and p.poll() is None:
            try:
                os.killpg(p.pid, signal.SIGTERM)
            except OSError:
                pass

    def snapshot(self, since):
        with self.lock:
            start = max(0, since - self.base)
            return {"lines": self.lines[start:], "next": self.base + len(self.lines), "running": not self.done,
                    "code": self.code, "name": self.name, "progress": self.progress}


JOB = Job()


def find_flash():
    for d in (cfg["flash"], os.environ.get("PS3_DEV_FLASH", ""), os.path.join(ROOT, "dev_flash"),
              os.path.join(HOME, ".config/rpcs3/dev_flash"),
              os.path.join(HOME, ".var/app/net.rpcs3.RPCS3/config/rpcs3/dev_flash"),
              os.path.join(HOME, "Library/Application Support/rpcs3/dev_flash")):
        if d and os.path.isfile(os.path.join(d, "sys/external/flashMP3.pic")):
            return d
    return ""


def tree_size(d):
    t = 0
    for dp, _, fs in os.walk(d):
        for f in fs:
            try:
                t += os.path.getsize(os.path.join(dp, f))
            except OSError:
                pass
    return t


def fw_watcher():
    """RPCS3's --installfw can stay open after installing: stop it once the
    dev_flash has been complete and unchanged for 20 s."""
    flat = os.path.join(HOME, ".var/app/net.rpcs3.RPCS3/config/rpcs3/dev_flash")
    last, stable = -1, 0
    while not JOB.done:
        time.sleep(5)
        d = next((x for x in (flat, os.path.join(HOME, ".config/rpcs3/dev_flash")) if os.path.isfile(os.path.join(x, "sys/external/flashMP3.pic"))), "")
        if not d:
            continue
        sz = tree_size(d)
        stable = stable + 1 if sz == last else 0
        last = sz
        if stable >= 4:
            JOB.add("-- firmware looks installed (dev_flash complete and unchanged): closing RPCS3")
            JOB.stop()
            return


def steps(body):
    """Whitelisted actions. Returns (name, argv, watcher) or raises ValueError."""
    step = body.get("step")
    path = os.path.expanduser(str(body.get("path", "")))
    if step == "prepare":
        if not os.path.exists(path):
            raise ValueError("choose the game ISO or folder first")
        return "prepare", ["bash", "scripts/prepare_disc.sh", path], None
    if step == "setup":
        return "setup", ["bash", "scripts/setup.sh"], None
    if step == "lift":
        return "lift", ["bash", "scripts/lift.sh"], None
    if step == "build":
        jobs, mem = auto_jobs()
        return "build", ["env", "JOBS=%s" % (cfg["jobs"] or jobs), "PORT_BUILD_MEM=%dG" % mem, "bash", "scripts/build.sh"], None
    if step == "firmware":
        if not os.path.isfile(path) or not path.upper().endswith(".PUP"):
            raise ValueError("choose the PS3UPDAT.PUP file first")
        if shutil.which("rpcs3"):
            return "firmware", ["rpcs3", "--installfw", path], fw_watcher
        if shutil.which("flatpak"):
            return "firmware", ["flatpak", "run", "--filesystem=" + os.path.dirname(path), "net.rpcs3.RPCS3", "--installfw", path], fw_watcher
        raise ValueError("RPCS3 not found: install it (rpcs3.net, or flatpak install flathub net.rpcs3.RPCS3)")
    raise ValueError("unknown step")


def run_status():
    env = make_env()
    try:
        r = subprocess.run(["bash", "scripts/doctor.sh"], cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
        out = r.stdout
    except Exception as e:
        return {"error": str(e)}
    sections, cur = [], None
    for line in out.splitlines():
        if line and not line.startswith(" ") and line not in ("",) and not line.startswith(("Ready", "Not ready")) \
                and not re.match(r"\d+ required", line):
            cur = {"title": line.strip(), "items": []}
            sections.append(cur)
            continue
        m = re.match(r"\s+(ok|warn|MISSING)\s+(.*)", line)
        if m and cur is not None:
            cur["items"].append({"level": m.group(1), "text": m.group(2)})
        elif line.startswith("          ") and cur and cur["items"]:
            cur["items"][-1]["text"] += "\n" + line.strip()
    return {"sections": sections, "flash": find_flash(), "jobs": auto_jobs()[0], "cfg": cfg,
            "home": HOME, "root": ROOT, "update": list(update_source()),
            "version": read_text(os.path.join(ROOT, ".bundle_version"))}


def list_dir(path, kind):
    path = os.path.abspath(os.path.expanduser(path or HOME))
    if not os.path.isdir(path):
        path = HOME
    exts = {"iso": (".iso",), "pup": (".pup",)}.get(kind, ())
    dirs, files = [], []
    try:
        for e in sorted(os.scandir(path), key=lambda e: e.name.lower()):
            if e.name.startswith("."):
                continue
            try:
                if e.is_dir():
                    dirs.append(e.name)
                elif exts and e.name.lower().endswith(exts):
                    files.append(e.name)
            except OSError:
                pass
    except OSError:
        pass
    return {"path": path, "parent": os.path.dirname(path), "dirs": dirs, "files": files}


def play():
    env = make_env()
    flash = find_flash()
    if flash:
        env["PS3_DEV_FLASH"] = flash
    if not os.path.isfile(os.path.join(ROOT, "work/port/build/PacificRiftRecomp")):
        raise ValueError("the port is not built yet")
    log = open(os.path.join(ROOT, "work", "runs", "gui-launch.out"), "ab") if os.path.isdir(os.path.join(ROOT, "work", "runs")) else subprocess.DEVNULL
    subprocess.Popen(["bash", "work/run.sh"], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)


# -------------------------------------------------------------- updates ----
UA = "motorstorm-setup-gui"
LAST_CHECK = {}


def update_source():
    repo, ref = cfg.get("update_repo", ""), cfg.get("update_ref", "")
    if not repo:
        parts = read_text(os.path.join(ROOT, ".update_source")).split()
        if len(parts) >= 2:
            repo, ref = parts[0], parts[1]
    repo, ref = repo.strip(), (ref or "main").strip()
    if not re.fullmatch(r"[\w.-]+/[\w.-]+", repo) or not re.fullmatch(r"[\w./-]+", ref):
        return "", ref
    return repo, ref


def gh_get(url, binary=False, limit=120 << 20):
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": "application/vnd.github+json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            data = r.read(limit + 1)
    except urllib.error.HTTPError as e:
        raise ValueError("GitHub answered %d %s" % (e.code, "(rate limit reached, try later)" if e.code == 403 else e.reason))
    except (urllib.error.URLError, OSError) as e:
        raise ValueError("could not reach GitHub: %s" % getattr(e, "reason", e))
    if len(data) > limit:
        raise ValueError("download is larger than expected; stopped")
    return data if binary else json.loads(data)


def rebuild_hint(files):
    if any(f.startswith("work/patches/") or f.startswith("work/tools/") or f in ("scripts/lift.sh", "scripts/setup.sh") for f in files):
        return "lift"      # the lifted code or the toolkit patch may change: lift and build again
    if any(f.startswith("work/port/") or f == "scripts/build.sh" for f in files):
        return "build"
    return ""


def check_update():
    if os.path.isdir(os.path.join(ROOT, ".git")):
        raise ValueError("this workspace is a git checkout: update it with git pull")
    repo, ref = update_source()
    if not repo:
        raise ValueError("no update source set (owner/repo and a branch or tag, under Build settings)")
    latest = gh_get("https://api.github.com/repos/%s/commits/%s" % (repo, urllib.parse.quote(ref, safe="/")))
    sha = latest["sha"]
    msg = (latest.get("commit", {}).get("message") or "").splitlines()[0:1]
    date = (latest.get("commit", {}).get("committer", {}) or {}).get("date", "")[:10]
    cur = read_text(os.path.join(ROOT, ".bundle_version")).replace("-dirty", "")
    out = {"repo": repo, "ref": ref, "current": cur, "latest": sha[:7], "can_apply": False, "files": []}
    LAST_CHECK.clear()
    if cur and sha.startswith(cur):
        out["message"] = "Up to date (%s, %s@%s)." % (cur, repo, ref)
        return out
    try:
        cmp_ = gh_get("https://api.github.com/repos/%s/compare/%s...%s" % (repo, cur, sha)) if cur else None
    except ValueError:
        cmp_ = None
    what = "%s%s" % (msg[0] if msg else "", " (%s)" % date if date else "")
    if cmp_ and cmp_.get("status") == "ahead":
        files = [f["filename"] for f in cmp_.get("files", [])]
        out.update(can_apply=True, files=files, ahead=cmp_.get("ahead_by", 0), rebuild=rebuild_hint(files))
        LAST_CHECK.update(repo=repo, sha=sha)
        out["message"] = "Update available: %d new change(s), %s -> %s. Latest: %s" % (cmp_.get("ahead_by", 0), cur, sha[:7], what)
    elif cmp_ and cmp_.get("status") in ("behind", "diverged"):
        out["message"] = ("%s@%s is %s relative to this workspace (%s): not applying it automatically. Latest there: %s"
                          % (repo, ref, cmp_["status"], cur, what))
    else:
        out["message"] = ("This workspace's version (%s) is not known on %s@%s, so I can't tell whether %s is newer: not applying it. Latest: %s"
                          % (cur or "none", repo, ref, sha[:7], what))
    return out


ALLOWED_FILES = {"README.md", "LICENSE", "CREDITS.md", "version.txt", ".gitignore"}


def allowed_path(rel):
    if not rel or rel.startswith("/") or ".." in rel.split("/"):
        return False
    return (rel in ALLOWED_FILES or rel.startswith(("scripts/", "work/"))
            or (rel.startswith("docs/") and rel.count("/") == 1 and rel.endswith(".md")))


def apply_update():
    if not LAST_CHECK:
        raise ValueError("check for updates first")
    if os.path.isdir(os.path.join(ROOT, ".git")):
        raise ValueError("this workspace is a git checkout: update it with git pull")
    repo, sha = LAST_CHECK["repo"], LAST_CHECK["sha"]
    data = gh_get("https://codeload.github.com/%s/tar.gz/%s" % (repo, sha), binary=True)
    backup = os.path.join(ROOT, ".update-backup", time.strftime("%Y%m%d-%H%M%S"))
    changed, skipped = [], 0
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tf:
        for m in tf:
            rel = m.name.split("/", 1)[1] if "/" in m.name else ""
            if not m.isreg() or not allowed_path(rel):
                skipped += 1
                continue
            f = tf.extractfile(m)
            if f and place_file(ROOT, rel, f.read(), m.mode & 0o777, backup):
                changed.append(rel)
    write_text(os.path.join(ROOT, ".bundle_version"), sha[:7])
    hint = rebuild_hint(changed)
    LAST_CHECK.clear()
    msg = "Updated to %s: %d file(s) changed." % (sha[:7], len(changed))
    if changed and os.path.isdir(backup):
        msg += " Files you had changed were saved to .update-backup/."
    if hint == "lift":
        msg += " The toolkit, patches or lift tools changed: run Toolkit, Lift and Build again."
    elif hint == "build":
        msg += " The port's sources changed: run Build again."
    elif changed and any(c == "scripts/setup_gui.py" for c in changed):
        msg += " The wizard itself changed: restart it to use the new version."
    return {"message": msg, "changed": changed, "rebuild": hint, "version": sha[:7]}


# ---------------------------------------------------------------- http ----
class H(BaseHTTPRequestHandler):
    server_version = "setup-gui"

    def log_message(self, *a):
        pass

    def _ok(self):
        host = self.headers.get("Host", "")
        if host.split(":")[0] not in ("127.0.0.1", "localhost"):
            return False
        origin = self.headers.get("Origin")
        if origin and urlparse(origin).hostname not in ("127.0.0.1", "localhost"):
            return False
        q = parse_qs(urlparse(self.path).query)
        return secrets.compare_digest(self.headers.get("X-Token", (q.get("t") or [""])[0]), TOKEN)

    def _send(self, code, body, ctype="application/json"):
        data = body if isinstance(body, bytes) else (body if isinstance(body, str) else json.dumps(body)).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype + "; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if not self._ok():
            return self._send(403, {"error": "forbidden"})
        u = urlparse(self.path)
        q = parse_qs(u.query)
        if u.path == "/":
            return self._send(200, PAGE.replace("__TOKEN__", TOKEN), "text/html")
        if u.path == "/api/status":
            return self._send(200, run_status())
        if u.path == "/api/log":
            return self._send(200, JOB.snapshot(int((q.get("since") or ["0"])[0] or 0)))
        if u.path == "/api/ls":
            return self._send(200, list_dir((q.get("path") or [""])[0], (q.get("kind") or [""])[0]))
        if u.path == "/api/update/check":
            try:
                return self._send(200, check_update())
            except ValueError as e:
                return self._send(400, {"error": str(e)})
            except Exception as e:      # an unexpected reply from GitHub
                return self._send(502, {"error": "unexpected update reply: %s" % e})
        self._send(404, {"error": "not found"})

    def do_POST(self):
        if not self._ok():
            return self._send(403, {"error": "forbidden"})
        n = int(self.headers.get("Content-Length") or 0)
        try:
            body = json.loads(self.rfile.read(n) or b"{}")
        except ValueError:
            return self._send(400, {"error": "bad json"})
        u = urlparse(self.path).path
        try:
            if u == "/api/config":
                for k in ("prefix", "jobs", "flash", "update_repo", "update_ref"):
                    if k in body:
                        cfg[k] = str(body[k]).strip()
                if "clean_env" in body:
                    cfg["clean_env"] = bool(body["clean_env"])
                save_cfg()
                return self._send(200, {"ok": True})
            if u == "/api/run":
                name, argv, watcher = steps(body)
                if not JOB.start(name, argv, make_env(), watcher):
                    return self._send(409, {"error": "something is already running"})
                return self._send(200, {"ok": True})
            if u == "/api/update/apply":
                return self._send(200, apply_update())
            if u == "/api/stop":
                JOB.stop()
                return self._send(200, {"ok": True})
            if u == "/api/play":
                play()
                return self._send(200, {"ok": True})
        except ValueError as e:
            return self._send(400, {"error": str(e)})
        self._send(404, {"error": "not found"})


PAGE = r"""<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>MotorStorm PR setup</title>
<style>
:root{--bg:#fff;--fg:#1d2329;--mut:#5b6672;--card:#f4f6f8;--line:#d7dde3;--ok:#1a7f37;--warn:#9a6700;--bad:#cf222e;--acc:#0b63ce;--logbg:#0e1319;--logfg:#d6dde5}
@media(prefers-color-scheme:dark){:root{--bg:#12161b;--fg:#e6ebf0;--mut:#98a3af;--card:#1a2027;--line:#2c353f;--ok:#3fb950;--warn:#d29922;--bad:#f85149;--acc:#4c9aff}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,sans-serif}
main{max-width:900px;margin:0 auto;padding:20px 16px 60px}h1{font-size:1.4rem;margin:0 0 4px}p.sub{color:var(--mut);margin:0 0 18px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px 16px;margin:12px 0}
.card h2{font-size:1rem;margin:0 0 8px;display:flex;gap:8px;align-items:center}
.badge{font-size:.75rem;padding:1px 8px;border-radius:99px;border:1px solid currentColor}
.ok{color:var(--ok)}.warn{color:var(--warn)}.bad{color:var(--bad)}.mut{color:var(--mut)}
ul.items{list-style:none;margin:6px 0;padding:0;font-size:.9rem}ul.items li{padding:1px 0;white-space:pre-wrap}
button{font:inherit;padding:6px 14px;border-radius:7px;border:1px solid var(--line);background:var(--bg);color:var(--fg);cursor:pointer}
button.pri{background:var(--acc);border-color:var(--acc);color:#fff}button:disabled{opacity:.5;cursor:default}
input[type=text]{font:inherit;padding:6px 8px;border-radius:7px;border:1px solid var(--line);background:var(--bg);color:var(--fg);width:100%}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin:8px 0}.row>input{flex:1;min-width:200px}
pre#log{background:var(--logbg);color:var(--logfg);border-radius:8px;padding:10px;height:280px;overflow:auto;font:12px/1.4 ui-monospace,monospace;white-space:pre-wrap;margin:8px 0}
progress{width:100%;height:10px}small{color:var(--mut)}
dialog{border:1px solid var(--line);border-radius:10px;background:var(--bg);color:var(--fg);width:min(640px,94vw);padding:14px}
dialog::backdrop{background:#0008}#ls{max-height:50vh;overflow:auto;border:1px solid var(--line);border-radius:7px;margin:8px 0}
#ls div{padding:5px 10px;cursor:pointer;border-bottom:1px solid var(--line)}#ls div:hover{background:var(--card)}
</style></head><body><main>
<h1>MotorStorm: Pacific Rift setup</h1>
<p class="sub">Prepares your own game, then lifts, builds and starts the port. Nothing leaves this computer.</p>

<div class="card"><h2>1. Check this computer <span id="b0" class="badge mut">…</span></h2><div id="doctor"></div>
 <div class="row"><button id="recheck">Check again</button></div>
 <details><summary>Build settings</summary>
  <div class="row"><label>Local libraries folder (SDL2, glslang) <small>optional</small></label><input id="prefix" type="text" placeholder="e.g. ~/.local/ps3deps"></div>
  <div class="row"><label><input id="clean" type="checkbox"> Build in a clean environment (ignore CC, CXX, LDFLAGS, CMAKE_SYSROOT from this shell)</label></div>
  <div class="row"><label>Update source <small>owner/repo</small></label><input id="updrepo" type="text" placeholder="owner/repo"> <input id="updref" type="text" style="max-width:200px" placeholder="branch or tag"></div>
  <div class="row"><label>Parallel build jobs</label><input id="jobs" type="text" style="max-width:90px" placeholder="auto"> <small id="jobsnote"></small></div>
 </details></div>

<div class="card"><h2>2. Your game <span id="b1" class="badge mut">…</span></h2>
 <small>Pick your decrypted game ISO, or a folder holding PS3_GAME. It must be BCES-00129 (Europe) 01.00. RPCS3 is used to decrypt the executable.</small>
 <div class="row"><input id="gamepath" type="text" placeholder="/path/to/game.iso or folder"><button data-pick="gamepath" data-kind="iso">Browse…</button></div>
 <div class="row"><button class="pri" id="prepare">Prepare game files</button></div></div>

<div class="card"><h2>3. Firmware <span id="b2" class="badge mut">…</span></h2>
 <small>The game needs the PS3 system firmware's files (fonts, MP3 decoder). Pick PS3UPDAT.PUP and RPCS3 installs it.</small>
 <div class="row"><input id="puppath" type="text" placeholder="/path/to/PS3UPDAT.PUP"><button data-pick="puppath" data-kind="pup">Browse…</button></div>
 <div class="row"><button class="pri" id="firmware">Install firmware</button></div></div>

<div class="card"><h2>4. Lift and build <span id="b3" class="badge mut">…</span></h2>
 <small>Fetches the toolkit, translates the game's code on this machine, then compiles it. The build takes a long time and a lot of memory.</small>
 <div class="row"><button class="pri" id="all">Do all steps</button> <button id="setup">Toolkit</button> <button id="lift">Lift</button> <button id="build">Build</button></div></div>

<div class="card"><h2>5. Play <span id="b4" class="badge mut">…</span></h2>
 <div class="row"><button class="pri" id="play">Start the game</button> <small>Opens its own window with the start screen.</small></div></div>

<div class="card"><h2>Updates <span id="ver" class="badge mut"></span></h2>
 <small>Checks GitHub only when you press the button. An update replaces just the port's scripts and sources in this folder; your game, saves and settings are not touched, and any file you changed is saved first.</small>
 <div class="row"><button id="updcheck">Check for updates</button> <button class="pri" id="updgo" hidden>Update now</button></div>
 <div id="updout" class="mut"></div></div>

<div class="card"><h2>Progress <span id="jobname" class="badge mut"></span></h2>
 <progress id="prog" value="0" max="1" hidden></progress>
 <pre id="log"></pre><div class="row"><button id="stop" disabled>Stop</button></div></div>
<dialog id="dlg"><b id="dtitle"></b><div class="row"><input id="dpath" type="text"><button id="dgo">Go</button></div><div id="ls"></div>
 <div class="row"><button id="dchoose" class="pri">Use this folder</button><button id="dcancel">Cancel</button></div></dialog>
</main>
<script>
const T="__TOKEN__",$=id=>document.getElementById(id);
const api=(p,o={})=>fetch(p,{...o,headers:{"X-Token":T,"Content-Type":"application/json"}}).then(r=>r.json().then(j=>{if(!r.ok)throw new Error(j.error||r.status);return j}));
const post=(p,b)=>api(p,{method:"POST",body:JSON.stringify(b||{})});
let st=null,since=0,running=false,chain=[],chainName="";
function badge(id,lvl,txt){const e=$(id);e.className="badge "+lvl;e.textContent=txt}
const sec=(t)=>st.sections.find(s=>s.title.startsWith(t))||{items:[]};
const lvl=s=>s.items.some(i=>i.level=="MISSING")?"bad":s.items.some(i=>i.level=="warn")?"warn":"ok";
async function refresh(){
 try{st=await api("/api/status")}catch(e){return}
 if(st.error){$("doctor").textContent=st.error;return}
 $("doctor").innerHTML=st.sections.filter(s=>["Platform","Environment","Tools"].includes(s.title)).map(s=>"<b>"+s.title+"</b><ul class=items>"+s.items.map(i=>"<li class='"+(i.level=="ok"?"ok":i.level=="warn"?"warn":"bad")+"'>"+(i.level=="ok"?"✓ ":i.level=="warn"?"! ":"✗ ")+esc(i.text)+"</li>").join("")+"</ul>").join("");
 const m=["Platform","Environment","Tools"].map(t=>lvl(sec(t)));
 badge("b0",m.includes("bad")?"bad":m.includes("warn")?"warn":"ok",m.includes("bad")?"needs attention":m.includes("warn")?"ok, with notes":"ready");
 const g=sec("Game files");badge("b1",lvl(g),lvl(g)=="ok"?"ready":"missing");
 const f=sec("Firmware");badge("b2",lvl(f),lvl(f)=="ok"?"installed":"missing");
 const b=sec("Built");const built=b.items.length&&b.items.every(i=>i.level=="ok");badge("b3",built?"ok":"warn",built?"built":"not built");
 badge("b4",built&&lvl(g)=="ok"?"ok":"mut",built?"ready":"build first");
 $("ver").textContent=st.version?"version "+st.version:"";
 if(!$("prefix").dataset.set){$("prefix").value=st.cfg.prefix;$("updrepo").value=st.cfg.update_repo;$("updref").value=st.cfg.update_ref;$("updrepo").placeholder=st.update[0]||"owner/repo";$("updref").placeholder=st.update[1]||"branch or tag";$("clean").checked=st.cfg.clean_env;$("jobs").value=st.cfg.jobs;$("jobsnote").textContent="auto: "+st.jobs;$("prefix").dataset.set=1}
}
const esc=s=>s.replace(/[&<>]/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;"}[c]));
async function saveCfg(){await post("/api/config",{prefix:$("prefix").value,clean_env:$("clean").checked,jobs:$("jobs").value,update_repo:$("updrepo").value,update_ref:$("updref").value})}
["prefix","clean","jobs","updrepo","updref"].forEach(i=>$(i).addEventListener("change",async()=>{await saveCfg();refresh()}));
async function run(step,extra){await saveCfg();try{await post("/api/run",{step,...extra});since=0;$("log").textContent=""}catch(e){alert(e.message);chain=[]}}
$("recheck").onclick=refresh;
$("updcheck").onclick=async()=>{await saveCfg();const o=$("updout");o.className="mut";o.textContent="Checking…";$("updgo").hidden=true;
 try{const j=await api("/api/update/check");o.textContent=j.message+(j.files&&j.files.length?"\nChanged files: "+j.files.slice(0,12).join(", ")+(j.files.length>12?" …":""):"");o.style.whiteSpace="pre-wrap";$("updgo").hidden=!j.can_apply}
 catch(e){o.className="bad";o.textContent="Could not check: "+e.message}};
$("updgo").onclick=async()=>{if(!confirm("Update the port's scripts and sources in this folder? Files you changed are saved first."))return;const o=$("updout");o.textContent="Updating…";
 try{const j=await post("/api/update/apply",{});o.className="ok";o.textContent=j.message;$("updgo").hidden=true;refresh()}catch(e){o.className="bad";o.textContent="Update failed: "+e.message}};
$("prepare").onclick=()=>run("prepare",{path:$("gamepath").value});
$("firmware").onclick=()=>run("firmware",{path:$("puppath").value});
["setup","lift","build"].forEach(s=>$(s).onclick=()=>run(s));
$("all").onclick=()=>{chain=["setup","lift","build"];if($("gamepath").value&&lvl(sec("Game files"))!="ok")chain.unshift("prepare");next()};
function next(){const s=chain.shift();if(!s)return;run(s,s=="prepare"?{path:$("gamepath").value}:{})}
$("stop").onclick=()=>{chain=[];post("/api/stop")};
$("play").onclick=()=>post("/api/play").catch(e=>alert(e.message));
async function poll(){
 try{const j=await api("/api/log?since="+since);
  if(j.lines.length){const l=$("log"),bot=l.scrollTop+l.clientHeight>=l.scrollHeight-20;l.textContent+=j.lines.join("\n")+"\n";if(bot)l.scrollTop=l.scrollHeight}
  since=j.next;$("jobname").textContent=j.name?(j.running?"running: ":"last: ")+j.name:"";$("jobname").className="badge "+(j.running?"warn":j.code===0?"ok":j.code==null?"mut":"bad");
  const p=$("prog");if(j.progress&&j.running){p.hidden=false;p.max=j.progress[1];p.value=j.progress[0]}else if(!j.running)p.hidden=true;
  $("stop").disabled=!j.running;
  if(running&&!j.running){running=false;await refresh();if(j.code===0&&chain.length)next();else if(j.code!==0)chain=[]}
  if(j.running)running=true;
 }catch(e){}
 setTimeout(poll,800)}
// folder / file picker
let pick=null;
async function ls(p){const j=await api("/api/ls?kind="+pick.kind+"&path="+encodeURIComponent(p||""));$("dpath").value=j.path;
 $("ls").innerHTML="";const add=(t,cb,c)=>{const d=document.createElement("div");d.textContent=t;d.className=c||"";d.onclick=cb;$("ls").appendChild(d)};
 add("↑ ..",()=>ls(j.parent));j.dirs.forEach(d=>add("📁 "+d,()=>ls(j.path+"/"+d)));j.files.forEach(f=>add("📄 "+f,()=>{$(pick.id).value=j.path+"/"+f;$("dlg").close()}))}
document.querySelectorAll("[data-pick]").forEach(b=>b.onclick=()=>{pick={id:b.dataset.pick,kind:b.dataset.kind};$("dtitle").textContent=pick.kind=="pup"?"Choose PS3UPDAT.PUP":"Choose the game ISO, or a folder";$("dchoose").hidden=pick.kind=="pup";$("dlg").showModal();ls($(pick.id).value.replace(/\/[^/]*$/,"")||st&&st.home)});
$("dgo").onclick=()=>ls($("dpath").value);$("dcancel").onclick=()=>$("dlg").close();$("dchoose").onclick=()=>{$(pick.id).value=$("dpath").value;$("dlg").close()};
refresh();poll();setInterval(()=>{if(!running)refresh()},20000);
</script></body></html>"""


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", help="workspace folder (default: this checkout, or ~/MotorStormPR for the executable)")
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--no-browser", action="store_true")
    a = ap.parse_args()
    if sys.platform not in ("linux", "darwin"):
        sys.exit("setup_gui: Linux and macOS only (the scripts it drives are bash)")
    root = pick_root(a.root)
    ensure_workspace(root)
    if not os.path.isfile(os.path.join(root, "scripts", "doctor.sh")):
        sys.exit("setup_gui: %s is not a workspace (no scripts/doctor.sh)" % root)
    set_root(root)
    print("Workspace: %s" % ROOT, flush=True)
    srv = ThreadingHTTPServer(("127.0.0.1", a.port), H)
    url = "http://127.0.0.1:%d/?t=%s" % (srv.server_address[1], TOKEN)
    print("Setup wizard: %s\n(opens in your browser; Ctrl+C here to quit)" % url, flush=True)
    if not a.no_browser:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        JOB.stop()


if __name__ == "__main__":
    main()
