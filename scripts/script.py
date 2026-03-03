#!/usr/bin/env python3
"""
Benchmark Bot for Multi-Client Chat System
==========================================
Spawns automated client processes that register, login,
and send messages — measuring latency and server load.

Usage:
    python3 benchmark_bot.py --mode load   --server fork
    python3 benchmark_bot.py --mode load   --server thread
    python3 benchmark_bot.py --mode load   --server select
    python3 benchmark_bot.py --mode stress --server thread

Requirements:
    - Discovery server running on port 8080
    - One of the chat servers running on port 8000
    - ../logs/ directory must exist relative to the client binary

Directory structure assumed:
    project/
    ├── chat_client/
    │   └── client          ← compiled binary
    ├── logs/               ← created by this script
    └── benchmarks/
        └── benchmark_bot.py  ← this file
"""

import subprocess
import threading
import time
import os
import argparse
import statistics
import signal
import sys

# ─────────────────────────────────────────────
# CONFIG  –  adjust these paths to match yours
# ─────────────────────────────────────────────
CLIENT_BINARY   = "./chat_client/client"   # path to compiled client
LOGS_DIR        = "./logs"                 # must exist; monitor.c writes here too
MESSAGES_PER_CLIENT = 30                   # broadcasts per bot in load test
BROADCAST_DELAY     = 0.0                  # seconds between each message
BOT_USERNAME_PREFIX = "bot"
BOT_PASSWORD        = "pass123"

# ─────────────────────────────────────────────


def ensure_logs_dir():
    os.makedirs(LOGS_DIR, exist_ok=True)


# ─────────────────────────────────────────────
# Single Bot Client
# ─────────────────────────────────────────────
class BotClient:
    """
    Wraps one instance of the compiled client binary.
    Communicates via stdin pipe exactly as a human would.
    """

    def __init__(self, bot_id: int, server_type: str):
        self.bot_id     = bot_id
        self.username   = f"{BOT_USERNAME_PREFIX}{bot_id}"
        self.password   = BOT_PASSWORD
        self.server_type = server_type
        self.process    = None
        self.latencies  = []          # collected from stdout parsing
        self._stdout_lines = []
        self._reader_thread = None

    # ── Launch the client process ──────────────────────────────────────────
    def start(self, action: int = 1):
        """
        action=1 → Register then connect
        action=2 → Login then connect
        """
        self.process = subprocess.Popen(
            [CLIENT_BINARY],
            stdin  = subprocess.PIPE,
            stdout = subprocess.PIPE,
            stderr = subprocess.PIPE,
            text   = True,
            bufsize = 1,          # line-buffered
        )

        # Start background thread to drain stdout (prevents deadlock)
        self._reader_thread = threading.Thread(
            target=self._read_stdout, daemon=True
        )
        self._reader_thread.start()

        # ── Mimic human interaction ──────────────────────────────────
        # 1. Choose Register (1) or Login (2)
        self._send(str(action))
        time.sleep(0.1)

        # 2. Username
        self._send(self.username)
        time.sleep(0.1)

        # 3. Password
        self._send(self.password)
        time.sleep(0.5)   # give discovery server time to respond + chat login

    def _send(self, text: str):
        """Write one line to the client's stdin."""
        try:
            self.process.stdin.write(text + "\n")
            self.process.stdin.flush()
        except (BrokenPipeError, OSError):
            pass   # client already exited

    def _read_stdout(self):
        """Drain stdout in background so the pipe doesn't fill up."""
        try:
            for line in self.process.stdout:
                self._stdout_lines.append(line.rstrip())
        except Exception:
            pass

    # ── Send messages ──────────────────────────────────────────────────────
    def broadcast(self, message: str):
        """Send a BROADCAST command."""
        self._send(f"BROADCAST {message}")

    def private(self, to_user: str, message: str):
        """Send a PRIVATE command."""
        self._send(f"PRIVATE {to_user} {message}")

    def list_users(self):
        """Send LIST command."""
        self._send("LIST")

    # ── Teardown ───────────────────────────────────────────────────────────
    def stop(self):
        try:
            self.process.stdin.close()
        except Exception:
            pass
        try:
            self.process.terminate()
            self.process.wait(timeout=3)
        except Exception:
            pass


