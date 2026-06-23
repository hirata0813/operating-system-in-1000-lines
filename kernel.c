#include "kernel.h"
#include "common.h"

typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef uint32_t size_t;

extern char __bss[], __bss_end[], __stack_top[]; // リンカスクリプトで定義されているシンボル
extern char __free_ram[], __free_ram_end[];
extern char _binary_shell_bin_start[], _binary_shell_bin_size[]; // shell.bin.o に入っているシンボル

struct process *current_proc; // 現在実行中のプロセス
struct process *idle_proc;    // アイドルプロセス

// disk[]: ディスクイメージをそのまま展開するための変数
// files[]: disk[] の中身を構造化して区切った配列
struct file files[FILES_MAX];
uint8_t disk[DISK_MAX_SIZE];

// virtio デバイスの MMIO 上レジスタを操作する関数群
uint32_t virtio_reg_read32(unsigned offset) {
    return *((volatile uint32_t *) (VIRTIO_BLK_PADDR + offset));
}

uint64_t virtio_reg_read64(unsigned offset) {
    return *((volatile uint64_t *) (VIRTIO_BLK_PADDR + offset));
}

void virtio_reg_write32(unsigned offset, uint32_t value) {
    *((volatile uint32_t *) (VIRTIO_BLK_PADDR + offset)) = value;
}

void virtio_reg_fetch_and_or32(unsigned offset, uint32_t value) {
    virtio_reg_write32(offset, virtio_reg_read32(offset) | value);
}

struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4,
                       long arg5, long fid, long eid) {
    register long a0 __asm__("a0") = arg0; // 指定したレジスタに値を入れる命令
    register long a1 __asm__("a1") = arg1;
    register long a2 __asm__("a2") = arg2;
    register long a3 __asm__("a3") = arg3;
    register long a4 __asm__("a4") = arg4;
    register long a5 __asm__("a5") = arg5;
    register long a6 __asm__("a6") = fid; // SBI の仕様として，a6 レジスタで，SBI のどの関数を利用するかを決める
    register long a7 __asm__("a7") = eid; // SBI の仕様として，a7 レジスタで，SBI のどの拡張機能を利用するかを決める(選んだ拡張機能の中で，さらに a6 で関数を選ぶ)

    // ecall を呼び出すと，CPU の動作モードが s-mode から m-mode に切り替わり，OpenSBI の処理ハンドラが呼び出される
    // OpenSBI の処理が終わると，自動的に m-mode から s-mode に切り替わり，ecall の次の命令から処理が再開される
    __asm__ __volatile__("ecall"
                         : "=r"(a0), "=r"(a1)
                         : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5),
                           "r"(a6), "r"(a7)
                         : "memory");
    return (struct sbiret){.error = a0, .value = a1};
}

void putchar(char ch) {
    sbi_call(ch, 0, 0, 0, 0, 0, 0, 1 /* Console Putchar */);
}

// getchar は1文字読んだら返る
// 文字が入力されない場合も即座に返る
long getchar(void) {
    struct sbiret ret = sbi_call(0, 0, 0, 0, 0, 0, 0, 2);
    return ret.error;
}

// kernel_entry 関数の先頭アドレスを，stvec レジスタ(例外ハンドラのアドレスを示すレジスタ)に後ほどセットする
// 最初に，sscratch レジスタ(カーネルが自由に使えるレジスタ)を用いて，例外発生時のスタックポインタを保存しておく(ユーザランドプログラムなどが使っていたローカル変数などの状態を保存しておくため)
// プロセスごとにカーネルスタックを持っているため，例外発生時には，実行中プロセスのカーネルスタックを sscratch レジスタから取り出して，sp レジスタにセットする必要がある
// pc などの保存は，RISC-V では CPU が自動でやってくれるが，スタックの保存は自分でやる必要がある

/* 8章と10章の例外ハンドラの実装の違い
    8章
        sp: 例外発生時のスタック(そのままずらして退避領域に使う) 
        sscratch: sp の保存場所(ここが安全な領域だと信じている)
    10章
        最初に，sp と sscratch を入れ替えている．入れ替え後は，以下の役目を担う
        sp: 実行中プロセスのカーネルスタックのアドレス(プロセス生成時に用意された安全領域)
        sscratch: 例外発生時のスタック
    一言で言うと，例外発生時のレジスタ保存領域を，安全な場所にした，という違いがある
*/

