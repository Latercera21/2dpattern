@echo off
cd /d "%~dp0"
nesting.exe figuras.json resultado.json 160 1000
echo.
echo Listo. Presiona una tecla para cerrar...
pause >nul
