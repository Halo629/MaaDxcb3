#!/usr/bin/env python3
"""打包脚本：将 mxu.exe 重命名为 MaaDxcb3.exe 并替换图标。

参考 MaaEnd (https://github.com/MaaEnd/MaaEnd) 的做法：
  1. 复制 mxu.exe -> MaaDxcb3.exe
  2. resource/MaaDxcb3.png -> 多尺寸 ICO
  3. 用 rcedit 设置 exe 图标

用法：python package.py
"""

import os
import shutil
import subprocess
import sys
import urllib.request

APP_NAME = "MaaDxcb3"
ICON_PNG = os.path.join("resource", "MaaDxcb3.png")
ICON_ICO = f"{APP_NAME}.ico"
RCEDIT_URL = "https://github.com/electron/rcedit/releases/download/v2.0.0/rcedit-x64.exe"


def main() -> None:
    if not os.path.isfile("mxu.exe"):
        print("[错误] 未找到 mxu.exe，请先从 MXU release 下载", file=sys.stderr)
        sys.exit(1)

    print(f"[1/3] 复制并改名 mxu.exe -> {APP_NAME}.exe")
    shutil.copy("mxu.exe", f"{APP_NAME}.exe")

    print(f"[2/3] PNG 转 ICO: {ICON_PNG}")
    from PIL import Image

    im = Image.open(ICON_PNG)
    im.save(
        ICON_ICO,
        sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256), (512, 512)],
    )

    if not os.path.isfile("rcedit.exe"):
        print("[3/3] 下载 rcedit.exe")
        urllib.request.urlretrieve(RCEDIT_URL, "rcedit.exe")
    else:
        print("[3/3] 使用本地 rcedit.exe")

    print(f"替换图标: {APP_NAME}.exe --set-icon {ICON_ICO}")
    subprocess.run(["rcedit.exe", f"{APP_NAME}.exe", "--set-icon", ICON_ICO], check=True)

    os.remove(ICON_ICO)
    print(f"[完成] 生成 {APP_NAME}.exe")


if __name__ == "__main__":
    main()