__attribute__((naked))
__attribute__((aligned(4)))
void kernel_entry(void) {
    __asm__ __volatile__(
        // sscratch: 例外発生時のスタックポインタが入る
        // sp: 実行中プロセスのカーネルスタックのアドレスが入る
        "csrrw sp, sscratch, sp\n"

        "addi sp, sp, -4 * 31\n"
        "sw ra,  4 * 0(sp)\n"
        "sw gp,  4 * 1(sp)\n"
        "sw tp,  4 * 2(sp)\n"
        "sw t0,  4 * 3(sp)\n"
        "sw t1,  4 * 4(sp)\n"
        "sw t2,  4 * 5(sp)\n"
        "sw t3,  4 * 6(sp)\n"
        "sw t4,  4 * 7(sp)\n"
        "sw t5,  4 * 8(sp)\n"
        "sw t6,  4 * 9(sp)\n"
        "sw a0,  4 * 10(sp)\n"
        "sw a1,  4 * 11(sp)\n"
        "sw a2,  4 * 12(sp)\n"
        "sw a3,  4 * 13(sp)\n"
        "sw a4,  4 * 14(sp)\n"
        "sw a5,  4 * 15(sp)\n"
        "sw a6,  4 * 16(sp)\n"
        "sw a7,  4 * 17(sp)\n"
        "sw s0,  4 * 18(sp)\n"
        "sw s1,  4 * 19(sp)\n"
        "sw s2,  4 * 20(sp)\n"
        "sw s3,  4 * 21(sp)\n"
        "sw s4,  4 * 22(sp)\n"
        "sw s5,  4 * 23(sp)\n"
        "sw s6,  4 * 24(sp)\n"
        "sw s7,  4 * 25(sp)\n"
        "sw s8,  4 * 26(sp)\n"
        "sw s9,  4 * 27(sp)\n"
        "sw s10, 4 * 28(sp)\n"
        "sw s11, 4 * 29(sp)\n"

        // sscratch は後に別用途で使うので，元の sp をスタックに保存する
        "csrr a0, sscratch\n"
        "sw a0, 4 * 30(sp)\n"

        // カーネルスタックを設定し直す
        "addi a0, sp, 4 * 31\n"
        "csrw sscratch, a0\n"

        // a0 レジスタに，trap_frame 構造体(ここでは，上の行で保存しておいた各レジスタの状態)の先頭アドレスを代入して，handle_trap 関数を呼び出す
        "mv a0, sp\n"
        "call handle_trap\n"

        "lw ra,  4 * 0(sp)\n"
        "lw gp,  4 * 1(sp)\n"
        "lw tp,  4 * 2(sp)\n"
        "lw t0,  4 * 3(sp)\n"
        "lw t1,  4 * 4(sp)\n"
        "lw t2,  4 * 5(sp)\n"
        "lw t3,  4 * 6(sp)\n"
        "lw t4,  4 * 7(sp)\n"
        "lw t5,  4 * 8(sp)\n"
        "lw t6,  4 * 9(sp)\n"
        "lw a0,  4 * 10(sp)\n"
        "lw a1,  4 * 11(sp)\n"
        "lw a2,  4 * 12(sp)\n"
        "lw a3,  4 * 13(sp)\n"
        "lw a4,  4 * 14(sp)\n"
        "lw a5,  4 * 15(sp)\n"
        "lw a6,  4 * 16(sp)\n"
        "lw a7,  4 * 17(sp)\n"
        "lw s0,  4 * 18(sp)\n"
        "lw s1,  4 * 19(sp)\n"
        "lw s2,  4 * 20(sp)\n"
        "lw s3,  4 * 21(sp)\n"
        "lw s4,  4 * 22(sp)\n"
        "lw s5,  4 * 23(sp)\n"
        "lw s6,  4 * 24(sp)\n"
        "lw s7,  4 * 25(sp)\n"
        "lw s8,  4 * 26(sp)\n"
        "lw s9,  4 * 27(sp)\n"
        "lw s10, 4 * 28(sp)\n"
        "lw s11, 4 * 29(sp)\n"
        "lw sp,  4 * 30(sp)\n"
        "sret\n"
    );
}

// files[](disk[] の中身をファイルごとのエントリに区切った配列) を順番に走査し，ファイル名が一致するエントリを探す
struct file *fs_lookup(const char *filename) {
    for (int i = 0; i < FILES_MAX; i++) {
        struct file *file = &files[i];
        if (!strcmp(file->name, filename))
            return file;
    }

    return NULL;
}

void handle_trap(struct trap_frame *f) {

    // READ_CSR()マクロに引数を渡す時，引数は文字列として展開される．例えば，READ_CSR(scause) と書くと，マクロの中で #reg として "scause" という文字列が展開される
    uint32_t scause = READ_CSR(scause);
    uint32_t stval = READ_CSR(stval);
    uint32_t user_pc = READ_CSR(sepc);

    // scause レジスタに，例外の種類が格納される
    if (scause == SCAUSE_ECALL) {
        handle_syscall(f);
        user_pc += 4;
    } else {
        PANIC("unexpected trap scause=%x, stval=%x, sepc=%x\n", scause, stval, user_pc);
    }

    WRITE_CSR(sepc, user_pc);
}

