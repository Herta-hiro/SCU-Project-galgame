#!/bin/sh
# 编译脚本。在 Git Bash 或 MSYS2 bash 中执行: sh build.sh
set -e

MSYS_INC="C:/msys64/ucrt64/include"
MSYS_LIB="C:/msys64/ucrt64/lib"

gcc -O2 -Wall -Wextra -std=c11 \
    -Isrc -I"$MSYS_INC" \
    src/main.c src/script.c src/save.c src/ui.c src/util.c \
    src/menu.c src/ai.c src/character.c src/bg.c src/sprite.c src/chinese_font_data.c \
    -o gal.exe \
    -L"$MSYS_LIB" -lraylib -lglfw3 -lopengl32 -lgdi32 -lwinmm -lpthread \
    -static-libgcc

echo "built: gal.exe"
