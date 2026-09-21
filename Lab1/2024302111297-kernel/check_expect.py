#!/usr/bin/env python3
"""check_expect.py — 学生版 expect_banner.txt 自检器(随基线包发放)

验收前自检 expect_banner.txt 格式规范是否合规，只校验输出格式、不含任何标准答案。

用法:
  python3 check_expect.py <你的学号> [expect文件路径]
  # 省略路径时默认检查当前目录的 expect_banner.txt

检查项(与验收 autograder 保持完全相同的评测标准):
  1. 文本中包含你的学号十进制(协议 1 按去点后文本判断)
  2. 文本中包含 (学号%97) 的十六进制,格式必须:0x 小写、无前导零
  3. 协议 0(sid%3==0):以换行结束
     协议 1(sid%3==1):每个字节后跟 '.',末尾亦然
     协议 2(sid%3==2):最末段含 ASCII 数字校验和(校验和为最末输出,
                       算法自定,但必须与你的内核实际输出逐字节一致)
"""
import sys


def mod97(sid: int) -> int:
    return sid % 97


def check(sid: int, text: str) -> list:
    errs = []
    p = sid % 3
    probe = text.replace(".", "") if p == 1 else text
    if str(sid) not in probe:
        errs.append(f"未包含学号十进制 {sid}(协议 1 按去点后的文本判断)")
    if f"0x{mod97(sid):x}" not in probe:
        errs.append(f"未包含 mod97 十六进制 0x{mod97(sid):x}"
                    "(注意:必须 0x 小写、无前导零)")
    if p == 0 and not text.endswith("\n"):
        errs.append("协议 0 应以换行结束")
    if p == 1 and not text.endswith("."):
        errs.append("协议 1 每个字节(含末字节)后都要跟一个 '.'")
    if p == 2:
        seg = text.rstrip("\n").split("\n")[-1]
        if not any(ch.isdigit() for ch in seg):
            errs.append("协议 2 最末段应含 ASCII 数字校验和"
                        "(校验和之后不应再有正文)")
    return errs


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    sid = int(sys.argv[1])
    path = sys.argv[2] if len(sys.argv) > 2 else "expect_banner.txt"
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        sys.exit(f"无法读取 {path}: {e}")
    errs = check(sid, data.decode("utf-8", "ignore"))
    for e in errs:
        print(f"! {e}")
    if errs:
        print(f"[FAIL] 形状校验未通过(sid={sid}, 协议{sid % 3})——"
              f"请对照上面的提示修改 expect_banner.txt 后重试")
        sys.exit(1)
    print(f"[ok] 形状校验通过(sid={sid}, 协议{sid % 3})。"
          f"注意:最终验收还会将你的内核实跑输出与此文件逐字节比对,请确保二者一致。")


if __name__ == "__main__":
    main()
