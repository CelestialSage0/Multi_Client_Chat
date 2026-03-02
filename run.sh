#!/bin/bash
# run_benchmarks.sh
# ─────────────────────────────────────────────────────────────────
# Full benchmark suite: runs load + stress tests against each
# server variant (fork, thread, nonblocking/select), then plots.
#
# USAGE:
#   chmod +x run_benchmarks.sh
#   ./run_benchmarks.sh
#
# BEFORE RUNNING:
#   1. Compile the discovery server:  gcc discovery/discovery.c -o discovery/discovery
#   2. Compile each chat server:
#        gcc servers/fork.c   monitoring/monitor.c -o servers/server_fork   -lpthread
#        gcc servers/thread.c monitoring/monitor.c -o servers/server_thread -lpthread
#        gcc servers/select.c monitoring/monitor.c -o servers/server_select -lpthread
#   3. Compile the client:
#        gcc chat_client/client.c -o chat_client/client -lpthread
#   4. pip install matplotlib numpy   (for plots)
#
# ADJUST these paths to match YOUR directory structure:
DISCOVERY_BIN="./discovery_server/discovery"
SERVER_FORK="./chat_server/fork_server"
SERVER_THREAD="./chat_server/thread_server"
SERVER_SELECT="./chat_server/select_server"
BOT_SCRIPT="./scripts/script.py"
PLOT_SCRIPT="./scripts/plot.py"
LOGS_DIR="./logs"
# ─────────────────────────────────────────────────────────────────

set -e # stop on any error

mkdir -p "$LOGS_DIR"

# ── Helper: kill a background process and wait ───────────────────
kill_and_wait() {
  local pid=$1
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
}

echo ""
echo "══════════════════════════════════════════════"
echo "  Starting Discovery Server"
echo "══════════════════════════════════════════════"
"$DISCOVERY_BIN" &
DISC_PID=$!
sleep 1
echo "  Discovery PID: $DISC_PID"

# ═══════════════════════════════════════════════
# 1. FORK SERVER
# ═══════════════════════════════════════════════
echo ""
echo "══════════════════════════════════════════════"
echo "  [1/3] Fork Server — Load Test"
echo "══════════════════════════════════════════════"
"$SERVER_FORK" &
SRV_PID=$!
sleep 1

python3 "$BOT_SCRIPT" --mode load --server fork --bots 10 --messages 30

echo "  [1/3] Fork Server — Stress Test"
python3 "$BOT_SCRIPT" --mode stress --server fork --messages 15

kill_and_wait "$SRV_PID"
echo "  Fork server stopped. Sleeping 3s..."
sleep 3

# ═══════════════════════════════════════════════
# 2. THREAD SERVER
# ═══════════════════════════════════════════════
echo ""
echo "══════════════════════════════════════════════"
echo "  [2/3] Thread Server — Load Test"
echo "══════════════════════════════════════════════"
"$SERVER_THREAD" &
SRV_PID=$!
sleep 1

python3 "$BOT_SCRIPT" --mode load --server thread --bots 10 --messages 30

echo "  [2/3] Thread Server — Stress Test"
python3 "$BOT_SCRIPT" --mode stress --server thread --messages 15

kill_and_wait "$SRV_PID"
echo "  Thread server stopped. Sleeping 3s..."
sleep 3

# ═══════════════════════════════════════════════
# 3. SELECT / NON-BLOCKING SERVER
# ═══════════════════════════════════════════════
echo ""
echo "══════════════════════════════════════════════"
echo "  [3/3] Non-blocking Server — Load Test"
echo "══════════════════════════════════════════════"
"$SERVER_SELECT" &
SRV_PID=$!
sleep 1

python3 "$BOT_SCRIPT" --mode load --server select --bots 10 --messages 30

echo "  [3/3] Non-blocking Server — Stress Test"
python3 "$BOT_SCRIPT" --mode stress --server select --messages 15

kill_and_wait "$SRV_PID"
echo "  Non-blocking server stopped."

# ═══════════════════════════════════════════════
# Stop Discovery Server
# ═══════════════════════════════════════════════
kill_and_wait "$DISC_PID"
echo ""
echo "  All servers stopped."

# ═══════════════════════════════════════════════
# Generate Plots
# ═══════════════════════════════════════════════
echo ""
echo "══════════════════════════════════════════════"
echo "  Generating Plots"
echo "══════════════════════════════════════════════"
python3 "$PLOT_SCRIPT"

echo ""
echo "✓ Benchmark complete. Check ./benchmarks/ for graphs."
