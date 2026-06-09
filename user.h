#pragma once
#include "common.h"

// プロトタイプ宣言
__attribute__((noreturn)) void exit(void);
void putchar(char ch);
int getchar(void);
int syscall(int sysno, int arg0, int arg1, int arg2);
int readfile(const char *filename, char *buf, int len);
int writefile(const char *filename, const char *buf, int len);
