#!/usr/bin/env python3
"""Setup wizard: a small local web page that walks through preparing, lifting,
building and starting the port. It drives the repository's own scripts
(doctor.sh, prepare_disc.sh, setup.sh, lift.sh, build.sh, work/run.sh), so
they stay the source of truth; this only runs them and shows the result.

    scripts/setup_gui.py [--port N] [--no-browser]

Standard library only (no tkinter or GTK needed). It listens on 127.0.0.1
with a random access token and opens your browser. Linux only for now.
It never installs system packages: it tells you what is missing.
Nothing from your game is sent anywhere.
"""
import argparse, json, os, re, secrets, shutil, signal, subprocess, sys, threading, time, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOKEN = secrets.token_urlsafe(24)
HOME = os.path.expanduser("~")
CONFIG_PATH = os.path.join(ROOT, "user", "setup_gui.json")
# Variables a shell may export that make CMake pick up a foreign toolchain.
LEAKY = ["CC", "CXX", "AR", "RANLIB", "LDFLAGS", "CMAKE_SYSROOT", "CMAKE_TOOLCHAIN_FILE", "LD_LIBRARY_PATH"]

cfg = {"prefix": "", "clean_env": True, "jobs": "", "flash": ""}
try:
    with open(CONFIG_PATH) as f:
        cfg.update(json.load(f))
except (OSError, ValueError):
    pass


def save_cfg():
    try:
        os.makedirs(os.path.dirname(CONFIG_PATH), exist_ok=True)
        with open(CONFIG_PATH, "w") as f:
            json.dump(cfg, f)
    except OSError:
        pass


def make_env():
    env = dict(os.environ)
    if cfg["clean_env"]:
        for v in LEAKY:
            env.pop(v, None)
        # Prefer the system's tools over any SDK that sits first in PATH.
        sysdirs = ["/usr/local/bin", "/usr/bin", "/bin"]
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
              os.path.join(HOME, ".var/app/net.rpcs3.RPCS3/config/rpcs3/dev_flash")):
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
            "home": HOME, "root": ROOT}


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
                for k in ("prefix", "jobs", "flash"):
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
 if(!$("prefix").dataset.set){$("prefix").value=st.cfg.prefix;$("clean").checked=st.cfg.clean_env;$("jobs").value=st.cfg.jobs;$("jobsnote").textContent="auto: "+st.jobs;$("prefix").dataset.set=1}
}
const esc=s=>s.replace(/[&<>]/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;"}[c]));
async function saveCfg(){await post("/api/config",{prefix:$("prefix").value,clean_env:$("clean").checked,jobs:$("jobs").value})}
["prefix","clean","jobs"].forEach(i=>$(i).addEventListener("change",async()=>{await saveCfg();refresh()}));
async function run(step,extra){await saveCfg();try{await post("/api/run",{step,...extra});since=0;$("log").textContent=""}catch(e){alert(e.message);chain=[]}}
$("recheck").onclick=refresh;
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
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--no-browser", action="store_true")
    a = ap.parse_args()
    if not sys.platform.startswith("linux"):
        sys.exit("setup_gui: Linux only for now (the scripts it drives are bash)")
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
