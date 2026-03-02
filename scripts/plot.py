#!/usr/bin/env python3
"""
plot_results.py  –  Generate all required benchmark graphs
===================================================================
Reads log files produced by:
  - monitor.c  → ../logs/metrics_fork.txt, metrics_thread.txt, metrics_select.txt
  - client.c   → ../logs/latency_fork.txt, latency_thread.txt, latency_nonblocking.txt
  - benchmark_bot.py → ../logs/stress_summary_*.txt

Run after benchmark tests:
    python3 plot_results.py

Outputs (saved next to this script):
    latency_distribution.png
    cpu_vs_clients.png
    memory_vs_clients.png
"""

import os
import sys
import statistics

# ── Try importing matplotlib, guide user if missing ───────────────────────────
try:
    import matplotlib.pyplot as plt
    import matplotlib.patches as mpatches
    import numpy as np
except ImportError:
    print("matplotlib / numpy not found. Install with:")
    print("  pip install matplotlib numpy")
    sys.exit(1)

LOGS_DIR   = "./logs"
OUTPUT_DIR = "./graphs"   # graphs saved here (same folder as this script)

COLORS = {
    "fork":        "#e74c3c",   # red
    "thread":      "#2ecc71",   # green
    "select":      "#3498db",   # alias for nonblocking
}

SERVER_TYPES = ["fork", "thread", "select"]
LABELS       = {"fork": "Fork", "thread": "Thread", "select": "Non-blocking (select)"}


# ─────────────────────────────────────────────
# Data Loaders
# ─────────────────────────────────────────────

def load_latency(server_type: str):
    """Load latency values (ms) from ../logs/latency_<type>.txt"""
    # nonblocking server writes metrics_select.txt but latency as nonblocking
    fname = os.path.join(LOGS_DIR, f"latency_{server_type}.txt")
    if not os.path.exists(fname):
        print(f"  [skip] {fname} not found")
        return []
    with open(fname) as f:
        vals = []
        for line in f:
            line = line.strip()
            if line:
                try:
                    vals.append(float(line))
                except ValueError:
                    pass
    print(f"  Loaded {len(vals)} latency samples from {fname}")
    return vals


def load_metrics(server_type: str):
    """
    Load server metrics from ../logs/metrics_<type>.txt
    Format written by monitor.c:  timestamp_ms,cpu_percent,vmrss_kb
    Returns dict with lists: timestamp, cpu, vmrss
    """
    # select server writes metrics_select.txt
    tag = "select" if server_type == "select" else server_type
    fname = os.path.join(LOGS_DIR, f"metrics_{tag}.txt")
    if not os.path.exists(fname):
        print(f"  [skip] {fname} not found")
        return None

    timestamps, cpu, vmrss = [], [], []
    with open(fname) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split(",")
            if len(parts) < 3:
                continue
            try:
                timestamps.append(int(parts[0]))
                cpu.append(float(parts[1]))
                vmrss.append(int(parts[2]))
            except ValueError:
                pass

    if not timestamps:
        print(f"  [skip] {fname} is empty")
        return None

    print(f"  Loaded {len(timestamps)} metric samples from {fname}")
    # Normalise timestamps to seconds from start
    t0 = timestamps[0]
    t_sec = [(t - t0) / 1000.0 for t in timestamps]
    return {"t": t_sec, "cpu": cpu, "vmrss": vmrss}


def load_stress_summary(server_type: str):
    """
    Load stress test summary: num_clients,throughput,wall_time
    Written by benchmark_bot.py
    """
    fname = os.path.join(LOGS_DIR, f"stress_summary_{server_type}.txt")
    if not os.path.exists(fname):
        return None
    clients, throughput = [], []
    with open(fname) as f:
        next(f, None)  # skip header
        for line in f:
            parts = line.strip().split(",")
            if len(parts) >= 2:
                try:
                    clients.append(int(parts[0]))
                    throughput.append(float(parts[1]))
                except ValueError:
                    pass
    return {"clients": clients, "throughput": throughput}


# ─────────────────────────────────────────────
# Plot 1: Latency Distribution (histogram)
# ─────────────────────────────────────────────