void handle_syscall(struct trap_frame *f) {
    switch (f->a3) {
        case SYS_EXIT:
            printf("process %d exited\n", current_proc->pid);
            current_proc->state = PROC_EXITED;
            // TODO: ページテーブルの開放
            yield();
            PANIC("unreachable"); // exit したプロセスはここには来ない(はず)が，メモリ上には展開されたまま
        case SYS_GETCHAR:
            // ここの while は，1文字入力されたら break する
            while (1) {
                // getchar は，文字が入力されようがされまいが即座に返る
                long ch = getchar();
                if (ch >= 0) {
                    f->a0 = ch;
                    break;
                }

                yield();
            }
            break;
        case SYS_PUTCHAR:
            putchar(f->a0);
            break;
        case SYS_READFILE:
        case SYS_WRITEFILE: {
            const char *filename = (const char *) f->a0;
            char *buf = (char *) f->a1;
            int len = f->a2;
            struct file *file = fs_lookup(filename);
            if (!file) {
                printf("file not found: %s\n", filename);
                f->a0 = -1;
                break;
            }

            if (len > (int) sizeof(file->data))
                len = file->size;

            if (f->a3 == SYS_WRITEFILE) {
                memcpy(file->data, buf, len);
                file->size = len;
                fs_flush();
            } else {
                memcpy(buf, file->data, len);
            }

            f->a0 = len;
            break;
        }
        default:
            PANIC("unexpected syscall a3=%x\n", f->a3);
    }
}

// n ページ分のメモリを動的に割り当て，その先頭アドレスを返す
// この関数は，ページ単位でメモリを割り当てる．1ページは 4096 バイト

// カーネルのグローバル変数
struct free_page {
    struct free_page *next;
};

// フリーリストの先頭を指すポインタ
static struct free_page *free_list = NULL;
// free_list の中身は，フリーリストの第1要素があるアドレスで，free_list自体のアドレスもグローバルに利用できるので，どこからでもフリーリストを利用できる
// free_list 自体は，リストの1要素ではなくただのポインタ変数(間違えやすいので注意)
// free_list -> next ( (*free_list).next の糖衣構文) は，第二要素のアドレス(つまり，第一要素が持つ next メンバ)を表す
// つまり，free_list == NULL は，フリーリストが空であることを意味し，free_list -> next == NULL は，フリーリストに1つしか要素がないことを意味する

void dump_free_list(void) {
    printf("free_list dump:\n");
    if (free_list == NULL) {
        printf("  (empty)\n");
        return;
    }
    struct free_page *cur = free_list;
    int index = 0;
    while (cur != NULL) {
        printf("  [%d] addr: %x, next: %x\n",
               index,
               (uint32_t) cur,
               (uint32_t) cur->next);
        cur = cur->next;
        index++;
    }
    printf("\n");
}

// n ページ連続確保（n=1 の単ページ確保を基本とする）
paddr_t alloc_pages(uint32_t n) {
    static paddr_t next_paddr = (paddr_t) __free_ram; // 未使用領域の先頭アドレスを指す変数．関数呼び出し間で値が保持される
    // 単ページならフリーリストから取得を試みる
    if (n == 1 && free_list != NULL) {
        printf("Starting free list allocation:\n");
        struct free_page *page = free_list; // フリーリストの先頭1要素をページとして割り当てる
        free_list = free_list->next; // それまで第二要素だったものをフリーリストの先頭にする
        memset((void *) page, 0, PAGE_SIZE);
        printf("  > allocated page address: %x\n", (uint32_t) page);
        printf("  > next_paddr: %x\n", next_paddr);
        dump_free_list();
        return (paddr_t) page;
    }

    // フリーリストにない場合はバンプアロケータで確保
    printf("Starting n = %d bump allocation:\n", n);
    printf("  > prev next_paddr: %x\n", next_paddr);
    paddr_t paddr = next_paddr;
    next_paddr += n * PAGE_SIZE;
    if (next_paddr > (paddr_t) __free_ram_end)
        PANIC("out of memory");
    printf("  > next next_paddr: %x\n", next_paddr);
    printf("\n");
    memset((void *) paddr, 0, n * PAGE_SIZE);
    return paddr;
}

