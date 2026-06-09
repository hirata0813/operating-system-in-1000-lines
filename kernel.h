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
#define SSTATUS_SUM  (1 << 18) // sstatus レジスタの SUM ビット．SUM ビットが立っていない場合，S-Mode プログラムは U-Mode のページにアクセスできない

// ユーザランドのアプリケーションは，必ず0x01000000から展開するようにする
#define USER_BASE 0x1000000

#define SSTATUS_SPIE (1 << 5)

// プロトタイプ宣言
void map_page(uint32_t *table1, uint32_t vaddr, paddr_t paddr, uint32_t flags);
void handle_syscall(struct trap_frame *f);
void yield(void);
void virtio_blk_init(void);
struct virtio_virtq *virtq_init(unsigned index);
void virtq_kick(struct virtio_virtq *vq, int desc_index);
void fs_init(void);
void fs_flush(void);

// 例外の識別子(例外ハンドラの実装で利用)
#define SCAUSE_ECALL 8 // Environment call from U-mode

// virtio 用の雑多な定義
#define SECTOR_SIZE       512
#define VIRTQ_ENTRY_NUM   16
#define VIRTIO_DEVICE_BLK 2
#define VIRTIO_BLK_PADDR  0x10001000
#define VIRTIO_REG_MAGIC         0x00
#define VIRTIO_REG_VERSION       0x04
#define VIRTIO_REG_DEVICE_ID     0x08
#define VIRTIO_REG_PAGE_SIZE     0x28
#define VIRTIO_REG_QUEUE_SEL     0x30
#define VIRTIO_REG_QUEUE_NUM_MAX 0x34
#define VIRTIO_REG_QUEUE_NUM     0x38
#define VIRTIO_REG_QUEUE_PFN     0x40
#define VIRTIO_REG_QUEUE_READY   0x44
#define VIRTIO_REG_QUEUE_NOTIFY  0x50
#define VIRTIO_REG_DEVICE_STATUS 0x70
#define VIRTIO_REG_DEVICE_CONFIG 0x100
#define VIRTIO_STATUS_ACK       1
#define VIRTIO_STATUS_DRIVER    2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTQ_DESC_F_NEXT          1
#define VIRTQ_DESC_F_WRITE         2
#define VIRTQ_AVAIL_F_NO_INTERRUPT 1
#define VIRTIO_BLK_T_IN  0
#define VIRTIO_BLK_T_OUT 1

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t index;
    uint16_t ring[VIRTQ_ENTRY_NUM];
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t index;
    struct virtq_used_elem ring[VIRTQ_ENTRY_NUM];
} __attribute__((packed));

struct virtio_virtq {
    struct virtq_desc descs[VIRTQ_ENTRY_NUM];
    struct virtq_avail avail;
    struct virtq_used used __attribute__((aligned(PAGE_SIZE)));
    int queue_index;
    volatile uint16_t *used_index;
    uint16_t last_used_index;
} __attribute__((packed));

struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
    uint8_t data[512];
    uint8_t status;
} __attribute__((packed));

// virtio デバイスのレジスタ操作用関数
uint32_t virtio_reg_read32(unsigned offset);
uint64_t virtio_reg_read64(unsigned offset);
void virtio_reg_write32(unsigned offset, uint32_t value);
void virtio_reg_fetch_and_or32(unsigned offset, uint32_t value);

// ファイルシステム関連の定義
#define FILES_MAX      2
#define DISK_MAX_SIZE  align_up(sizeof(struct file) * FILES_MAX, SECTOR_SIZE)

struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char type;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
    char data[];      // ヘッダに続くデータ領域を指す配列 (フレキシブル配列メンバ)
} __attribute__((packed));

struct file {
    bool in_use;      // このファイルエントリが使われているか
    char name[100];   // ファイル名
    char data[1024];  // ファイルの内容
    size_t size;      // ファイルサイズ
};
