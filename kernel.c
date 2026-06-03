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
            yield();
            PANIC("unreachable"); // exit したプロセスはここには来ない(はず)
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
        default:
            PANIC("unexpected syscall a3=%x\n", f->a3);
    }
}

// n ページ分のメモリを動的に割り当て，その先頭アドレスを返す
// この関数は，ページ単位でメモリを割り当てる．1ページは 4096 バイト
paddr_t alloc_pages(uint32_t n) {
    static paddr_t next_paddr = (paddr_t) __free_ram; // 次に割り当てられる空き領域の先頭アドレスを指す変数．関数呼び出し間で値が保持される
    paddr_t paddr = next_paddr;
    next_paddr += n * PAGE_SIZE;

    if (next_paddr > (paddr_t) __free_ram_end)
        PANIC("out of memory");

    memset((void *) paddr, 0, n * PAGE_SIZE);
    return paddr;
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
          [sstatus] "r" (SSTATUS_SPIE)
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

void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);
    WRITE_CSR(stvec, (uint32_t) kernel_entry);

    idle_proc = create_process(NULL, 0);
    idle_proc->pid = 0; // idle
    current_proc = idle_proc;

    create_process(_binary_shell_bin_start, (size_t) _binary_shell_bin_size);
    yield();
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
