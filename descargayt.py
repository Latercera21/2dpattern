# -*- coding: utf-8 -*-
import subprocess
import sys
import os
import re
import urllib.request

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
YTDLP_EXE  = os.path.join(SCRIPT_DIR, "yt-dlp.exe")

def descargar_ytdlp():
    if os.path.isfile(YTDLP_EXE):
        return
    print("[*] Descargando yt-dlp.exe...")
    url = "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe"
    urllib.request.urlretrieve(url, YTDLP_EXE)
    print("[+] yt-dlp.exe listo en: " + YTDLP_EXE + "\n")

def verificar_ffmpeg():
    r = subprocess.run(["ffmpeg", "-version"], capture_output=True)
    return r.returncode == 0

def limpiar():
    os.system("cls")

def segundos_a_hms(s):
    s = int(s)
    return "%02d:%02d:%02d" % (s // 3600, (s % 3600) // 60, s % 60)

def hms_a_segundos(t):
    p = t.strip().split(":")
    if len(p) == 3:
        return int(p[0])*3600 + int(p[1])*60 + int(p[2])
    if len(p) == 2:
        return int(p[0])*60 + int(p[1])
    return int(p[0])

def validar_tiempo(t):
    return re.match(r"^\d{1,2}:\d{2}(:\d{2})?$|^\d+$", t.strip()) is not None

def obtener_info(url):
    r = subprocess.run(
        [YTDLP_EXE, "--print", "%(title)s|||%(uploader)s|||%(duration)s|||%(view_count)s",
         "--no-download", url],
        capture_output=True, text=True
    )
    partes = r.stdout.strip().split("|||")
    if len(partes) == 4:
        return partes[0], partes[1], partes[2], partes[3]
    return "N/A", "N/A", "0", "0"

def listar_formatos(url):
    print("\n[*] Obteniendo formatos disponibles...\n")
    r = subprocess.run(
        [YTDLP_EXE, "--list-formats", url],
        capture_output=False, text=True
    )

def elegir_calidad(tiene_ffmpeg, solo_audio):
    if solo_audio:
        print("\n  Calidad de audio:")
        print("    [1] Mejor calidad disponible (MP3 320kbps)")
        print("    [2] Calidad media (MP3 192kbps)")
        print("    [3] Calidad baja  (MP3 128kbps)")
        op = input("\n  Opcion [1-3] (Enter = mejor): ").strip()
        calidades = {"1": "0", "2": "5", "3": "9"}
        return calidades.get(op, "0")
    else:
        print("\n  Calidad de video:")
        if tiene_ffmpeg:
            print("    [1] Mejor calidad disponible (bestvideo+bestaudio)")
            print("    [2] 1080p")
            print("    [3] 720p")
            print("    [4] 480p")
            print("    [5] 360p")
            print("    [6] Ver todos los formatos y elegir ID manualmente")
        else:
            print("    [!] ffmpeg no detectado - solo formatos sin fusion")
            print("    [1] Mejor formato unico (puede ser 720p maximo)")
            print("    [2] 720p")
            print("    [3] 480p")
            print("    [4] 360p")
        op = input("\n  Opcion (Enter = mejor): ").strip()
        return op

def formato_video(opcion, tiene_ffmpeg, url):
    if tiene_ffmpeg:
        mapa = {
            "": "bestvideo[ext=mp4]+bestaudio[ext=m4a]/bestvideo+bestaudio/best",
            "1": "bestvideo[ext=mp4]+bestaudio[ext=m4a]/bestvideo+bestaudio/best",
            "2": "bestvideo[height<=1080][ext=mp4]+bestaudio[ext=m4a]/best[height<=1080]",
            "3": "bestvideo[height<=720][ext=mp4]+bestaudio[ext=m4a]/best[height<=720]",
            "4": "bestvideo[height<=480][ext=mp4]+bestaudio[ext=m4a]/best[height<=480]",
            "5": "bestvideo[height<=360][ext=mp4]+bestaudio[ext=m4a]/best[height<=360]",
        }
        if opcion == "6":
            listar_formatos(url)
            fmt_id = input("\n  Ingresa el ID de formato (ej: 137+140): ").strip()
            return fmt_id if fmt_id else mapa["1"]
        return mapa.get(opcion, mapa["1"])
    else:
        mapa = {
            "": "best[ext=mp4]/best",
            "1": "best[ext=mp4]/best",
            "2": "best[height<=720][ext=mp4]/best[height<=720]",
            "3": "best[height<=480][ext=mp4]/best[height<=480]",
            "4": "best[height<=360][ext=mp4]/best[height<=360]",
        }
        return mapa.get(opcion, mapa["1"])

def menu():
    limpiar()
    tiene_ffmpeg = verificar_ffmpeg()

    print("=" * 55)
    print("   YouTube Downloader  |  Windows 10")
    if not tiene_ffmpeg:
        print("   [!] ffmpeg NO detectado - calidad y segmentos limitados")
    else:
        print("   [+] ffmpeg OK")
    print("=" * 55)

    url = input("\n  URL de YouTube: ").strip()
    if not url:
        print("[!] URL vacia."); return

    print("\n[*] Obteniendo informacion...")
    titulo, canal, duracion_s, vistas = obtener_info(url)
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
        print("  Instala con: winget install ffmpeg")
        print("  O descarga ffmpeg.exe y ponlo en C:\\Windows\\System32\\")
        input("\n  Presiona Enter para salir...")
        return

    # Selector de calidad
    cal_op = elegir_calidad(tiene_ffmpeg, solo_audio)

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
        q = cal_op if cal_op in ("0","5","9") else "0"
        args = [
            "-f", "bestaudio/best",
            "-o", salida,
            "-x", "--audio-format", "mp3", "--audio-quality", q,
        ]
    else:
        fmt = formato_video(cal_op, tiene_ffmpeg, url)
        args = ["-f", fmt, "-o", salida]
        if tiene_ffmpeg:
            args += ["--merge-output-format", "mp4"]

    if segmento:
        args += [
            "--download-sections", "*" + str(inicio_s) + "-" + str(fin_s),
            "--force-keyframes-at-cuts",
        ]

    args.append(url)

    print("\n[*] Iniciando descarga...\n")
    subprocess.run([YTDLP_EXE] + args)

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