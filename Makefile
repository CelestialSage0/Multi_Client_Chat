CC = gcc
CFLAGS = -Wall -pthread

# Paths
MONITOR_SRC = monitoring/monitor.c
MONITOR_OBJ = monitoring/monitor.o

THREAD_SRC = chat_server/thread.c
FORK_SRC = chat_server/fork.c
SELECT_SRC = chat_server/select.c
CLIENT_SRC = chat_client/client.c
DISCOVERY_SRC = discovery_server/discovery.c

# Default build
all: thread fork select client discovery

# Compile monitor object
$(MONITOR_OBJ): $(MONITOR_SRC) monitoring/monitor.h
	$(CC) $(CFLAGS) -c $(MONITOR_SRC) -o $(MONITOR_OBJ)

# Thread server
thread: $(MONITOR_OBJ)
	$(CC) $(CFLAGS) $(THREAD_SRC) $(MONITOR_OBJ) -o chat_server/thread_server

# Fork server
fork: $(MONITOR_OBJ)
	$(CC) $(CFLAGS) $(FORK_SRC) $(MONITOR_OBJ) -o chat_server/fork_server

# Select server
select: $(MONITOR_OBJ)
	$(CC) $(CFLAGS) $(SELECT_SRC) $(MONITOR_OBJ) -o chat_server/select_server

# Client
client:
	$(CC) $(CFLAGS) $(CLIENT_SRC) -o chat_client/client

# Discovery server
discovery:
	$(CC) $(CFLAGS) $(DISCOVERY_SRC) -o discovery_server/discovery

# Clean everything
clean:
	rm -f \
	chat_server/thread_server \
	chat_server/fork_server \
	chat_server/select_server \
	chat_client/client \
	discovery_server/discovery \
	monitoring/monitor.o