// n ページ解放（ページの中身にポインタを書き込んでリストへ）
void free_pages(paddr_t paddr, uint32_t n) {
    // 1ページずつフリーリストに追加していく
    printf("Starting n = %d freeing pages\n", n);
    printf("\n");
    for (uint32_t i = 0; i < n; i++) {
        printf("  > freeing page address: %x, i = %d\n", paddr, i);
        paddr_t p = paddr + i * PAGE_SIZE; // ページの先頭アドレスを計算
        struct free_page *page = (struct free_page *) p; // ページの先頭アドレスを，free_page* 型として無理やり解釈
                                                         // ここでキャストしているのは，そのようにすると，next メンバへのアクセスが可能になるから
                                                         // 構造体ポインタ型は，構造体の実体そのものではないため，構造体が持つメンバへのアクセスはできないように思える
                                                         // しかし，構造体ポインタ型にすることで，そのアドレスから始まる領域を「メモリレイアウトが構造体の実体通りになっていると解釈しろ」とできる
                                                         // 構造体は，メンバの並び順に従い連続してデータが置かれたメモリ配置となっている
                                                         // そのため，各メンバは，ベースアドレス+オフセットでアクセスできる
                                                         // この下にある page->next も，ベースアドレス+オフセット(この場合，メンバがnextだけなのでベースアドレスそのもの)の位置には next メンバがあると解釈し，まるで構造体メンバにアクセスするかのように書ける
        page->next = free_list; // page というアドレスから始まる4096バイトのページのうち，先頭4バイト(つまり，next メンバ領域)に free_list の値(つまり，それまでの先頭要素)を格納
        free_list = page; // free_list の値を page に更新し，page をフリーリストの先頭にする．このようにすることで，page がフリーリストの先頭に割り込む形で登録される
        dump_free_list();
    }
}

void delay(void) {
    for (int i = 0; i < 30000000; i++)
        __asm__ __volatile__("nop"); // 何もしない命令
}


struct process procs[PROCS_MAX];

// コンテキストスイッチを行う．
// prev_sp: 切り替え前プロセスの sp
// next_sp: 次に動作するプロセスの sp
__attribute__((naked)) void switch_context(uint32_t *prev_sp,
                                           uint32_t *next_sp) {
    __asm__ __volatile__(
        // 実行中プロセスのスタックへレジスタを保存
        // 実際には，CPU のレジスタは全プロセスで共通なので，実行プロセスの切替時には，実行コンテキストをメモリに退避させておく必要がある
        // AI によると，RISC-V では汎用レジスタが32本あるが，その呼び出し規約的には，caller-save(呼び出し側が保存すべきレジスタ) レジスタが13本なので，13本だけ保存するようにコンテキストスイッチしている
        "addi sp, sp, -13 * 4\n" // ここで $sp が指しているのは，switch_context() を呼び出したプロセスのスタック
        "sw ra,  0  * 4(sp)\n"
        "sw s0,  1  * 4(sp)\n"
        "sw s1,  2  * 4(sp)\n"
        "sw s2,  3  * 4(sp)\n"
        "sw s3,  4  * 4(sp)\n"
        "sw s4,  5  * 4(sp)\n"
        "sw s5,  6  * 4(sp)\n"
        "sw s6,  7  * 4(sp)\n"
        "sw s7,  8  * 4(sp)\n"
        "sw s8,  9  * 4(sp)\n"
        "sw s9,  10 * 4(sp)\n"
        "sw s10, 11 * 4(sp)\n"
        "sw s11, 12 * 4(sp)\n"

        // スタックポインタの更新&切り替え(これが実質的なプロセス切り替え)
        // a0 は prev_sp，a1 は next_sp が入っている
        "sw sp, (a0)\n" // *prev_sp = sp と同じ．現在のプロセスのスタック位置を保存する．prev_sp はアドレスとして与えられているので，ここでプロセス構造体の sp メンバの値が更新される
        "lw sp, (a1)\n" // sp = *next_sp と同じ．次のプロセスのスタック位置を読み込む

        // 次のプロセスのスタックからレジスタを復元
        "lw ra,  0  * 4(sp)\n"
        "lw s0,  1  * 4(sp)\n"
        "lw s1,  2  * 4(sp)\n"
        "lw s2,  3  * 4(sp)\n"
        "lw s3,  4  * 4(sp)\n"
        "lw s4,  5  * 4(sp)\n"
        "lw s5,  6  * 4(sp)\n"
        "lw s6,  7  * 4(sp)\n"
        "lw s7,  8  * 4(sp)\n"
        "lw s8,  9  * 4(sp)\n"
        "lw s9,  10 * 4(sp)\n"
        "lw s10, 11 * 4(sp)\n"
        "lw s11, 12 * 4(sp)\n"
        "addi sp, sp, 13 * 4\n"
        //"sw sp, (a1)\n" // ここで sp を保存すると，より最新のスタック状態を保持する実装となる
        "ret\n" // j ra と同じ．次のプロセスのスタックから復元した ra レジスタの値にジャンプすることになる
    );
}

