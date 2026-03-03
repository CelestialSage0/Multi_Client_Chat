#!/bin/bash
# run_benchmarks.sh
# ─────────────────────────────────────────────────────────────────
# Full benchmark suite: runs load + stress tests against each
# server variant (fork, thread, select), captures all output to
# a timestamped report file, then generates plots.
#
# USAGE:
#   chmod +x run_benchmarks.sh
#   ./run_benchmarks.sh
#
# Output files produced:
#   logs/benchmark_report_<timestamp>.txt   ← full console transcript
#   logs/server_fork_<timestamp>.log        ← fork server stderr/stdout
#   logs/server_thread_<timestamp>.log      ← thread server stderr/stdout
#   logs/server_select_<timestamp>.log      ← select server stderr/stdout
#   logs/discovery_<timestamp>.log          ← discovery server output
#   graphs/*.png                            ← performance plots
# ─────────────────────────────────────────────────────────────────

DISCOVERY_BIN="./discovery_server/discovery"
SERVER_FORK="./chat_server/fork_server"
SERVER_THREAD="./chat_server/thread_server"
SERVER_SELECT="./chat_server/select_server"
BOT_SCRIPT="./scripts/script.py"
PLOT_SCRIPT="./scripts/plot.py"
LOGS_DIR="./logs"
GRAPHS_DIR="./graphs"       # FIX: was never created; plot.py writes here

# ── Timestamped output file ───────────────────────────────────────
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
REPORT="$LOGS_DIR/benchmark_report_${TIMESTAMP}.txt"

# ─────────────────────────────────────────────────────────────────
# Redirect ALL output (stdout + stderr) to both terminal and file.
# Everything below this line is captured automatically.
# ─────────────────────────────────────────────────────────────────
mkdir -p "$LOGS_DIR" "$GRAPHS_DIR"
exec > >(tee -a "$REPORT") 2>&1

echo "══════════════════════════════════════════════"
echo "  Benchmark Suite"
echo "  Started : $(date)"
echo "  Report  : $REPORT"
echo "══════════════════════════════════════════════"

# FIX: Removed `set -e` — a single bot failure (e.g. a dropped connection)
#      would abort all remaining server runs. Handle errors per-command instead.

# ── Helper: kill a background process and wait ───────────────────
kill_and_wait() {
  local pid=$1 name=$2
  if kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    echo "  [stopped] $name (PID $pid)"
  else
    echo "  [already gone] $name (PID $pid)"
  fi
}

# ── Helper: wait for a port to open (max 10 s) ───────────────────
wait_for_port() {
  local port=$1 name=$2
  for i in $(seq 1 10); do
    if ss -tlnp 2>/dev/null | grep -q ":${port} "; then
      echo "  [ready] $name on port $port"
      return 0
    fi
    sleep 1
  done
  echo "  [ERROR] $name did not open port $port after 10s — aborting."
  exit 1
}

# ── Helper: run one full server benchmark (load + stress) ────────
run_benchmark() {
  local label=$1       # "Fork" / "Thread" / "Select"
  local binary=$2      # path to server binary
  local server_tag=$3  # fork / thread / select  (matches script.py --server arg)
  local srv_log="$LOGS_DIR/server_${server_tag}_${TIMESTAMP}.log"

  echo ""
  echo "══════════════════════════════════════════════"
  echo "  [$label Server]  Starting..."
  echo "══════════════════════════════════════════════"

  # Start server; redirect its own output to a dedicated log file
  "$binary" > "$srv_log" 2>&1 &
  local SRV_PID=$!
  echo "  Server PID : $SRV_PID"
  echo "  Server log : $srv_log"

  wait_for_port 8000 "$label server"

  # ── Load test ──────────────────────────────────────────────────
  echo ""
  echo "  ── Load Test (10 bots × 30 messages) ──"
  if python3 "$BOT_SCRIPT" --mode load --server "$server_tag" \
             --bots 10 --messages 30; then
    echo "  [OK] Load test complete"
  else
    echo "  [WARN] Load test exited with errors (continuing)"
  fi

  # ── Stress test ────────────────────────────────────────────────
  echo ""
  echo "  ── Stress Test (ramp 2 → 12 clients) ──"
  if python3 "$BOT_SCRIPT" --mode stress --server "$server_tag" \
             --messages 15; then
    echo "  [OK] Stress test complete"
  else
    echo "  [WARN] Stress test exited with errors (continuing)"
  fi

  # ── Teardown ───────────────────────────────────────────────────
  kill_and_wait "$SRV_PID" "$label server"

  echo "  Sleeping 3s before next server..."
  sleep 3
}

# ═══════════════════════════════════════════════════════════════
# Start Discovery Server
# ═══════════════════════════════════════════════════════════════
echo ""
echo "══════════════════════════════════════════════"
echo "  Starting Discovery Server"
echo "══════════════════════════════════════════════"

DISC_LOG="$LOGS_DIR/discovery_${TIMESTAMP}.log"
"$DISCOVERY_BIN" > "$DISC_LOG" 2>&1 &
DISC_PID=$!
echo "  Discovery PID : $DISC_PID"
echo "  Discovery log : $DISC_LOG"

wait_for_port 8080 "discovery server"

# ═══════════════════════════════════════════════════════════════
# Run all three server variants
# ═══════════════════════════════════════════════════════════════
run_benchmark "Fork"     "$SERVER_FORK"   "fork"
run_benchmark "Thread"   "$SERVER_THREAD" "thread"
run_benchmark "Select"   "$SERVER_SELECT" "select"

# ═══════════════════════════════════════════════════════════════
# Stop Discovery Server
# ═══════════════════════════════════════════════════════════════
echo ""
kill_and_wait "$DISC_PID" "discovery server"

# ═══════════════════════════════════════════════════════════════
# Generate Plots
# ═══════════════════════════════════════════════════════════════
echo ""
echo "══════════════════════════════════════════════"
echo "  Generating Plots → $GRAPHS_DIR/"
echo "══════════════════════════════════════════════"

if python3 "$PLOT_SCRIPT"; then
  echo "  [OK] Plots saved to $GRAPHS_DIR/"
else
  echo "  [WARN] plot.py exited with errors — check log files in $LOGS_DIR/"
fi

# ═══════════════════════════════════════════════════════════════
# Summary
# ═══════════════════════════════════════════════════════════════
echo ""
echo "══════════════════════════════════════════════"
echo "  Benchmark Complete"
echo "  Finished : $(date)"
echo ""
echo "  Output files:"
echo "    Full report   : $REPORT"
echo "    Server logs   : $LOGS_DIR/server_*_${TIMESTAMP}.log"
echo "    Discovery log : $DISC_LOG"
echo "    Stress summaries:"
for f in "$LOGS_DIR"/stress_summary_*.txt; do
  [ -f "$f" ] && echo "      $f"
done
echo "    Latency files:"
for f in "$LOGS_DIR"/latency_*.txt; do
  [ -f "$f" ] && echo "      $f"
done
echo "    Graphs:"
for f in "$GRAPHS_DIR"/*.png; do
  [ -f "$f" ] && echo "      $f"
done
echo "══════════════════════════════════════════════"