def plot_latency_distribution():
    print("\n[Plot 1] Latency distribution...")

    fig, axes = plt.subplots(1, 3, figsize=(15, 5), sharey=False)
    fig.suptitle("Message Delivery Latency Distribution", fontsize=14, fontweight="bold")

    any_data = False
    for ax, stype in zip(axes, SERVER_TYPES):
        vals = load_latency(stype)
        if not vals:
            ax.text(0.5, 0.5, "No data", ha="center", va="center",
                    transform=ax.transAxes, fontsize=12, color="grey")
            ax.set_title(LABELS[stype])
            continue

        any_data = True
        color = COLORS[stype]

        # Clip extreme outliers for cleaner histogram (keep 99th percentile)
        vals_sorted = sorted(vals)
        cutoff = vals_sorted[int(len(vals_sorted) * 0.99)]
        clipped = [v for v in vals if v <= cutoff]

        ax.hist(clipped, bins=40, color=color, alpha=0.8, edgecolor="white", linewidth=0.3)

        mean_val   = statistics.mean(vals)
        median_val = statistics.median(vals)

        ax.axvline(mean_val,   color="black",  linestyle="--", linewidth=1.2, label=f"Mean {mean_val:.1f}ms")
        ax.axvline(median_val, color="orange", linestyle=":",  linewidth=1.2, label=f"Median {median_val:.1f}ms")

        ax.set_title(LABELS[stype], fontweight="bold")
        ax.set_xlabel("Latency (ms)")
        ax.set_ylabel("Frequency")
        ax.legend(fontsize=8)

        # Stats box
        stdev = statistics.stdev(vals) if len(vals) > 1 else 0
        stats_text = f"n={len(vals)}\nmin={min(vals):.1f}\nmax={max(vals):.1f}\nσ={stdev:.1f}"
        ax.text(0.97, 0.97, stats_text, transform=ax.transAxes,
                fontsize=7, va="top", ha="right",
                bbox=dict(boxstyle="round,pad=0.3", facecolor="lightyellow", alpha=0.8))

    plt.tight_layout()
    out = os.path.join(OUTPUT_DIR, "latency_distribution.png")
    plt.savefig(out, dpi=150, bbox_inches="tight")
    print(f"  Saved → {out}")
    if any_data:
        plt.show()
    plt.close()


# ─────────────────────────────────────────────
# Plot 2: Latency Overlay (all 3 on one plot)
# ─────────────────────────────────────────────

def plot_latency_overlay():
    print("\n[Plot 2] Latency overlay comparison...")

    fig, ax = plt.subplots(figsize=(10, 5))
    ax.set_title("Latency Distribution – Fork vs Thread vs Non-blocking", fontweight="bold")

    any_data = False
    for stype in SERVER_TYPES:
        vals = load_latency(stype)
        if not vals:
            continue
        any_data = True
        vals_sorted = sorted(vals)
        cutoff = vals_sorted[int(len(vals_sorted) * 0.99)]
        clipped = [v for v in vals if v <= cutoff]
        ax.hist(clipped, bins=40, color=COLORS[stype], alpha=0.5,
                label=f"{LABELS[stype]} (n={len(vals)}, μ={statistics.mean(vals):.1f}ms)",
                edgecolor="white", linewidth=0.2)

    ax.set_xlabel("Latency (ms)")
    ax.set_ylabel("Frequency")
    ax.legend()
    plt.tight_layout()
    out = os.path.join(OUTPUT_DIR, "latency_overlay.png")
    plt.savefig(out, dpi=150, bbox_inches="tight")
    print(f"  Saved → {out}")
    if any_data:
        plt.show()
    plt.close()


# ─────────────────────────────────────────────
# Plot 3: CPU Usage over Time
# ─────────────────────────────────────────────

def plot_cpu_over_time():
    print("\n[Plot 3] CPU usage over time...")

    fig, ax = plt.subplots(figsize=(11, 5))
    ax.set_title("CPU Usage Over Time – Fork vs Thread vs Non-blocking", fontweight="bold")

    any_data = False
    for stype in SERVER_TYPES:
        data = load_metrics(stype)
        if not data:
            continue
        any_data = True
        ax.plot(data["t"], data["cpu"], color=COLORS[stype],
                label=LABELS[stype], linewidth=1.5, alpha=0.85)

    ax.set_xlabel("Time (seconds from server start)")
    ax.set_ylabel("CPU Usage (%)")
    ax.legend()
    ax.set_ylim(bottom=0)
    plt.tight_layout()
    out = os.path.join(OUTPUT_DIR, "cpu_over_time.png")
    plt.savefig(out, dpi=150, bbox_inches="tight")
    print(f"  Saved → {out}")
    if any_data:
        plt.show()
    plt.close()


# ─────────────────────────────────────────────
# Plot 4: Memory (VmRSS) over Time
# ─────────────────────────────────────────────

def plot_memory_over_time():
    print("\n[Plot 4] Memory usage over time...")

    fig, ax = plt.subplots(figsize=(11, 5))
    ax.set_title("Memory (VmRSS) Over Time – Fork vs Thread vs Non-blocking", fontweight="bold")

    any_data = False
    for stype in SERVER_TYPES:
        data = load_metrics(stype)
        if not data:
            continue
        any_data = True
        vmrss_mb = [v / 1024.0 for v in data["vmrss"]]  # KB → MB
        ax.plot(data["t"], vmrss_mb, color=COLORS[stype],
                label=LABELS[stype], linewidth=1.5, alpha=0.85)

    ax.set_xlabel("Time (seconds from server start)")
    ax.set_ylabel("VmRSS (MB)")
    ax.legend()
    ax.set_ylim(bottom=0)
    plt.tight_layout()
    out = os.path.join(OUTPUT_DIR, "memory_over_time.png")
    plt.savefig(out, dpi=150, bbox_inches="tight")
    print(f"  Saved → {out}")
    if any_data:
        plt.show()
    plt.close()