extern char __kernel_base[];

// sret 命令(1つ下の権限レベルのモードに遷移する命令)でユーザモードに移行してユーザプログラムを実行
// sret 命令実行時は，sepc レジスタの値が pc にセットされ，モード移行後はそこから実行される
__attribute__((naked)) void user_entry(void) {
    __asm__ __volatile__(
        "csrw sepc, %[sepc]\n"
        "csrw sstatus, %[sstatus]\n"
        "sret\n"
        :
        : [sepc] "r" (USER_BASE),
          [sstatus] "r" (SSTATUS_SPIE | SSTATUS_SUM)
    );
}

// 実行イメージへのポインタとイメージサイズを受け取り，プロセスの初期化処理を行う．
struct process *create_process(const void *image, size_t image_size) {
    // 空いているプロセス管理構造体を探す
    struct process *proc = NULL;
    int i;
    for (i = 0; i < PROCS_MAX; i++) {
        if (procs[i].state == PROC_UNUSED) {
            proc = &procs[i];
            break;
        }
    }

    if (!proc)
        PANIC("no free process slots");

    // switch_context() で復帰できるように、スタックに呼び出し先保存レジスタを積む
    // プロセス管理構造体の stack には，レジスタの途中状態やプロセス内で利用するローカル変数，関数呼び出し時の引数の情報など格納
    // 各プロセスが持つ sp の初期値は，stack の先頭からではなく，stack の最後からになる．なぜなら，スタックは，低アドレスに向かって伸びていくから
    uint32_t *sp = (uint32_t *) &proc->stack[sizeof(proc->stack)];
    *--sp = 0;                      // s11
    *--sp = 0;                      // s10
    *--sp = 0;                      // s9
    *--sp = 0;                      // s8
    *--sp = 0;                      // s7
    *--sp = 0;                      // s6
    *--sp = 0;                      // s5
    *--sp = 0;                      // s4
    *--sp = 0;                      // s3
    *--sp = 0;                      // s2
    *--sp = 0;                      // s1
    *--sp = 0;                      // s0
    *--sp = (uint32_t) user_entry;  // ra

    uint32_t *page_table = (uint32_t *) alloc_pages(1);

    // カーネルのページを各プロセスのページテーブルにもマッピングする
    // この理由は，例外発生時などには，ユーザモードプロセスのページテーブルを用いてカーネルのコードにアクセスする必要があるから
    for (paddr_t paddr = (paddr_t) __kernel_base; paddr < (paddr_t) __free_ram_end; paddr += PAGE_SIZE)
        map_page(page_table, paddr, paddr, PAGE_R | PAGE_W | PAGE_X);

    // virtio ブロックデバイスの MMIO 領域をマッピング
    map_page(page_table, VIRTIO_BLK_PADDR, VIRTIO_BLK_PADDR, PAGE_R | PAGE_W);

    // ページテーブルに，アプリの実行バイナリの仮想->物理の対応を登録する
    // image_size が3ページ分であれば，以下のようになる
    // エントリ1: 仮想 0x1000000 → 物理 0x80265000
    // エントリ2: 仮想 0x1001000 → 物理 0x80266000
    // エントリ3: 仮想 0x1002000 → 物理 0x80267000
    // 先頭アドレスさえ対応付けておけば，あとは，CPU が順番に命令を拾って実行していってくれる
    for (uint32_t off = 0; off < image_size; off += PAGE_SIZE) {
        paddr_t page = alloc_pages(1);

        // コピーするデータがページサイズより小さい場合を考慮
        // https://github.com/nuta/operating-system-in-1000-lines/pull/27
        size_t remaining = image_size - off;
        size_t copy_size = PAGE_SIZE <= remaining ? PAGE_SIZE : remaining;

        // 確保した物理ページに，実行イメージ(バイナリ)をコピー
        memcpy((void *) page, image + off, copy_size);

        // ページテーブルに，仮想アドレスと物理アドレスの対応をマッピング
        // 物理アドレスは，上段で確保した物理ページのアドレス
        map_page(page_table, USER_BASE + off, page,
                 PAGE_U | PAGE_R | PAGE_W | PAGE_X);
    }
    // 各フィールドを初期化
    proc->pid = i + 1;
    proc->state = PROC_RUNNABLE;
    proc->sp = (uint32_t) sp;
    proc->page_table = page_table;
    return proc;
}

