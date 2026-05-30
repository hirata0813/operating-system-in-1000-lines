#!/bin/bash
set -xue

# QEMUのファイルパス
QEMU=qemu-system-riscv32

# clangのパス (Ubuntuの場合は CC=clang)
CC=clang

CFLAGS="-std=c11 -O2 -g3 -Wall -Wextra --target=riscv32-unknown-elf -fuse-ld=lld -fno-stack-protector -ffreestanding -nostdlib"

OBJCOPY=/usr/bin/llvm-objcopy

# シェルをビルド
$CC $CFLAGS -Wl,-Tuser.ld -Wl,-Map=shell.map -o shell.elf shell.c user.c common.c # シェルを ELF 形式でビルド
$OBJCOPY --set-section-flags .bss=alloc,contents -O binary shell.elf shell.bin # ELF を生バイナリ(ベースアドレスから実際にメモリに展開される内容)に変換
$OBJCOPY -Ibinary -Oelf32-littleriscv shell.bin shell.bin.o # 生バイナリを，シンボル付きのオブジェクトファイルに変換．シンボルは C 言語から参照できる

# カーネルをビルド
$CC $CFLAGS -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf \
    kernel.c common.c shell.bin.o

# QEMUを起動
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -kernel kernel.elf
