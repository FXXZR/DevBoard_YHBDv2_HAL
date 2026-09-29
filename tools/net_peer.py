#!/usr/bin/env python3
"""
W5500 对端工具 —— 板子在 PC 侧的对话者。

三个子命令, 对应 W2 的三类验证:

    server   TCP server: 等板子来连      (板子做 client 时用它)
    client   TCP client: 主动连板子       (板子做 server 时用它)
    udp      UDP echo  : 收一个回一个, 可用 --burst 主动打一串包

三种应答策略 (--mode), 用来造异常路径:

    echo    收到就回 (默认)   验证往返通路
    sink    读走但不回        板子视角是"对端只收不发"
    stall   连上就不读        接收窗口压到 0, 验证板子发不动时的行为

统计: 双向字节数 / 次数, 每秒最多打一行 (突发时不刷屏), 退出时打总账。
--burst 发出的包带 4 字节大端序号, 收到的回包会据此统计丢包与乱序。

用法:
    python tools/net_peer.py server --port 5000
    python tools/net_peer.py server --mode stall
    python tools/net_peer.py client --host 192.168.1.100 --size 2048 --count 10
    python tools/net_peer.py udp --burst 1000 --size 100

⚠️ Windows 防火墙首次运行会弹窗, 必须勾"专用网络"放行, 否则板子的包进不来。
"""

import argparse
import socket
import struct
import time

# 板子的默认地址 —— 见 Document/temp/w2-plan.md §4 的网络参数表
BOARD_IP = "192.168.1.100"
DEFAULT_PORT = 5000


class Stats:
    """双向字节/次数计数 —— 每秒最多打一行"""

    def __init__(self):
        self.rx_bytes = self.tx_bytes = 0
        self.rx_chunks = self.tx_chunks = 0
        self._next = time.monotonic() + 1.0

    def add(self, rx=0, tx=0, rx_chunks=0, tx_chunks=0):
        self.rx_bytes += rx
        self.tx_bytes += tx
        self.rx_chunks += rx_chunks
        self.tx_chunks += tx_chunks
        self.tick()

    def tick(self):
        now = time.monotonic()
        if now >= self._next:
            self._next = now + 1.0
            self.line("1s")

    def line(self, tag):
        print(f"[{tag}] rx {self.rx_bytes} B / {self.rx_chunks}"
              f"   tx {self.tx_bytes} B / {self.tx_chunks}", flush=True)


class SeqTracker:
    """按 4 字节大端序号统计丢包/乱序 —— 只对 --burst 发出的包有意义"""

    def __init__(self):
        self.next_seq = 0
        self.lost = 0
        self.out_of_order = 0

    def feed(self, data):
        if len(data) < 4:
            return None
        seq = int.from_bytes(data[:4], "big")
        if seq == self.next_seq:
            self.next_seq += 1
        elif seq > self.next_seq:
            self.lost += seq - self.next_seq
            self.next_seq = seq + 1
        else:
            self.out_of_order += 1
        return seq

    def line(self):
        return (f"seq {self.next_seq - 1}  lost {self.lost}  "
                f"out-of-order {self.out_of_order}")


def make_payload(seq, size):
    """4 字节序号 + 填充到 size"""
    body = bytes((seq + i) & 0xFF for i in range(max(size, 4) - 4))
    return seq.to_bytes(4, "big") + body


