import subprocess, sys, os, re, argparse, shutil, tempfile

R="\033[91m"; G="\033[92m"; C="\033[96m"; W="\033[0m"
def err(msg):  print(f"{R}[ERROR]{W} {msg}"); sys.exit(1)
def ok(msg):   print(f"{G}[OK]{W}    {msg}")
def info(msg): print(f"{C}[INFO]{W}  {msg}")

def check_deps():
    missing = [x for x in ["yt-dlp","ffmpeg"] if not shutil.which(x)]
    if missing: err(f"Faltan: {', '.join(missing)}\n  pip install yt-dlp\n  Agrega ffmpeg al PATH")
    ok("yt-dlp y ffmpeg encontrados")

def parse_time(t):
    t = t.strip()
    if re.fullmatch(r"\d+(\.\d+)?", t): return float(t)
    parts = t.split(":")
    try: parts = [float(p) for p in parts]
    except: err(f"Tiempo invalido: '{t}' -- usa HH:MM:SS o MM:SS")
    if len(parts)==2: return parts[0]*60+parts[1]
    if len(parts)==3: return parts[0]*3600+parts[1]*60+parts[2]
    err(f"Tiempo invalido: '{t}'")

def fmt(s):
    h=int(s//3600); m=int((s%3600)//60); sec=s%60
    return f"{h:02d}:{m:02d}:{sec:06.3f}"

def fmtf(s):
    h=int(s//3600); m=int((s%3600)//60); sec=s%60
    return f"{h:02d}:{m:02d}:{sec:05.2f}"

QUALITY_MAP = {
    "best":  "bestvideo[ext=mp4]+bestaudio[ext=m4a]/bestvideo+bestaudio/best",
    "1440":  "bestvideo[height<=1440][ext=mp4]+bestaudio[ext=m4a]/bestvideo[height<=1440]+bestaudio/best",
    "1080":  "bestvideo[height<=1080][ext=mp4]+bestaudio[ext=m4a]/bestvideo[height<=1080]+bestaudio/best",
    "720":   "bestvideo[height<=720][ext=mp4]+bestaudio[ext=m4a]/bestvideo[height<=720]+bestaudio/best",
    "480":   "bestvideo[height<=480][ext=mp4]+bestaudio[ext=m4a]/bestvideo[height<=480]+bestaudio/best",
    "360":   "bestvideo[height<=360][ext=mp4]+bestaudio[ext=m4a]/bestvideo[height<=360]+bestaudio/best",
    "audio": "bestaudio[ext=m4a]/bestaudio/best",
}

def run(cmd):
    info("CMD: " + " ".join(str(c) for c in cmd))
    try: subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError as e: err(f"Fallo codigo {e.returncode}")
    except FileNotFoundError as e: err(f"No encontrado: {e}")

def download_full(url, quality, outdir, extra):
    fmt2 = QUALITY_MAP.get(quality, QUALITY_MAP["best"])
    run(["yt-dlp","--js-runtimes","node",
         "--format", fmt2,
         "--merge-output-format","mp4",
         "--output", os.path.join(outdir,"%(title)s.%(ext)s"),
         "--no-playlist","--retries","10","--fragment-retries","10",
         "--concurrent-fragments","4","--progress"] + extra + [url])

def download_segment(url, start, end, quality, outdir, extra):
    duration = end - start
    if duration <= 0: err("El fin debe ser mayor que el inicio.")
    fmt2 = QUALITY_MAP.get(quality, QUALITY_MAP["best"])
    out_path = os.path.join(outdir, f"seg_{fmt(start).replace(':','-')}_{fmt(end).replace(':','-')}.mp4")
    info(f"Segmento pedido: {fmt(start)} -> {fmt(end)} ({fmtf(duration)} de duracion)")
    info("Descargando video completo al temp, luego corta y borra automatico...")
    with tempfile.TemporaryDirectory(prefix="ytdl_") as tmp:
        run(["yt-dlp","--js-runtimes","node",
             "--format", fmt2,
             "--merge-output-format","mp4",
             "--output", os.path.join(tmp,"full.%(ext)s"),
             "--no-playlist","--retries","10","--fragment-retries","10",
             "--concurrent-fragments","4","--progress"] + extra + [url])
        files = [f for f in os.listdir(tmp) if os.path.isfile(os.path.join(tmp,f))]
        if not files: err("No se encontro el archivo descargado.")
        src = os.path.join(tmp, files[0])
        info(f"Cortando {fmtf(start)} -> {fmtf(end)} (sin re-encode, rapido)...")
        run(["ffmpeg","-y",
             "-ss", fmtf(start),
             "-i", src,
             "-t", fmtf(duration),
             "-c", "copy",
             out_path])
        ok(f"Segmento guardado: {out_path}")

def main():
    p = argparse.ArgumentParser(prog="ytdl")
    p.add_argument("url")
    p.add_argument("-q","--quality", choices=list(QUALITY_MAP.keys()), default="best")
    p.add_argument("-s","--start")
    p.add_argument("-e","--end")
    p.add_argument("-o","--outdir", default=os.path.join(os.path.expanduser("~"),"Videos","ytdl"))
    p.add_argument("--cookies-from-browser")
    args = p.parse_args()
    check_deps()
    os.makedirs(args.outdir, exist_ok=True)
    extra=[]
    if args.cookies_from_browser: extra+=["--cookies-from-browser",args.cookies_from_browser]
    if args.start or args.end:
        if not (args.start and args.end): err("Especifica AMBOS --start y --end.")
        download_segment(args.url, parse_time(args.start), parse_time(args.end),
                         args.quality, args.outdir, extra)
    else:
        download_full(args.url, args.quality, args.outdir, extra)
    ok(f"Listo. Archivos en: {args.outdir}")

if __name__ == "__main__":
    main()