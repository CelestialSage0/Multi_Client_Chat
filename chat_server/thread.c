#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#define BUFFER_SIZE 1024

// Structure to pass data to thread
typedef struct {
  int socket; // client socket
  char username[32];
} client_t;

// Assumung maximum clients are 100
client_t *clients[100];
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// Add client handler
void add_client(client_t *client) {
  pthread_mutex_lock(&lock);
  for (int i = 0; i < 100; i++) {
    if (!clients[i]) {
      clients[i] = client;
      break;
    }
  }
  printf("Maximum connections reached!\nCould not add client");
  pthread_mutex_unlock(&lock);
}

// Remove client handler
void remove_client(int socket) {
  pthread_mutex_lock(&lock);
  for (int i = 0; i < 100; i++) {
    if (clients[i] && clients[i]->socket == socket) {
      clients[i] = NULL;
      break;
    }
  }
  pthread_mutex_unlock(&lock);
}

// Broadcast handler
void broadcast(char *message) {
  pthread_mutex_lock(&lock);
  for (int i = 0; i < 100; i++) {
    if (clients[i]) {
      send(clients[i]->socket, message, strlen(message), 0);
    }
  }
  pthread_mutex_unlock(&lock);
}