# ─────────────────────────────────────────────
# Registration Pass
# ─────────────────────────────────────────────
def register_bots(num_bots: int, server_type: str):
    """
    Register all bot accounts first (action=1).
    Re-registering an existing user returns USER_EXISTS which is fine.
    """
    print(f"[*] Registering {num_bots} bot accounts...")
    bots = []
    for i in range(num_bots):
        b = BotClient(i, server_type)
        b.start(action=1)   # Register
        bots.append(b)
        time.sleep(0.05)    # slight stagger to avoid hammering discovery server

    time.sleep(1.0)         # wait for all registrations to complete
    for b in bots:
        b.stop()
    print(f"[*] Registration done.\n")


# ─────────────────────────────────────────────
# Load Test  –  10 concurrent bots, fixed load
# ─────────────────────────────────────────────
def run_load_test(num_bots: int, server_type: str, messages: int):
    """
    Spawn num_bots clients simultaneously.
    Each sends `messages` broadcasts.
    Returns list of (bot_id, messages_sent) tuples.
    """
    print(f"[LOAD TEST] {num_bots} bots × {messages} messages | server={server_type}")
    print("-" * 60)

    bots = []
    threads = []
    results = {}

    def bot_session(bot: BotClient):
        bot.start(action=2)    # Login (already registered)
        time.sleep(0.3)        # wait for LOGIN_SUCCESS

        sent = 0
        for m in range(messages):
            bot.broadcast(f"hello_from_{bot.username}_msg{m}")
            sent += 1
            time.sleep(BROADCAST_DELAY)

        results[bot.bot_id] = sent
        bot.stop()
        print(f"  [{bot.username}] sent {sent} messages")

    # Create all bots
    for i in range(num_bots):
        b = BotClient(i, server_type)
        bots.append(b)

    # Launch all simultaneously
    start_wall = time.monotonic()
    for b in bots:
        t = threading.Thread(target=bot_session, args=(b,), daemon=True)
        threads.append(t)
        t.start()

    # Wait for all to finish
    for t in threads:
        t.join()
    elapsed = time.monotonic() - start_wall

    total_msgs = sum(results.values())
    print(f"\n[LOAD TEST DONE]")
    print(f"  Total messages sent : {total_msgs}")
    print(f"  Wall time           : {elapsed:.2f}s")
    print(f"  Throughput          : {total_msgs/elapsed:.1f} msg/s\n")

    return results, elapsed


# ─────────────────────────────────────────────
# Stress Test  –  ramp up clients gradually
# ─────────────────────────────────────────────
def run_stress_test(server_type: str, messages_per_bot: int = 15):
    """
    Gradually increase client count: 2 → 4 → 6 → 8 → 10 → 12
    At each level, run a load test and record throughput.
    The server's monitor.c thread logs CPU/memory to the log file.
    """
    levels = [2, 4, 6, 8, 10, 12]
    stress_results = []

    summary_file = os.path.join(LOGS_DIR, f"stress_summary_{server_type}.txt")
    with open(summary_file, "w") as sf:
        sf.write(f"num_clients,throughput_msg_per_s,wall_time_s\n")

    print(f"[STRESS TEST] server={server_type}")
    print("=" * 60)

    for n in levels:
        print(f"\n── Level: {n} clients ──")
        # Register any new bots needed
        register_bots(n, server_type)

        results, elapsed = run_load_test(n, server_type, messages_per_bot)
        total = sum(results.values())
        throughput = total / elapsed if elapsed > 0 else 0

        stress_results.append((n, throughput, elapsed))

        with open(summary_file, "a") as sf:
            sf.write(f"{n},{throughput:.2f},{elapsed:.2f}\n")

        print(f"  Sleeping 5s before next level (let server stabilise)...")
        time.sleep(5)

    print(f"\n[STRESS TEST COMPLETE] Summary saved to {summary_file}")
    print(f"\n{'Clients':>10} {'Throughput (msg/s)':>20} {'Wall Time (s)':>15}")
    print("-" * 50)
    for n, tp, wt in stress_results:
        print(f"{n:>10} {tp:>20.2f} {wt:>15.2f}")