void yield(void) {
    // 実行可能なプロセスを探す
    struct process *next = idle_proc;
    for (int i = 0; i < PROCS_MAX; i++) {
        // 現在実行中のプロセスの次のプロセスから順番に、実行可能なプロセスを探す。見つかったら、そのプロセスを次の実行プロセスとして選ぶ
        struct process *proc = &procs[(current_proc->pid + i) % PROCS_MAX];
        if (proc->state == PROC_RUNNABLE && proc->pid > 0) {
            next = proc;
            break;
        }
    }

    // 現在実行中のプロセス以外に、実行可能なプロセスがない。戻って処理を続行する
    if (next == current_proc)
        return;

    // 次に動かすプロセスのカーネルスタックの初期値を，sscratch レジスタに設定
    // satp レジスタは，「どのページテーブルを使うか」を CPU に伝えるレジスタ
    // また，Sv32 モードでページングを行うことを示すレジスタでもある
    __asm__ __volatile__(
        "sfence.vma\n"
        "csrw satp, %[satp]\n"
        "sfence.vma\n"
        "csrw sscratch, %[sscratch]\n"
        :
        // ページテーブルの切り替え
        : [satp] "r" (SATP_SV32 | ((uint32_t) next->page_table / PAGE_SIZE)),
          [sscratch] "r" ((uint32_t) &next->stack[sizeof(next->stack)])
    );

    // コンテキストスイッチ
    // 現在動いているプロセスを prev，次に動かすプロセスを next として，switch_context() を呼び出す
    struct process *prev = current_proc;
    current_proc = next;
    switch_context(&prev->sp, &next->sp);
}

// 1段目のページテーブル(table1)，マップしたい仮想アドレス(vaddr)，マップ先の物理アドレス(paddr)，ページテーブルエントリに設定するフラグ(flags)を受け取り，ページテーブルを構築する
// vaddr と paddr の対応を1エントリ登録する
// table1[vpn1] は，第2レベルテーブルのPPN(物理メモリ全体を4KBで区切ったときに，何番目のページになるか)と，第2レベルテーブルが作成されているかどうかを表すビット(0bit目)が入っている．
void map_page(uint32_t *table1, uint32_t vaddr, paddr_t paddr, uint32_t flags) {
    // ページサイズが 4KB なので，vaddr，paddr ともに 4KB 単位である必要がある
    if (!is_aligned(vaddr, PAGE_SIZE))
        PANIC("unaligned vaddr %x", vaddr);

    if (!is_aligned(paddr, PAGE_SIZE))
        PANIC("unaligned paddr %x", paddr);

    // まず，第2レベルテーブルという空の入れ物を用意して，table1[vpn1] からそこへの道案内を登録
    uint32_t vpn1 = (vaddr >> 22) & 0x3ff;
    if ((table1[vpn1] & PAGE_V) == 0) { // table1[vpn1]の0bit目を見て，第2レベルテーブルが存在するか確認
        uint32_t pt_paddr = alloc_pages(1);
        table1[vpn1] = ((pt_paddr / PAGE_SIZE) << 10) | PAGE_V; // 10bit 目以降に PPN をセットして，フラグを立てる
    }

    // 次に，第2レベルテーブルに，中身(仮想->物理の対応エントリ)を追加
    uint32_t vpn0 = (vaddr >> 12) & 0x3ff; // vaddr の 12～21bitを取り出す
    uint32_t *table0 = (uint32_t *) ((table1[vpn1] >> 10) * PAGE_SIZE); // table1[vpn1]から PPN を取り出して，第2レベルテーブルの先頭アドレスに変換する
    table0[vpn0] = ((paddr / PAGE_SIZE) << 10) | flags | PAGE_V; // table0[vpn0]の10bit目以降に PPN をセット，下位10bitにフラグを詰め込む

    // 中盤のPPNと最後のPPNは意味が異なる．中盤の方は，第2レベルテーブルを指すページ番号で，最後の方は，物理アドレスに変換するためのPPN
}

struct virtio_virtq *blk_request_vq;
struct virtio_blk_req *blk_req;
paddr_t blk_req_paddr;
uint64_t blk_capacity;