def serve_tcp(args):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

        if args.mode == "stall":
            # 把接收缓冲压到最小 —— 目的是让对端【很快】发不动。
            # 用默认的几十 KB, 要靠"对端打满 64KB"才能造出零窗口, 太慢也太依赖
            # 系统默认值。注意必须设在 listen 之前: 窗口大小在握手时就协商定了,
            # accept 之后再改对已建立的连接无效。
            srv.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2048)

        srv.bind((args.host, args.port))
        srv.listen(1)
        print(f"[tcp] listening on {args.host}:{args.port}  mode={args.mode}", flush=True)

        # 一轮连接结束后继续 accept —— 板子会反复连 (重连测试、多轮往返),
        # 只 accept 一次的话第二轮就没人监听了, 现象是"连接超时"。
        while True:
            conn, peer = srv.accept()
            print(f"[tcp] connected from {peer[0]}:{peer[1]}", flush=True)

            stats = Stats()
            with conn:
                if args.mode == "stall":
                    # 一个字节都不读 —— 接收窗口很快被填满, 板子那边的 send 会先
                    # 成功若干次然后卡住。这是"对端不消费"的唯一复现方式:
                    # 读了就不算了。
                    print("[tcp] stall: 不读本连接 (Ctrl-C 结束)", flush=True)
                    while True:
                        time.sleep(1)
                        stats.tick()
                else:
                    half_closed = False
                    try:
                        while True:
                            data = conn.recv(args.bufsize)
                            if not data:
                                print("[tcp] peer closed, 本连接统计:", end=" ", flush=True)
                                stats.line("total")
                                break
                            stats.add(rx=len(data), rx_chunks=1)

                            if args.mode == "rst":
                                # 带 RST 断开: SO_LINGER=0 让 close() 直接发 RST,
                                # 而不是走四次挥手。板子那边"对端连接被复位"只能
                                # 这么复现 —— 正常的 close 发的是 FIN, 那是另一条路径。
                                conn.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                                                struct.pack("hh", 1, 0))
                                conn.close()
                                print("[tcp] 已用 RST 断开", flush=True)
                                break

                            if args.mode == "half-close" and not half_closed:
                                # 只关写方向: 对端会收到 FIN, 但这条连接还能收数据。
                                # 板子那边应表现成 recv 报"对端已关闭"、send 仍然成功。
                                conn.shutdown(socket.SHUT_WR)
                                half_closed = True
                                print("[tcp] 已发 FIN (半关闭), 之后只读不回", flush=True)

                            if args.mode == "echo" and not half_closed:
                                conn.sendall(data)
                                stats.add(tx=len(data), tx_chunks=1)
                    except OSError as e:
                        # 板子被复位 / 拔网线时这里【必然】报错 (WinError 10053/10054)。
                        # 对端复不复位不归 PC 管, 所以这是常态而非异常 —— 让它把
                        # 整个 server 带走的话, 下次上电就没人监听了, 现象是
                        # "板子连不上", 查半天查到工具自己身上。
                        print(f"[tcp] 连接中断 ({e}), 继续监听", flush=True)


def connect_with_retry(host, port, wait_s):
    """板子要跑完前面的自检阶段才开始监听, 所以连不上要等, 不是一次就放弃"""
    deadline = time.monotonic() + wait_s
    while True:
        try:
            return socket.create_connection((host, port), timeout=5.0)
        except OSError as e:
            if time.monotonic() >= deadline:
                raise
            print(f"[tcp] 连不上 ({e}), 继续等板子...", flush=True)
            time.sleep(1)


def run_client(args):
    if args.stall:
        # 压满对端发送窗口的专用路径。
        # ⚠️ 接收缓冲必须在 connect 【之前】设: TCP 窗口大小是握手时协商的,
        # 连上之后再改对这条连接无效 —— 用默认值的话对端能推几十上百 KB
        # (Windows 还会自动调优), 根本压不满。
        deadline = time.monotonic() + args.wait
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2048)
        while True:                     # 板子还没监听时要等
            try:
                s.connect((args.host, args.port))
                break
            except OSError as e:
                if time.monotonic() >= deadline:
                    raise
                print(f"[tcp] 连不上 ({e}), 继续等板子...", flush=True)
                time.sleep(1)
        with s:
            print(f"[tcp] connected to {args.host}:{args.port}", flush=True)
            print("[tcp] stall: 收缓冲 2048, 不读不写 (Ctrl-C 结束)", flush=True)
            while True:
                time.sleep(1)
        return

    with connect_with_retry(args.host, args.port, args.wait) as s:
        print(f"[tcp] connected to {args.host}:{args.port}", flush=True)
        stats = Stats()
        payload = make_payload(0, args.size)
        got = 0

        for i in range(args.count):
            s.sendall(make_payload(i, args.size))
            stats.add(tx=args.size, tx_chunks=1)

            # 收满 size 才算一次往返 —— TCP 是流, 一次 recv 可能只到一半
            need = args.size
            while need > 0:
                data = s.recv(min(need, args.bufsize))
                if not data:
                    print("[tcp] peer closed mid-transfer", flush=True)
                    stats.line("total")
                    return
                stats.add(rx=len(data), rx_chunks=1)
                need -= len(data)
            got += 1

            if args.interval:
                time.sleep(args.interval)

        print(f"[tcp] {got} round-trips done:", end=" ", flush=True)
        stats.line("total")


