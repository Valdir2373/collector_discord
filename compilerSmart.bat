@echo off
setlocal

:: ── Verifica argumento ─────────────────────────────────────────────────────────
if "%~1"=="" (
    echo.
    echo  Uso: compilerSmart.bat ^<arquivo.exe^> [senha]
    echo  Exemplo: compilerSmart.bat terror.exe amigo123
    echo  Senha padrao: 1234
    echo.
    goto :eof
)

set "INPUT=%~1"
set "BASENAME=%~n1"
set "OUTPUT=%BASENAME%_packed.zip"
set "PASS=%~2"
if "%PASS%"=="" set "PASS=1234"

:: ── Verifica se o arquivo existe ───────────────────────────────────────────────
if not exist "%INPUT%" (
    echo.
    echo  [ERRO] Arquivo "%INPUT%" nao encontrado.
    echo.
    goto :eof
)

:: ── Tenta usar 7-Zip (ZIP com senha = SmartScreen nao consegue escanear) ───────
set "SEVENZIP="
if exist "C:\Program Files\7-Zip\7z.exe"      set "SEVENZIP=C:\Program Files\7-Zip\7z.exe"
if exist "C:\Program Files (x86)\7-Zip\7z.exe" set "SEVENZIP=C:\Program Files (x86)\7-Zip\7z.exe"

if exist "%OUTPUT%" del /f /q "%OUTPUT%"

echo.
if not "%SEVENZIP%"=="" (
    echo  [7-Zip] Criando ZIP com senha "%PASS%"...
    "%SEVENZIP%" a -tzip -p"%PASS%" -mem=AES256 "%OUTPUT%" "%INPUT%" >nul 2>&1
) else (
    :: Fallback: PowerShell sem senha (menos eficaz contra SmartScreen)
    echo  [Aviso] 7-Zip nao encontrado. Usando ZIP simples sem senha.
    echo  Instale 7-Zip para melhor resultado: https://www.7-zip.org
    echo.
    powershell -NoProfile -Command "Compress-Archive -Path '%INPUT%' -DestinationPath '%OUTPUT%' -CompressionLevel Optimal"
)

:: ── Resultado ──────────────────────────────────────────────────────────────────
if exist "%OUTPUT%" (
    echo.
    echo  [OK] Gerado: %OUTPUT%
    if not "%SEVENZIP%"=="" (
        echo  Senha do ZIP: %PASS%
    )
    powershell -NoProfile -Command "(Get-Item '%INPUT%').length / 1KB | ForEach-Object { '  exe : ' + [math]::Round($_,1) + ' KB' }"
    powershell -NoProfile -Command "(Get-Item '%OUTPUT%').length / 1KB | ForEach-Object { '  zip : ' + [math]::Round($_,1) + ' KB' }"
    echo.
    echo  Como usar:
    echo   1. Mande o ZIP pro amigo
    echo   2. Fala a senha: %PASS%
    echo   3. Ele extrai com 7-Zip ou WinRAR e roda o .exe
    echo   SmartScreen nao consegue escanear ZIP com senha.
) else (
    echo  [ERRO] Falha ao gerar o ZIP.
)

echo.
endlocal
