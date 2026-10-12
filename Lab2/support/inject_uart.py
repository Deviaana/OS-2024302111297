#!/usr/bin/env python3
"""
inject_uart.py — 向运行中的 xv6 注入预定义串口输入(lab2 风暴测试驱动)

用法:
  python3 inject_uart.py --tree <内核树> --script storm.txt [--log out.txt]

脚本文件指令(每行一条):
  SEND hello          向串口发送一行文本(自动带回车)
  SENDRAW abc         原样发送(不加回车)
  WAIT 200            等待毫秒
  REPEAT 10 x         重复发送文本 x 共 10 次(每次间隔 20ms,风暴用)
  EXPECT regex        等待输出匹配正则(最多 10s,失败记 FAIL)
"""

import argparse
import os
import re
import selectors
import subprocess
import sys
import time

QEMU = "qemu-system-riscv64"
QEMU_ARGS = ["-machine", "virt", "-bios", "none", "-kernel", "kernel/kernel",
             "-m", "128M", "-smp", "1", "-nographic",
             "-global", "virtio-mmio.force-legacy=false",
             "-drive", "file=fs.img,if=none,format=raw,id=x0",
             "-device", "virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tree", required=True)
    ap.add_argument("--script", required=True)
    ap.add_argument("--log", default="inject_uart.log")
    args = ap.parse_args()

    with open(args.script) as f:
        cmds = [l.strip() for l in f if l.strip() and not l.startswith("#")]

    proc = subprocess.Popen([QEMU] + QEMU_ARGS, cwd=args.tree,
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, bufsize=0)
    sel = selectors.DefaultSelector()
    sel.register(proc.stdout, selectors.EVENT_READ)
    buf = ""
    log = open(args.log, "w")

    def drain():
        nonlocal buf
        for key, _ in sel.select(0):
            c = os.read(key.fd, 4096)
            if c:
                buf += c.decode("utf-8", "ignore")
                log.write(c.decode("utf-8", "ignore"))
                log.flush()

    def wait_prompt(timeout=10):
        t0 = time.time()
        while "$ " not in buf and time.time() - t0 < timeout:
            drain()
            time.sleep(0.1)
        return "$ " in buf

    ok = wait_prompt()
    print(f"boot {'ok' if ok else 'TIMEOUT'}")
    failures = 0
    for line in cmds:
        parts = line.split(None, 1)
        op = parts[0].upper()
        arg = parts[1] if len(parts) > 1 else ""
        if op == "WAIT":
            time.sleep(int(arg) / 1000)
        elif op == "SEND":
            proc.stdin.write((arg + "\n").encode())
            proc.stdin.flush()
            time.sleep(0.05)
        elif op == "SENDRAW":
            proc.stdin.write(arg.encode())
            proc.stdin.flush()
        elif op == "REPEAT":
            n, text = arg.split(None, 1)
            for _ in range(int(n)):
                proc.stdin.write((text + "\n").encode())
                proc.stdin.flush()
                time.sleep(0.02)
        elif op == "EXPECT":
            t0 = time.time()
            hit = False
            while time.time() - t0 < 10:
                drain()
                if re.search(arg, buf, re.M):
                    hit = True
                    break
                time.sleep(0.1)
            print(f"EXPECT {arg!r}: {'PASS' if hit else 'FAIL'}")
            failures += 0 if hit else 1
        drain()

    time.sleep(1)
    drain()
    try:
        proc.stdin.write(b"\x01x")
        proc.stdin.flush()
        time.sleep(1)
    except Exception:
        pass
    proc.kill()
    log.close()
    print(f"log → {args.log};EXPECT 失败 {failures} 条")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
