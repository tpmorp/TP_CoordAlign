@echo off
rem ============================================================
rem  TP_CoordAlign.aux2 ビルドスクリプト（要 Visual Studio 2022 + CMake）
rem  「x64 Native Tools Command Prompt for VS 2022」から実行すると確実。
rem ============================================================
setlocal
cd /d "%~dp0"

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 goto :err

cmake --build build --config Release
if errorlevel 1 goto :err

echo.
echo === ビルド成功 ===
echo 出力: build\Release\TP_CoordAlign.aux2
echo これを AviUtl2 の Plugin フォルダへコピーしてください。
goto :eof

:err
echo.
echo === ビルド失敗 ===
exit /b 1
