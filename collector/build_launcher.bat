@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d "C:\Users\ildin\cheat\Troll\collector"

echo.
echo [*] Compilando discord_launcher.exe (standalone)...
cl /EHsc /O2 /MT /std:c++17 ^
    launcher.cpp ^
    user32.lib shell32.lib ws2_32.lib ^
    /Fe:discord_launcher.exe ^
    /link /subsystem:console

if %errorlevel% == 0 (
    echo [OK] discord_launcher.exe pronto!
    echo.
    echo Copie junto com discord.txt e sysinfo.txt
) else (
    echo [ERRO] Falha na compilacao.
)
pause