# ─────────────────────────────────────────────
# Plot 5: Throughput vs Number of Clients (Stress)
# ─────────────────────────────────────────────

def plot_throughput_vs_clients():
    print("\n[Plot 5] Throughput vs number of clients (stress test)...")

    fig, ax = plt.subplots(figsize=(9, 5))
    ax.set_title("Throughput vs Number of Clients – Stress Test", fontweight="bold")

    any_data = False
    for stype in SERVER_TYPES:
        data = load_stress_summary(stype)
        if not data or not data["clients"]:
            continue
        any_data = True
        ax.plot(data["clients"], data["throughput"],
                color=COLORS[stype], label=LABELS[stype],
                marker="o", linewidth=2, markersize=6)

    ax.set_xlabel("Number of Concurrent Clients")
    ax.set_ylabel("Throughput (messages / second)")
    ax.legend()
    ax.set_ylim(bottom=0)
    ax.xaxis.get_major_locator().set_params(integer=True)
    plt.tight_layout()
    out = os.path.join(OUTPUT_DIR, "throughput_vs_clients.png")
    plt.savefig(out, dpi=150, bbox_inches="tight")
    print(f"  Saved → {out}")
    if any_data:
        plt.show()
    plt.close()


# ─────────────────────────────────────────────
# Plot 6: CPU vs Clients  (from stress + metrics)
#   Reads peak CPU from the server metric file
#   for the time window of each stress level.
#   Simple approach: split the metric file into
#   equal segments equal to number of stress levels.
# ─────────────────────────────────────────────

def plot_cpu_vs_clients():
    print("\n[Plot 6] CPU vs number of clients...")

    stress_levels = [2, 4, 6, 8, 10, 12]

    fig, axes = plt.subplots(1, 2, figsize=(14, 5))
    fig.suptitle("Resource Usage vs Number of Clients", fontweight="bold")

    cpu_ax, mem_ax = axes

    any_data = False
    for stype in SERVER_TYPES:
        data = load_metrics(stype)
        if not data:
            continue

        cpu_vals = data["cpu"]
        mem_vals = [v / 1024.0 for v in data["vmrss"]]  # MB
        n = len(cpu_vals)

        if n < len(stress_levels):
            # Not enough samples to split — just plot all as one bar
            avg_cpu = [statistics.mean(cpu_vals)] * len(stress_levels)
            avg_mem = [statistics.mean(mem_vals)] * len(stress_levels)
        else:
            # Split metric timeline equally among stress levels
            chunk = n // len(stress_levels)
            avg_cpu, avg_mem = [], []
            for idx in range(len(stress_levels)):
                start = idx * chunk
                end   = start + chunk
                segment_cpu = cpu_vals[start:end]
                segment_mem = mem_vals[start:end]
                avg_cpu.append(statistics.mean(segment_cpu) if segment_cpu else 0)
                avg_mem.append(statistics.mean(segment_mem) if segment_mem else 0)

        any_data = True
        cpu_ax.plot(stress_levels[:len(avg_cpu)], avg_cpu,
                    color=COLORS[stype], label=LABELS[stype],
                    marker="s", linewidth=2, markersize=6)
        mem_ax.plot(stress_levels[:len(avg_mem)], avg_mem,
                    color=COLORS[stype], label=LABELS[stype],
                    marker="s", linewidth=2, markersize=6)

    cpu_ax.set_xlabel("Number of Clients")
    cpu_ax.set_ylabel("Avg CPU Usage (%)")
    cpu_ax.set_title("CPU Usage vs Clients")
    cpu_ax.legend()
    cpu_ax.set_ylim(bottom=0)

    mem_ax.set_xlabel("Number of Clients")
    mem_ax.set_ylabel("Avg VmRSS (MB)")
    mem_ax.set_title("Memory Usage vs Clients")
    mem_ax.legend()
    mem_ax.set_ylim(bottom=0)

    plt.tight_layout()
    out = os.path.join(OUTPUT_DIR, "cpu_memory_vs_clients.png")
    plt.savefig(out, dpi=150, bbox_inches="tight")
    print(f"  Saved → {out}")
    if any_data:
        plt.show()
    plt.close()


# ─────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────

def main():
    print("=" * 60)
    print("  Benchmark Results Visualiser")
    print(f"  Reading logs from : {os.path.abspath(LOGS_DIR)}")
    print(f"  Saving graphs to  : {os.path.abspath(OUTPUT_DIR)}")
    print("=" * 60)

    plot_latency_distribution()
    plot_latency_overlay()
    plot_cpu_over_time()
    plot_memory_over_time()
    plot_throughput_vs_clients()
    plot_cpu_vs_clients()

    print("\n✓ All plots done.")


if __name__ == "__main__":
    main()