# ─────────────────────────────────────────────
# Latency Report Helper
# ─────────────────────────────────────────────
def print_latency_report(server_type: str, phase: str = ""):
    """
    Read the latency log written by client.c's receive_handler
    and print a summary.

    NOTE: client.c hardcodes the log path as ../logs/latency_thread.txt
    regardless of which server is running. After each test, copy/rename
    the file to latency_<server_type>.txt to preserve it.
    """
    raw_file = os.path.join(LOGS_DIR, "latency.txt")
    print(server_type)
    tag = f"{server_type}_{phase}" if phase else server_type
    out_file = os.path.join(LOGS_DIR, f"latency_{tag}.txt")

    if not os.path.exists(raw_file):
        print(f"[!] No latency file found at {raw_file}")
        print(f"    Make sure the client binary is run from the chat_client/ directory.")
        return

    with open(raw_file) as f:
        lines = [l.strip() for l in f if l.strip()]

    if not lines:
        print("[!] Latency file is empty.")
        return

    values = []
    for l in lines:
        try:
            values.append(float(l))
        except ValueError:
            pass

    if not values:
        print("[!] No valid latency values parsed.")
        return

    # Save a copy named after this server type
    import shutil
    shutil.copy(raw_file, out_file)
    print(f"[*] Latency data saved to {out_file}")

    print(f"\n── Latency Report ({server_type}) ──────────────────")
    print(f"  Samples : {len(values)}")
    print(f"  Min     : {min(values):.3f} ms")
    print(f"  Max     : {max(values):.3f} ms")
    print(f"  Mean    : {statistics.mean(values):.3f} ms")
    print(f"  Median  : {statistics.median(values):.3f} ms")
    if len(values) > 1:
        print(f"  Stdev   : {statistics.stdev(values):.3f} ms")

    # Clear the raw file for next run
    open(raw_file, "w").close()
    print(f"[*] Cleared {raw_file} for next run.\n")


# ─────────────────────────────────────────────
# CLI Entry Point
# ─────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="Benchmark bot for Multi-Client Chat System"
    )
    parser.add_argument(
        "--mode", choices=["load", "stress", "register"],
        default="load",
        help="Test mode: load (fixed 10 bots), stress (ramp up), or register only"
    )
    parser.add_argument(
        "--server", choices=["fork", "thread", "select"],
        default="thread",
        help="Which server variant is currently running (for log naming only)"
    )
    parser.add_argument(
        "--bots", type=int, default=10,
        help="Number of bots for load test (default: 10)"
    )
    parser.add_argument(
        "--messages", type=int, default=MESSAGES_PER_CLIENT,
        help=f"Messages per bot (default: {MESSAGES_PER_CLIENT})"
    )
    args = parser.parse_args()

    ensure_logs_dir()

    if args.mode == "register":
        register_bots(args.bots, args.server)

    elif args.mode == "load":
        # Step 1: Register all bots
        register_bots(args.bots, args.server)

        # Step 2: Run load test
        run_load_test(args.bots, args.server, args.messages)

        # Step 3: Print latency summary from what client.c logged
        print_latency_report(args.server, phase="load")

    elif args.mode == "stress":
        run_stress_test(args.server, messages_per_bot=args.messages)
        print_latency_report(args.server, phase="stress")


if __name__ == "__main__":
    main()