void virtio_blk_init(void) {
    if (virtio_reg_read32(VIRTIO_REG_MAGIC) != 0x74726976)
        PANIC("virtio: invalid magic value");
    if (virtio_reg_read32(VIRTIO_REG_VERSION) != 1)
        PANIC("virtio: invalid version");
    if (virtio_reg_read32(VIRTIO_REG_DEVICE_ID) != VIRTIO_DEVICE_BLK)
        PANIC("virtio: invalid device id");

    // 1. デバイスをリセット
    virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, 0);
    // 2. ACKNOWLEDGEステータスビットを設定: デバイスを認識した
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACK);
    // 3. DRIVERステータスビットを設定: デバイスの使い方を知っている
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER);
    // ページサイズを設定: 4KBページを使用。PFN (ページフレーム番号) の計算に使われる
    virtio_reg_write32(VIRTIO_REG_PAGE_SIZE, PAGE_SIZE);
    // ディスク読み書き用のキューを初期化
    blk_request_vq = virtq_init(0);
    // 6. DRIVER_OKステータスビットを設定: デバイスが使用可能になった
    virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER_OK);

    // ディスクの容量を取得
    blk_capacity = virtio_reg_read64(VIRTIO_REG_DEVICE_CONFIG + 0) * SECTOR_SIZE;
    printf("virtio-blk: capacity is %d bytes\n", (int)blk_capacity);

    // デバイスへの処理要求を格納する領域を確保
    blk_req_paddr = alloc_pages(align_up(sizeof(*blk_req), PAGE_SIZE) / PAGE_SIZE);
    blk_req = (struct virtio_blk_req *) blk_req_paddr;
}

struct virtio_virtq *virtq_init(unsigned index) {
    paddr_t virtq_paddr = alloc_pages(align_up(sizeof(struct virtio_virtq), PAGE_SIZE) / PAGE_SIZE);
    struct virtio_virtq *vq = (struct virtio_virtq *) virtq_paddr;
    vq->queue_index = index;
    vq->used_index = (volatile uint16_t *) &vq->used.index;
    // キューを選択: virtqueueのインデックスを書き込む (最初のキューは0)
    virtio_reg_write32(VIRTIO_REG_QUEUE_SEL, index);
    // キューサイズを指定: 使用するディスクリプタの数を書き込む
    virtio_reg_write32(VIRTIO_REG_QUEUE_NUM, VIRTQ_ENTRY_NUM);
    // キューのページフレーム番号 (物理アドレスではない!) を書き込む
    virtio_reg_write32(VIRTIO_REG_QUEUE_PFN, virtq_paddr / PAGE_SIZE);
    return vq;
}

// デバイスに新しいリクエストがあることを通知する。desc_indexは、新しいリクエストの
// 先頭ディスクリプタのインデックス。
void virtq_kick(struct virtio_virtq *vq, int desc_index) {
    vq->avail.ring[vq->avail.index % VIRTQ_ENTRY_NUM] = desc_index;
    vq->avail.index++;
    __sync_synchronize();
    virtio_reg_write32(VIRTIO_REG_QUEUE_NOTIFY, vq->queue_index);
    vq->last_used_index++;
}

// デバイスが処理中のリクエストがあるかどうかを返す。
bool virtq_is_busy(struct virtio_virtq *vq) {
    return vq->last_used_index != *vq->used_index;
}

// virtio-blkデバイスの読み書き。
// OS 側は，この関数だけ知っておけば良く，他の関数の内部実装は気にしなくて良い
// 第一引数：読み込みの場合はデータの受取先，書き込みの場合はデータの送り元
// 第二引数：読み書きするセクタ番号
// 第三引数：読み込みなら0，書き込みなら1
void read_write_disk(void *buf, unsigned sector, int is_write) {
    if (sector >= blk_capacity / SECTOR_SIZE) {
        printf("virtio: tried to read/write sector=%d, but capacity is %d\n",
              sector, blk_capacity / SECTOR_SIZE);
        return;
    }

    // virtio-blkの仕様に従って、リクエストを構築する
    blk_req->sector = sector;
    blk_req->type = is_write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    if (is_write)
        memcpy(blk_req->data, buf, SECTOR_SIZE);

    // virtqueueのディスクリプタを構築する (3つのディスクリプタを使う)
    struct virtio_virtq *vq = blk_request_vq;
    vq->descs[0].addr = blk_req_paddr;
    vq->descs[0].len = sizeof(uint32_t) * 2 + sizeof(uint64_t);
    vq->descs[0].flags = VIRTQ_DESC_F_NEXT;
    vq->descs[0].next = 1;

    vq->descs[1].addr = blk_req_paddr + offsetof(struct virtio_blk_req, data);
    vq->descs[1].len = SECTOR_SIZE;
    vq->descs[1].flags = VIRTQ_DESC_F_NEXT | (is_write ? 0 : VIRTQ_DESC_F_WRITE);
    vq->descs[1].next = 2;

    vq->descs[2].addr = blk_req_paddr + offsetof(struct virtio_blk_req, status);
    vq->descs[2].len = sizeof(uint8_t);
    vq->descs[2].flags = VIRTQ_DESC_F_WRITE;

    // デバイスに新しいリクエストがあることを通知する
    virtq_kick(vq, 0);

    // デバイス側の処理が終わるまで待つ
    while (virtq_is_busy(vq))
        ;

    // virtio-blk: 0でない値が返ってきたらエラー
    if (blk_req->status != 0) {
        printf("virtio: warn: failed to read/write sector=%d status=%d\n",
               sector, blk_req->status);
        return;
    }

    // 読み込み処理の場合は、バッファにデータをコピーする
    if (!is_write)
        memcpy(buf, blk_req->data, SECTOR_SIZE);
}

