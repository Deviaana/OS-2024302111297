// 课程测试 · lab5-test3: echo 与 exec 参数栈规整校验
// 断言(与 sid 无关):
//   1. argc >= 1, argv 非空且按 16 字节对齐(((unsigned long)argv & 0xfUL) == 0);
//   2. argv[argc] 哨兵指针必须为 0(NULL), 且 argv[0..argc-1] 均为有效非空字符串;
//   3. 依次输出 argv[1..argc-1](空格分隔, 末尾换行), 并打印 T5-3 PASS 校验行。
#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    if(argc < 1 || argv == 0){
        printf("T5-3 FAIL: invalid argc=%d argv=%p\n", argc, argv);
        exit(1);
    }
    if(((unsigned long)argv & 0xfUL) != 0){
        printf("T5-3 FAIL: argv=%p not 16-byte aligned\n", argv);
        exit(1);
    }
    if(argv[argc] != 0){
        printf("T5-3 FAIL: argv[%d]=%p (expect NULL sentinel)\n", argc, argv[argc]);
        exit(1);
    }
    for(int i = 0; i < argc; i++){
        if(argv[i] == 0 || argv[i][0] == '\0'){
            printf("T5-3 FAIL: argv[%d] is empty\n", i);
            exit(1);
        }
    }
    for(int i = 1; i < argc; i++){
        write(1, argv[i], strlen(argv[i]));
        if(i + 1 < argc)
            write(1, " ", 1);
        else
            write(1, "\n", 1);
    }
    if(argc == 1)
        write(1, "\n", 1);
    printf("T5-3 PASS: argc=%d argv[0]=%s aligned\n", argc, argv[0]);
    exit(0);
}
