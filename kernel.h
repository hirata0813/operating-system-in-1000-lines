#pragma once

struct sbiret {
    long error;
    long value;
};

// __FILE__: 現在のファイル名を表すマクロ．例えば，kernel.c で PANIC マクロを利用すると，__FILE__ は "kernel.c" になる．
// __LINE__: 現在の行番号を表すマクロ．
// __FILE__ と __LINE__ は C の標準仕様．##__VA_ARGS__ は，GCC の拡張機能
#define PANIC(fmt, ...)                                                        \
    do {                                                                       \
        printf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);  \
        while (1) {}                                                           \
    } while (0)
