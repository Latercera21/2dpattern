@echo off
cd /d "%~dp0"
nesting.exe figuras.json resultado.json 160 100
echo.
echo Listo. Presiona una tecla para cerrar...
pause >nul