int oct2int(char *oct, int len) {
    int dec = 0;
    for (int i = 0; i < len; i++) {
        if (oct[i] < '0' || oct[i] > '7')
            break;

        dec = dec * 8 + (oct[i] - '0');
    }
    return dec;
}

void fs_init(void) {
    // ディスク(disk.tar)の中身を丸ごとメモリに読み込む
    for (unsigned sector = 0; sector < sizeof(disk) / SECTOR_SIZE; sector++)
        read_write_disk(&disk[sector * SECTOR_SIZE], sector, false);

    // tar ファイルの中身(ヘッダ->データ->ヘッダ->データ->...)を順番に解析し，files構造体へコピーする
    unsigned off = 0;
    for (int i = 0; i < FILES_MAX; i++) {
        struct tar_header *header = (struct tar_header *) &disk[off];
        if (header->name[0] == '\0')
            break;

        if (strcmp(header->magic, "ustar") != 0)
            PANIC("invalid tar header: magic=\"%s\"", header->magic);

        int filesz = oct2int(header->size, sizeof(header->size));
        struct file *file = &files[i];
        file->in_use = true;
        strcpy(file->name, header->name);
        memcpy(file->data, header->data, filesz);
        file->size = filesz;
        printf("file: %s, size=%d\n", file->name, file->size);

        off += align_up(sizeof(struct tar_header) + filesz, SECTOR_SIZE);
    }
}

void fs_flush(void) {
    // files変数の各ファイルの内容をdisk変数に書き込む
    memset(disk, 0, sizeof(disk));
    unsigned off = 0;
    for (int file_i = 0; file_i < FILES_MAX; file_i++) {
        struct file *file = &files[file_i];
        if (!file->in_use)
            continue;

        struct tar_header *header = (struct tar_header *) &disk[off];
        memset(header, 0, sizeof(*header));
        strcpy(header->name, file->name);
        strcpy(header->mode, "000644");
        strcpy(header->magic, "ustar");
        strcpy(header->version, "00");
        header->type = '0';

        // ファイルサイズを8進数文字列に変換
        int filesz = file->size;
        for (int i = sizeof(header->size); i > 0; i--) {
            header->size[i - 1] = (filesz % 8) + '0';
            filesz /= 8;
        }

        // チェックサムを計算
        int checksum = ' ' * sizeof(header->checksum);
        for (unsigned i = 0; i < sizeof(struct tar_header); i++)
            checksum += (unsigned char) disk[off + i];

        for (int i = 5; i >= 0; i--) {
            header->checksum[i] = (checksum % 8) + '0';
            checksum /= 8;
        }

        // ファイルデータをコピー
        memcpy(header->data, file->data, file->size);
        off += align_up(sizeof(struct tar_header) + file->size, SECTOR_SIZE);
    }

    // disk変数の内容をディスクに書き込む
    for (unsigned sector = 0; sector < sizeof(disk) / SECTOR_SIZE; sector++)
        read_write_disk(&disk[sector * SECTOR_SIZE], sector, true);

    printf("wrote %d bytes to disk\n", sizeof(disk));
}

void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);
    WRITE_CSR(stvec, (uint32_t) kernel_entry);

    virtio_blk_init();
    fs_init();

    //char buf[SECTOR_SIZE];
    //read_write_disk(buf, 0, false);
    //printf("first sector: %s\n", buf);

    //strcpy(buf, "hello from kernel!!!\n");
    //read_write_disk(buf, 0, true);

    printf("\n\n");
    printf("==================\n");
    printf("freeing pages test\n");
    // ページ解放処理のテストコード
    paddr_t paddr_test1 = alloc_pages(1);
    paddr_t paddr_test2 = alloc_pages(1);
    paddr_t paddr_test3 = alloc_pages(1);
    free_pages(paddr_test2, 2);
    paddr_test2 = alloc_pages(1);

    //idle_proc = create_process(NULL, 0);
    //idle_proc->pid = 0; // idle
    //current_proc = idle_proc;

    // create_process(_binary_shell_bin_start, (size_t) _binary_shell_bin_size);
    // yield();
    PANIC("switched to idle process");
}

__attribute__((section(".text.boot")))
__attribute__((naked))
void boot(void) {
    __asm__ __volatile__(
        "mv sp, %[stack_top]\n"
        "j kernel_main\n"
        :
        : [stack_top] "r" (__stack_top)
    );
}
