#pragma once
#include "common.h"

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


// スタックに積まれている，元の実行状態の構造を表す構造体
struct trap_frame {
    uint32_t ra;
    uint32_t gp;
    uint32_t tp;
    uint32_t t0;
    uint32_t t1;
    uint32_t t2;
    uint32_t t3;
    uint32_t t4;
    uint32_t t5;
    uint32_t t6;
    uint32_t a0;
    uint32_t a1;
    uint32_t a2;
    uint32_t a3;
    uint32_t a4;
    uint32_t a5;
    uint32_t a6;
    uint32_t a7;
    uint32_t s0;
    uint32_t s1;
    uint32_t s2;
    uint32_t s3;
    uint32_t s4;
    uint32_t s5;
    uint32_t s6;
    uint32_t s7;
    uint32_t s8;
    uint32_t s9;
    uint32_t s10;
    uint32_t s11;
    uint32_t sp;
} __attribute__((packed));

#define READ_CSR(reg)                                                          \
    ({                                                                         \
        unsigned long __tmp;                                                   \
        __asm__ __volatile__("csrr %0, " #reg : "=r"(__tmp));                  \
        __tmp;                                                                 \
    })

#define WRITE_CSR(reg, value)                                                  \
    do {                                                                       \
        uint32_t __tmp = (value);                                              \
        __asm__ __volatile__("csrw " #reg ", %0" ::"r"(__tmp));                \
    } while (0)

#define PROCS_MAX 8       // 最大プロセス数

#define PROC_UNUSED   0   // 未使用のプロセス管理構造体
#define PROC_RUNNABLE 1   // 実行可能なプロセス
#define PROC_EXITED   2   // 終了したプロセス

struct process {
    int pid;             // プロセスID
    int state;           // プロセスの状態: PROC_UNUSED または PROC_RUNNABLE
    vaddr_t sp;          // コンテキストスイッチ時のスタックポインタ
    uint8_t stack[8192]; // カーネルスタック(コンテキストスイッチ時の CPU レジスタ，関数の戻り先などプロセスのコンテキストを格納)
                         //   さらに，プロセス内で利用するローカル変数や関数呼び出し時の引数(シスプロで習ったようなこと)もここに格納される
    uint32_t *page_table;
};

// 1 << n は，1をnビット左にずらすという意味で，例えば，1 << 3 は 0b00001000 = 0x08 となり，3ビット目が1となる．
#define SATP_SV32 (1u << 31) // Sv32 モードでページングを有効化することを示す
#define PAGE_V    (1 << 0)   // 有効化ビット
#define PAGE_R    (1 << 1)   // 読み込み可能
#define PAGE_W    (1 << 2)   // 書き込み可能
#define PAGE_X    (1 << 3)   // 実行可能
#define PAGE_U    (1 << 4)   // ユーザーモードでアクセス可能

// ユーザランドのアプリケーションは，必ず0x01000000から展開するようにする
#define USER_BASE 0x1000000

#define SSTATUS_SPIE (1 << 5)

// プロトタイプ宣言
void map_page(uint32_t *table1, uint32_t vaddr, paddr_t paddr, uint32_t flags);
void handle_syscall(struct trap_frame *f);
void yield(void);

// 例外の識別子(例外ハンドラの実装で利用)
#define SCAUSE_ECALL 8 // Environment call from U-mode