def serve_udp(args):
    tracker = SeqTracker()

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((args.host, args.port))
        s.settimeout(1.0)
        print(f"[udp] listening on {args.host}:{args.port}  mode={args.mode}", flush=True)

        stats = Stats()

        if args.burst:
            target = (args.peer, args.port)
            for i in range(args.burst):
                pkt = make_payload(i, args.size)
                s.sendto(pkt, target)
                stats.add(tx=len(pkt), tx_chunks=1)
            print(f"[udp] burst {args.burst} x {args.size} B -> {target[0]}:{target[1]}",
                  flush=True)

            if args.no_recv:
                # 纯发包模式: 丢包率由板子数 (它按包头的序号统计)。
                # 不这么设计的话, 这个工具会一直等回包, 而丢包测试里
                # 板子根本不需要回 —— 脚本会挂在这里不退。
                stats.line("total")
                return

        while True:
            try:
                data, peer = s.recvfrom(args.bufsize)
            except socket.timeout:
                stats.tick()
                continue

            stats.add(rx=len(data), rx_chunks=1)

            if args.burst:
                tracker.feed(data)
                print(f"[udp] from {peer[0]}:{peer[1]}  {tracker.line()}", flush=True)

            # --burst 时【不】回包: 那一串包是我们自己发的, 回过去就成了
            # "自己发→自己回"的回环 (对端若也在回, 会变成无限的包风暴)。
            if args.mode == "echo" and not args.burst:
                s.sendto(data, peer)
                stats.add(tx=len(data), tx_chunks=1)


def main():
    parser = argparse.ArgumentParser(description="W5500 对端工具 (TCP server/client, UDP echo)")
    sub = parser.add_subparsers(dest="cmd", required=True)

    def common(p, host_default):
        p.add_argument("--host", default=host_default, help="绑定/连接的地址")
        p.add_argument("--port", type=int, default=DEFAULT_PORT)
        p.add_argument("--bufsize", type=int, default=4096, help="单次 recv 的缓冲大小")
        p.add_argument("--mode",
                       choices=("echo", "sink", "stall", "rst", "half-close"),
                       default="echo",
                       help="echo=收到就回, sink=读走不回, stall=不读, "
                            "rst=收到数据就用 RST 断开, half-close=收到数据后只关写方向")

    p = sub.add_parser("server", help="TCP server, 等板子来连")
    common(p, "0.0.0.0")

    p = sub.add_parser("client", help="TCP client, 主动连板子")
    common(p, BOARD_IP)
    p.add_argument("--size", type=int, default=1024, help="每次发送的字节数")
    p.add_argument("--count", type=int, default=1, help="往返次数")
    p.add_argument("--interval", type=float, default=0.0, help="两次之间的间隔 (秒)")
    p.add_argument("--stall", action="store_true",
                   help="连上后不读不写, 压满对端的发送窗口 (测对端的发送超时)")
    p.add_argument("--wait", type=float, default=40.0,
                   help="连不上时等板子这么久 (板子要跑完前面的自检阶段才监听)")

    p = sub.add_parser("udp", help="UDP echo, 可用 --burst 主动打一串包")
    common(p, "0.0.0.0")
    p.add_argument("--peer", default=BOARD_IP, help="--burst 的目标地址")
    p.add_argument("--size", type=int, default=100, help="每个包的字节数 (含 4 字节序号)")
    p.add_argument("--burst", type=int, default=0, help="启动时先发这么多个包")
    p.add_argument("--no-recv", action="store_true",
                   help="发完 --burst 就退出, 不等回包 (丢包率由板子统计)")

    args = parser.parse_args()

    try:
        if args.cmd == "server":
            serve_tcp(args)
        elif args.cmd == "client":
            run_client(args)
        else:
            serve_udp(args)
    except KeyboardInterrupt:
        print("\n[peer] interrupted", flush=True)


if __name__ == "__main__":
    main()
