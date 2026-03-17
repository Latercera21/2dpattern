# -*- coding: utf-8 -*-
import subprocess
import os
import re
import urllib.request

YTDLP_EXE   = r"C:\Users\Administrador\Downloads\python\yt-dlp.exe"
COOKIES_TXT = r"C:\Users\Administrador\Downloads\python\cookies.txt"

# Rutas comunes de node.exe en Windows
NODE_PATHS = [
    r"C:\Program Files\nodejs\node.exe",
    r"C:\Program Files (x86)\nodejs\node.exe",
]

def encontrar_node():
    r = subprocess.run(["node", "--version"], capture_output=True)
    if r.returncode == 0:
        return "node"
    for ruta in NODE_PATHS:
        if os.path.isfile(ruta):
            return ruta
    return None

def descargar_ytdlp():
    if os.path.isfile(YTDLP_EXE):
        return
    print("[*] Descargando yt-dlp.exe...")
    urllib.request.urlretrieve(
        "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe",
        YTDLP_EXE
    )
    print("[+] yt-dlp.exe listo.\n")

def verificar_ffmpeg():
    return subprocess.run(["ffmpeg", "-version"], capture_output=True).returncode == 0

def limpiar():
    os.system("cls")

def segundos_a_hms(s):
    s = int(s)
    return "%02d:%02d:%02d" % (s // 3600, (s % 3600) // 60, s % 60)

def hms_a_segundos(t):
    p = t.strip().split(":")
    if len(p) == 3: return int(p[0])*3600 + int(p[1])*60 + int(p[2])
    if len(p) == 2: return int(p[0])*60 + int(p[1])
    return int(p[0])

def validar_tiempo(t):
    return re.match(r"^\d{1,2}:\d{2}(:\d{2})?$|^\d+$", t.strip()) is not None

def args_node(node_path):
    if not node_path:
        return []
    if node_path == "node":
        return ["--js-runtimes", "node"]
    return ["--js-runtimes", "node:" + node_path]

def elegir_cookies():
    print("\n  Autenticacion:")
    print("    [0] Sin cookies  (video publico)")
    print("    [1] Chrome       (Chrome debe estar CERRADO)")
    print("    [2] Edge         (Edge debe estar CERRADO)")
    print("    [3] Firefox      (Firefox debe estar CERRADO)")
    print("    [4] Brave        (Brave debe estar CERRADO)")
    if os.path.isfile(COOKIES_TXT):
        print("    [5] cookies.txt  [ENCONTRADO]")
    else:
        print("    [5] cookies.txt  (no encontrado)")
    op = input("\n  Opcion [0-5] (Enter = sin cookies): ").strip()
    mapa = {"1": "chrome", "2": "edge", "3": "firefox", "4": "brave"}
    if op in mapa:
        return ["--cookies-from-browser", mapa[op]]
    if op == "5":
        if os.path.isfile(COOKIES_TXT):
            return ["--cookies", COOKIES_TXT]
        print("[!] cookies.txt no encontrado.")
        input("Presiona Enter para salir...")
        exit()
    return []

def obtener_info(url, cookies_args, node_path):
    cmd = [YTDLP_EXE] + args_node(node_path) + cookies_args + [
        "--print", "%(title)s|||%(uploader)s|||%(duration)s|||%(view_count)s",
        "--no-download", url
    ]
    r = subprocess.run(cmd, capture_output=True, text=True)
    partes = r.stdout.strip().split("|||")
    if len(partes) == 4 and partes[0] not in ("N/A", "NA", ""):
        return partes[0], partes[1], partes[2], partes[3]
    if r.stderr:
        for line in r.stderr.strip().splitlines()[-6:]:
            if line.strip():
                print("    " + line)
    return "N/A", "N/A", "0", "0"

def listar_formatos(url, cookies_args, node_path):
    cmd = [YTDLP_EXE] + args_node(node_path) + cookies_args + ["--list-formats", url]
    subprocess.run(cmd)

def elegir_calidad(tiene_ffmpeg, solo_audio, url, cookies_args, node_path):
    if solo_audio:
        print("\n  Calidad de audio:")
        print("    [1] Mejor calidad (MP3 320kbps)")
        print("    [2] Media         (MP3 192kbps)")
        print("    [3] Baja          (MP3 128kbps)")
        op = input("\n  Opcion [1-3] (Enter = mejor): ").strip()
        return {"1": "0", "2": "5", "3": "9"}.get(op, "0")
    else:
        print("\n  Calidad de video:")
        if tiene_ffmpeg:
            print("    [1] Mejor calidad disponible")
            print("    [2] 1080p")
            print("    [3] 720p")
            print("    [4] 480p")
            print("    [5] 360p")
            print("    [6] Ver todos los formatos y elegir ID manual")
        else:
            print("    [!] ffmpeg no detectado")
            print("    [1] Mejor formato unico")
            print("    [2] 720p")
            print("    [3] 480p")
            print("    [4] 360p")
        op = input("\n  Opcion (Enter = mejor): ").strip()
        if op == "6":
            listar_formatos(url, cookies_args, node_path)
            fmt_id = input("\n  ID de formato (ej: 137+140): ").strip()
            return fmt_id if fmt_id else ""
        return op

def formato_video(opcion, tiene_ffmpeg):
    if tiene_ffmpeg:
        mapa = {
            "":  "bestvideo[ext=mp4]+bestaudio[ext=m4a]/bestvideo+bestaudio/best",
            "1": "bestvideo[ext=mp4]+bestaudio[ext=m4a]/bestvideo+bestaudio/best",
            "2": "bestvideo[height<=1080][ext=mp4]+bestaudio[ext=m4a]/best[height<=1080]",
            "3": "bestvideo[height<=720][ext=mp4]+bestaudio[ext=m4a]/best[height<=720]",
            "4": "bestvideo[height<=480][ext=mp4]+bestaudio[ext=m4a]/best[height<=480]",
            "5": "bestvideo[height<=360][ext=mp4]+bestaudio[ext=m4a]/best[height<=360]",
        }
        return mapa.get(opcion, mapa["1"])
    else:
        mapa = {
            "":  "best[ext=mp4]/best",
            "1": "best[ext=mp4]/best",
            "2": "best[height<=720][ext=mp4]/best[height<=720]",
            "3": "best[height<=480][ext=mp4]/best[height<=480]",
            "4": "best[height<=360][ext=mp4]/best[height<=360]",
        }
        return mapa.get(opcion, mapa["1"])

def menu():
    limpiar()
    tiene_ffmpeg = verificar_ffmpeg()
    node_path    = encontrar_node()

    print("=" * 55)
    print("   YouTube Downloader  |  Windows 10")
    print("   ffmpeg : " + ("[+] OK" if tiene_ffmpeg else "[!] NO detectado"))
    print("   node.js: " + ("[+] OK - " + node_path if node_path else "[!] NO detectado"))
    print("=" * 55)

    url = input("\n  URL de YouTube: ").strip()
    if not url:
        print("[!] URL vacia."); return

    cookies_args = elegir_cookies()

    print("\n[*] Obteniendo informacion...")
    titulo, canal, duracion_s, vistas = obtener_info(url, cookies_args, node_path)

    if titulo == "N/A":
        print("\n[!] No se pudo obtener informacion del video.")
        if cookies_args and "--cookies" in cookies_args:
            print("    -> Las cookies expiraron. Exportalas de nuevo desde Chrome.")
        elif cookies_args and "--cookies-from-browser" in cookies_args:
            print("    -> Cierra el navegador completamente e intenta de nuevo.")
        input("\nPresiona Enter para salir...")
        return

    duracion = int(duracion_s) if duracion_s.isdigit() else 0
    print("\n  Titulo   : " + titulo)
    print("  Canal    : " + canal)
    print("  Duracion : " + segundos_a_hms(duracion))
    try:
        print("  Vistas   : " + "{:,}".format(int(vistas)))
    except:
        pass

    carpeta_default = os.path.join(os.path.expanduser("~"), "Downloads", "YT_Downloads")
    os.makedirs(carpeta_default, exist_ok=True)
    carpeta_input = input("\n  Carpeta destino [" + carpeta_default + "]: ").strip()
    carpeta = carpeta_input if carpeta_input else carpeta_default
    os.makedirs(carpeta, exist_ok=True)

    print("\n  Tipo de descarga:")
    print("    [1] Video completo")
    print("    [2] Solo audio completo (MP3)")
    print("    [3] Segmento de video")
    print("    [4] Segmento de solo audio (MP3)")
    opcion = input("\n  Opcion [1-4]: ").strip()

    solo_audio = opcion in ("2", "4")
    segmento   = opcion in ("3", "4")

    if segmento and not tiene_ffmpeg:
        print("\n  [!] Los segmentos requieren ffmpeg.")
        input("\n  Presiona Enter para salir...")
        return

    cal_op = elegir_calidad(tiene_ffmpeg, solo_audio, url, cookies_args, node_path)

    inicio_s = fin_s = 0
    sufijo = ""

    if segmento:
        print("\n  Duracion total: " + segundos_a_hms(duracion))
        print("  Formato: HH:MM:SS  o  MM:SS  o  segundos")
        inicio = input("  Tiempo de inicio: ").strip()
        fin    = input("  Tiempo de fin   : ").strip()
        if not validar_tiempo(inicio) or not validar_tiempo(fin):
            print("[!] Formato invalido."); return
        inicio_s = hms_a_segundos(inicio)
        fin_s    = hms_a_segundos(fin)
        if fin_s <= inicio_s:
            print("[!] El fin debe ser mayor que el inicio."); return
        sufijo = " [" + inicio.replace(":", "-") + "_" + fin.replace(":", "-") + "]"

    salida = os.path.join(carpeta, "%(title)s" + sufijo + ".%(ext)s")

    if solo_audio:
        q = cal_op if cal_op in ("0", "5", "9") else "0"
        args = ["-f", "bestaudio/best", "-o", salida,
                "-x", "--audio-format", "mp3", "--audio-quality", q]
    else:
        fmt = formato_video(cal_op, tiene_ffmpeg)
        args = ["-f", fmt, "-o", salida]
        if tiene_ffmpeg:
            args += ["--merge-output-format", "mp4"]

    if segmento:
        args += [
            "--download-sections", "*" + str(inicio_s) + "-" + str(fin_s),
            "--force-keyframes-at-cuts",
        ]

    cmd = [YTDLP_EXE] + args_node(node_path) + cookies_args + args + [url]

    print("\n[*] Iniciando descarga...\n")
    subprocess.run(cmd)

    print("\n[+] Listo. Archivos en: " + carpeta)
    input("\n  Presiona Enter para salir...")

if __name__ == "__main__":
    try:
        descargar_ytdlp()
        menu()
    except KeyboardInterrupt:
        print("\n[!] Cancelado.")
    except Exception as e:
        print("\n[!] Error: " + str(e))
        input("Presiona Enter para salir...")