param(
    [Parameter(Mandatory=$true, HelpMessage="Password to protect the generated ZIP file")]
    [string]$Password
)

$ErrorActionPreference = "Stop"

# 1. Clean up old build artifacts
Write-Host "[*] Cleaning up old artifacts..." -ForegroundColor Cyan
if (Test-Path "locker.exe") { Remove-Item "locker.exe" -Force }
if (Test-Path "unlocker.exe") { Remove-Item "unlocker.exe" -Force }
if (Test-Path "release.zip") { Remove-Item "release.zip" -Force }

# 2. Build locker.exe
Write-Host "[*] Compiling locker.exe..." -ForegroundColor Cyan
& g++ -O3 -std=c++17 -o locker.exe main.cpp crypto_service.cpp file_service.cpp disk_service.cpp worker_pool.cpp -lbcrypt -lcrypt32 -lpthread
if ($LASTEXITCODE -ne 0) {
    Write-Error "[-] Compilation of locker.exe failed."
    exit 1
}
Write-Host "[+] locker.exe compiled successfully." -ForegroundColor Green

# 3. Build unlocker.exe
Write-Host "[*] Compiling unlocker.exe..." -ForegroundColor Cyan
& g++ -O3 -std=c++17 -DBUILD_UNLOCKER -o unlocker.exe main.cpp crypto_service.cpp file_service.cpp disk_service.cpp worker_pool.cpp -lbcrypt -lcrypt32 -lpthread
if ($LASTEXITCODE -ne 0) {
    Write-Error "[-] Compilation of unlocker.exe failed."
    exit 1
}
Write-Host "[+] unlocker.exe compiled successfully." -ForegroundColor Green

# 4. Locate 7-Zip
$szPath = "C:\Program Files\7-Zip\7z.exe"
if (-not (Test-Path $szPath)) {
    # Fallback search
    $szPath = (Get-Command "7z.exe" -ErrorAction SilentlyContinue).Source
}

if (-not $szPath) {
    Write-Error "[-] 7-Zip (7z.exe) not found. Please install 7-Zip or make sure it is in Program Files."
    exit 1
}

# 5. Create password-protected ZIP archive
Write-Host "[*] Creating password-protected ZIP archive (release.zip)..." -ForegroundColor Cyan
& $szPath a -p"$Password" -tzip -mem=AES256 "release.zip" "locker.exe" "unlocker.exe"
if ($LASTEXITCODE -ne 0) {
    Write-Error "[-] Archiving failed."
    exit 1
}

# 6. Clean up loose executables
Write-Host "[*] Cleaning up temporary executables..." -ForegroundColor Cyan
Remove-Item "locker.exe" -Force
Remove-Item "unlocker.exe" -Force

Write-Host "[++] Success! 'release.zip' created and encrypted with AES-256." -ForegroundColor Green
