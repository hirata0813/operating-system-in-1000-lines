#include "kernel.h"
#include "common.h"

typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef uint32_t size_t;

extern char __bss[], __bss_end[], __stack_top[]; // リンカスクリプトで定義されているシンボル
extern char __free_ram[], __free_ram_end[];

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

// kernel_entry 関数の先頭アドレスを，stvec レジスタ(例外ハンドラのアドレスを示すレジスタ)に後ほどセットする
// 最初に，sscratch レジスタ(カーネルが自由に使えるレジスタ)を用いて，例外発生時のスタックポインタを保存しておく(ユーザランドプログラムなどが使っていたローカル変数などの状態を保存しておくため)
// pc などの保存は，RISC-V では CPU が自動でやってくれるが，スタックの保存は自分でやる必要がある
__attribute__((naked))
__attribute__((aligned(4)))
void kernel_entry(void) {
    __asm__ __volatile__(
        "csrw sscratch, sp\n"
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

    PANIC("unexpected trap scause=%x, stval=%x, sepc=%x\n", scause, stval, user_pc);
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

// プロセスの初期化処理を行う．具体的には，実行開始アドレスを受け取り，プロセス管理構造体を初期化して返す
struct process *create_process(uint32_t pc) {
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
    printf("initializing process stack for PID:%d, stack top = %x\n", i + 1, (uint32_t) sp);
    *--sp = 0;                      // s11
    printf("initializing process stack for PID:%d, stack top = %x\n", i + 1, (uint32_t) sp);
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
    *--sp = (uint32_t) pc;          // ra

    // 各フィールドを初期化
    proc->pid = i + 1;
    proc->state = PROC_RUNNABLE;
    proc->sp = (uint32_t) sp;
    printf("created process PID:%d with stack pointer %x\n", proc->pid, proc->sp);
    return proc;
}

struct process *current_proc; // 現在実行中のプロセス
struct process *idle_proc;    // アイドルプロセス

void yield(void) {
    // 実行可能なプロセスを探す
    struct process *next = idle_proc;
    for (int i = 0; i < PROCS_MAX; i++) {
        // 現在実行中のプロセスの次のプロセスから順番に、実行可能なプロセスを探す。見つかったら、そのプロセスを次の実行プロセスとして選ぶ
        struct process *proc = &procs[(current_proc->pid + i) % PROCS_MAX];
        printf("checking process PID:%d, state=%d\n", proc->pid, proc->state);
        if (proc->state == PROC_RUNNABLE && proc->pid > 0) {
            next = proc;
            break;
        }
    }

    // 現在実行中のプロセス以外に、実行可能なプロセスがない。戻って処理を続行する
    if (next == current_proc)
        return;

    // コンテキストスイッチ
    // 現在動いているプロセスを prev，次に動かすプロセスを next として，switch_context() を呼び出す
    struct process *prev = current_proc;
    current_proc = next;
    switch_context(&prev->sp, &next->sp);
}


struct process *proc_a;
struct process *proc_b;

void proc_a_entry(void) {
    printf("starting process A. sp = %x\n", proc_a->sp);
    while (1) {
        putchar('A');
        //printf("switching from process A to B. A sp = %x, B sp = %x\n", proc_a->sp, proc_b->sp);
        //printf("A $ra = %x, B $ra = %x\n", *((uint32_t *)((char *)proc_a->sp + 4)), *((uint32_t *)((char *)proc_b->sp + 4)));
        yield();
        delay();
    }
}

void proc_b_entry(void) {
    printf("starting process B. sp = %x\n", proc_b->sp);
    while (1) {
        putchar('B');
        //printf("switching from process B to A. A sp = %x, B sp = %x\n", proc_a->sp, proc_b->sp);
        //printf("B $ra = %x, A $ra = %x\n", *((uint32_t *)((char *)proc_a->sp + 4)), *((uint32_t *)((char *)proc_b->sp + 4)));
        yield();
        delay();
    }
}

void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);
    WRITE_CSR(stvec, (uint32_t) kernel_entry);

    idle_proc = create_process((uint32_t) NULL);
    idle_proc->pid = 0; // idle
    current_proc = idle_proc;

    proc_a = create_process((uint32_t) proc_a_entry);
    proc_b = create_process((uint32_t) proc_b_entry);

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
