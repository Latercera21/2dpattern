@echo off
chcp 65001 >nul
color 0A
title YouTube Downloader
:INICIO
cls
echo ============================================
echo      DESCARGADOR DE YOUTUBE
echo ============================================
echo.
set URL=
set /p URL="  URL del video: "
if "%URL%"=="" (echo URL vacia. & pause & goto INICIO)
echo.
echo  CALIDAD:
echo    1  Mejor disponible
echo    2  1440p
echo    3  1080p
echo    4  720p
echo    5  480p
echo    6  360p
echo    7  Solo audio
echo.
set CAL=
set /p CAL="  Elige calidad [1-7]: "
if "%CAL%"=="1" set FMT=best
if "%CAL%"=="2" set FMT=1440
if "%CAL%"=="3" set FMT=1080
if "%CAL%"=="4" set FMT=720
if "%CAL%"=="5" set FMT=480
if "%CAL%"=="6" set FMT=360
if "%CAL%"=="7" set FMT=audio
if not defined FMT set FMT=best
echo.
set SEG=
set /p SEG="  Descargar solo un segmento? (s/n): "
if /i "%SEG%"=="s" goto SEGMENTO
echo.
echo  Descargando...
echo.
python C:\ytdl\ytdl.py "%URL%" -q %FMT%
goto FIN
:SEGMENTO
echo.
echo  Formato: MM:SS  o  HH:MM:SS
set TINI=
set TFIN=
set /p TINI="  Inicio (ej: 1:30): "
set /p TFIN="  Fin    (ej: 3:45): "
if "%TINI%"=="" goto ERR
if "%TFIN%"=="" goto ERR
echo.
echo  Descargando y cortando segmento...
echo.
python C:\ytdl\ytdl.py "%URL%" -q %FMT% -s %TINI% -e %TFIN%
goto FIN
:ERR
echo  ERROR: Ingresa inicio Y fin.
pause
goto INICIO
:FIN
echo.
echo  Archivos en: %USERPROFILE%\Videos\ytdl
echo.
set OTRO=
set /p OTRO="  Descargar otro video? (s/n): "
if /i "%OTRO%"=="s" goto INICIO
exit