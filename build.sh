#!/bin/sh
# 编译脚本。在 Git Bash 或 MSYS2 bash 中执行: sh build.sh
# 头文件和库目录从 gcc 所在位置推出来，不再写死 C:/msys64（CI 上的 MSYS2 不在这个路径）。
set -e

if command -v gcc >/dev/null 2>&1; then
    MSYS_PREFIX="$(cd "$(dirname "$(dirname "$(command -v gcc)")")" && pwd)"
else
    MSYS_PREFIX="C:/msys64/ucrt64"
fi

gcc -O2 -Wall -Wextra -std=c11 \
    -Isrc -I"$MSYS_PREFIX/include" \
    src/main.c src/script.c src/save.c src/ui.c src/util.c \
    src/menu.c src/ai.c src/character.c src/bg.c src/sprite.c src/audio.c \
    src/chinese_font_data.c \
    -o gal.exe \
    -L"$MSYS_PREFIX/lib" -lraylib -lglfw3 -lopengl32 -lgdi32 -lwinmm -lpthread \
    -static-libgcc

echo "built: gal.exe"
