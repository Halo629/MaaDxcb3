@echo off
rem 打包：mxu.exe -> MaaDxcb3.exe（改名 + 换图标），双击运行即可
python package.py
if errorlevel 1 (
    echo.
    echo [失败] 打包出错，请检查上方错误信息。
    pause
)
