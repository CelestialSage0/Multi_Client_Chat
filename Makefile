CC = gcc
CFLAGS = -pthread

all:
	$(CC) $(CFLAGS) chat_server/thread.c -o chat_server/thread_server
	$(CC) $(CFLAGS) chat_client/client.c -o chat_client/client

clean:
	rm -f chat_server/thread_server chat_client/client
