Clear-Host

Write-Host "=========================================================" -ForegroundColor Green
Write-Host "           gm2373 LOCKER BUILDER SYSTEM v3.0               " -ForegroundColor Green
Write-Host "=========================================================" -ForegroundColor Green
Write-Host ""

# 1. Coloca o nome
$zipName = Read-Host "Coloque o nome do arquivo ZIP de saida [Enter = 'release.zip']"
if ([string]::IsNullOrWhiteSpace($zipName)) {
    $zipName = "release.zip"
}
# Ensure it ends with .zip
if (-not $zipName.EndsWith(".zip", [System.StringComparison]::OrdinalIgnoreCase)) {
    $zipName += ".zip"
}

# 2. Coloca a senha
$Password = Read-Host "Coloque a senha (sera embutida nos executaveis e protegera o ZIP)"
while ([string]::IsNullOrEmpty($Password)) {
    $Password = Read-Host "A senha nao pode ser vazia! Coloque a senha"
}
$escapedPassword = $Password.Replace('\', '\\').Replace('"', '\"')

# 3. Mostrar CMD?
$showCmdInput = Read-Host "Mostrar CMD? [S/N] (Pressione Enter para 'N')"
$showCmd = $false
if ($showCmdInput -eq "S" -or $showCmdInput -eq "s") {
    $showCmd = $true
}

# 4. Escolha o alvo padrao
Write-Host "`nEscolha o alvo padrao:" -ForegroundColor Yellow
Write-Host "  [1] all" -ForegroundColor Yellow
Write-Host "  [2] local (mesma pasta)" -ForegroundColor Yellow
Write-Host "  [3] mesmo disco" -ForegroundColor Yellow
Write-Host "  [4] discos externos (somente pendrives ou HDs que eu conectar...)" -ForegroundColor Yellow
$targetMode = Read-Host "Opcao (1-4) [Enter = 2]"
if ([string]::IsNullOrWhiteSpace($targetMode) -or $targetMode -lt "1" -or $targetMode -gt "4") {
    $targetMode = "2"
}

$targetDescription = "local (mesma pasta)"
if ($targetMode -eq "1") { $targetDescription = "all" }
elseif ($targetMode -eq "3") { $targetDescription = "mesmo disco" }
elseif ($targetMode -eq "4") { $targetDescription = "discos externos" }

Write-Host ""
Write-Host "---------------------------------------------------------" -ForegroundColor Gray
Write-Host "Resumo da Compilacao:" -ForegroundColor Yellow
Write-Host "  Arquivo de Saida  : $zipName" -ForegroundColor Yellow
Write-Host "  Senha Embutida/ZIP: ********" -ForegroundColor Yellow
Write-Host "  Mostrar CMD       : $(if ($showCmd) { 'Sim' } else { 'Nao (Oculto)' })" -ForegroundColor Yellow
Write-Host "  Alvo Padrao       : $targetDescription" -ForegroundColor Yellow
Write-Host "---------------------------------------------------------" -ForegroundColor Gray
Write-Host ""

$ErrorActionPreference = "Stop"

# Write config.h
Write-Host "[*] Gerando config.h..." -ForegroundColor Cyan
$configContent = @"
#pragma once
#include <string>

#define CONFIG_PASSWORD "$escapedPassword"
#define CONFIG_TARGET_MODE $targetMode
"@
$configContent | Out-File -FilePath "config.h" -Encoding ascii -Force

# Clean up old build artifacts
Write-Host "[*] Limpando artefatos antigos..." -ForegroundColor Cyan
if (Test-Path "locker.exe") { Remove-Item "locker.exe" -Force }
if (Test-Path "unlocker.exe") { Remove-Item "unlocker.exe" -Force }
if (Test-Path $zipName) { Remove-Item $zipName -Force }

# Determine compiler flags based on showCmd
$compilerFlags = @()
if (-not $showCmd) {
    $compilerFlags += "-DBUILD_GUI"
    $compilerFlags += "-mwindows"
}

# Build locker.exe
Write-Host "[*] Compilando locker.exe..." -ForegroundColor Cyan
try {
    & g++ -O3 -std=c++17 $compilerFlags -o locker.exe main.cpp crypto_service.cpp file_service.cpp disk_service.cpp worker_pool.cpp -lbcrypt -lcrypt32 -lpthread
} catch {
    Write-Host "[-] Falha na compilacao do locker.exe." -ForegroundColor Red
    Write-Host "Pressione qualquer tecla para sair..."
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
    exit 1
}

# Build unlocker.exe
Write-Host "[*] Compilando unlocker.exe..." -ForegroundColor Cyan
try {
    & g++ -O3 -std=c++17 -DBUILD_UNLOCKER $compilerFlags -o unlocker.exe main.cpp crypto_service.cpp file_service.cpp disk_service.cpp worker_pool.cpp -lbcrypt -lcrypt32 -lpthread
} catch {
    Write-Host "[-] Falha na compilacao do unlocker.exe." -ForegroundColor Red
    Write-Host "Pressione qualquer tecla para sair..."
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
    exit 1
}

# Locate 7-Zip
$szPath = "C:\Program Files\7-Zip\7z.exe"
if (-not (Test-Path $szPath)) {
    $szPath = (Get-Command "7z.exe" -ErrorAction SilentlyContinue).Source
}

if (-not $szPath) {
    Write-Host "[-] 7-Zip (7z.exe) nao foi encontrado no sistema. Instale o 7-Zip." -ForegroundColor Red
    Write-Host "Pressione qualquer tecla para sair..."
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
    exit 1
}

# Create password-protected ZIP archive
Write-Host "[*] Criando arquivo ZIP criptografado ($zipName)..." -ForegroundColor Cyan
try {
    & $szPath a -p"$Password" -tzip -mem=AES256 $zipName "locker.exe" "unlocker.exe"
} catch {
    Write-Host "[-] Falha ao compactar os executaveis." -ForegroundColor Red
    Write-Host "Pressione qualquer tecla para sair..."
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
    exit 1
}

# Clean up loose executables
Write-Host "[*] Removendo executaveis temporarios..." -ForegroundColor Cyan
Remove-Item "locker.exe" -Force
Remove-Item "unlocker.exe" -Force

Write-Host ""
Write-Host "=========================================================" -ForegroundColor Green
Write-Host "[++] SUCESSO! O arquivo '$zipName' foi gerado com AES-256." -ForegroundColor Green
Write-Host "=========================================================" -ForegroundColor Green
Write-Host ""
Write-Host "Pressione qualquer tecla para fechar..."
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
