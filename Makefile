CC = gcc
CFLAGS = -pthread

all:
	$(CC) $(CFLAGS) chat_server/thread.c -o chat_server/thread_server
	$(CC) chat_server/fork.c -o fork_server
	$(CC) chat_server/select.c -o select_server
	$(CC) chat_client/client.c -o chat_client/client

client:
	$(CC) chat_client/client.c -o chat_client/client

thread: client
	$(CC) $(CFLAGS) chat_server/thread.c -o chat_server/thread_server

fork: client
	$(CC) chat_server/fork.c -o chat_server/fork_server

select: client
	$(CC) chat_server/select.c -o chat_server/select_server

clean:
	rm -f chat_server/thread_server chat_server/fork_server chat_server/select_server chat_client/client
