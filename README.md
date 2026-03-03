# CS 3205 Assignment 2: Multi-Client Chat System

## System Architecture Overview┌──────────────────────────────────────────────────────────────┐
│                    Chat Client  (client.c)                    │
│  CLI: broadcast / private / list / history / status          │
└──────────┬──────────────────────────────────────────────────-┘
│ TCP :8080 (auth)              │ TCP :8000 (chat)
▼                               ▼
┌─────────────────────────┐   ┌──────────────────────────────────┐
│   Discovery Server      │   │   Chat Server  (3 variants)      │
│  REGISTER / AUTH        │   │  fork.c   – one process/client   │
│  Maps user → IP:port    │   │  thread.c – one thread/client    │
└─────────────────────────┘   │  select.c – single-threaded I/O  │
│  All: broadcast, private,        │
│  user list, status, history      │
└──────────────────────────────────┘

### Directory Structureproject/
├── chat_server/     fork.c  thread.c  select.c
├── chat_client/     client.c
├── discovery/       server.c
├── monitoring/      monitor.c  monitor.h
├── benchmarks/      script.py  plot.py
├── logs/            metrics_*.txt  latency.txt  history.json
├── graphs/          (generated PNGs)
├── Makefile
└── README.md

---

## Protocol Specification

All messages are **plain-text TCP** framed by `\n`.  
Every client message is prefixed with a **nanosecond `CLOCK_MONOTONIC` timestamp**.<timestamp_ns>  <COMMAND>  [arg1]  [rest of message]\n

### Client → Chat Server

| Command    | Example                         | Description                |
|------------|---------------------------------|----------------------------|
| LOGIN      | `123 LOGIN alice`               | Register session after auth|
| BROADCAST  | `123 BROADCAST hello everyone`  | Send to all online users   |
| PRIVATE    | `123 PRIVATE bob hey there`     | Direct message             |
| LIST       | `123 LIST`                      | Online users + statuses    |
| HISTORY    | `123 HISTORY`                   | View own message history   |
| STATUS     | `123 STATUS busy`               | Set presence (available/busy/away) |

### Client → Discovery Server

| Command   | Example                    | Description   |
|-----------|----------------------------|---------------|
| REGISTER  | `123 REGISTER alice pass`  | Create account |
| AUTH      | `123 AUTH alice pass`      | Authenticate  |

### Server Responses

| Response                             | Meaning                          |
|--------------------------------------|----------------------------------|
| `LOGIN_SUCCESS`                      | Chat login accepted              |
| `AUTH_SUCCESS`                       | Discovery auth passed            |
| `ONLINE alice(available) bob(busy)`  | User list with statuses          |
| `STATUS_OK busy`                     | Status change confirmed          |
| `ERROR <reason>`                     | Command rejected                 |
| `<ts> BROADCAST: alice hello`        | Incoming broadcast (ts = original send time) |
| `<ts> MSG alice hi`                  | Incoming private message         |

### Chat History Format — NDJSON (`logs/history.json`)

One JSON object per line, appended on every message:
```json{"type":"BROADCAST","from":"alice","to":"bob","msg":"hello world","timestamp":"2025-03-01T12:00:00Z"}
{"type":"PRIVATE","from":"alice","to":"charlie","msg":"hi!","timestamp":"2025-03-01T12:01:05Z"}

---

## Compilation and Execution

### Prerequisites
```bashsudo apt-get install gcc make python3 python3-pip
pip3 install matplotlib numpy

### Build
```bashmake all       # build everything
make fork      # fork server only
make thread    # thread server only
make select    # select server only
make client    # client only
make clean

### Running
```bashmkdir -p logs graphsTerminal 1 — Discovery server (port 8080)
./discovery/discovery_serverTerminal 2 — Chat server (port 8000), pick one:
./chat_server/fork_server
./chat_server/thread_server
./chat_server/select_serverTerminal 3, 4, … — Clients
./chat_client/client

### Client CLI CommandsBROADCAST hello world
PRIVATE alice how are you?
LIST
HISTORY
STATUS available
STATUS busy
STATUS away

---

## Testing Guide

### Manual
1. Start discovery + one chat server.
2. Open 3+ terminals; run `client` in each.
3. Register (option 1) first time; Login (option 2) subsequently.
4. Test all commands across clients.

### Automated Benchmarks
```bashcd benchmarksLoad test (10 bots × 30 messages)
python3 script.py --mode load --server thread --bots 10 --messages 30Stress test (ramp 2 → 12 clients)
python3 script.py --mode stress --server forkPreserve latency files per variant, then graph:
cp ../logs/latency.txt ../logs/latency_fork.txt
python3 plot.py

---

## Notes
- `client.c` must run from `chat_client/` so `../logs/latency.txt` resolves correctly.
- Max concurrent clients: 100 (adjustable via `#define` in server files).
- Chat history persists across restarts in `logs/history.json`.
- Monitoring thread samples CPU + VmRSS every 5 seconds.
- Thread server uses `PTHREAD_CREATE_DETACHED` so thread resources are reclaimed automatically.