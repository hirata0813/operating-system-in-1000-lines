#pragma once
#include "common.h"

// プロトタイプ宣言
__attribute__((noreturn)) void exit(void);
void putchar(char ch);
int syscall(int sysno, int arg0, int arg1, int arg2);
