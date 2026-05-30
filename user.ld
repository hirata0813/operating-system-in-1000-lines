ENTRY(start)

/* 0x01000000〜0x01800000の領域がユーザランド用メモリ空間．カーネル空間とは完全に別*/
/* リンカスクリプトは，メモリレイアウトの順番と開始アドレスを決めるだけ．仮に text 領域がすごく膨大になったとして，rodata 領域は極端に狭くなる，みたいなことは起こらず，開始アドレスが後ろにずれるだけ．各領域のサイズはコンパイル時に決まる*/
SECTIONS {
    . = 0x1000000;

    .text :{
        KEEP(*(.text.start));
        *(.text .text.*);
    }

    .rodata : ALIGN(4) {
        *(.rodata .rodata.*);
    }

    .data : ALIGN(4) {
        *(.data .data.*);
    }

    .bss : ALIGN(4) {
        *(.bss .bss.* .sbss .sbss.*);

        . = ALIGN(16); /* https://github.com/nuta/operating-system-in-1000-lines/pull/23 */
        . += 64 * 1024; /* 64KB */
        __stack_top = .;

       ASSERT(. < 0x1800000, "too large executable");
    }
}
