#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define PORT 8000

typedef struct {
  int socket;                 // client socket
  struct sockaddr_in address; // client address
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
      if (send(clients[i]->socket, message, strlen(message), 0) < 0) {
        perror("Broadcast failed");
      }
    }
  }
  pthread_mutex_unlock(&lock);
}

// Client handler
void *handle_client(void *arg) {
  client_t *client = (client_t *)arg;
  char buffer[BUFFER_SIZE];
  int bytes_rcved;

  add_client(client);

  while (1) {
    bytes_rcved = recv(client->socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_rcved <= 0) {
      if (bytes_rcved == 0) {
        printf("Client %s disconnected\n", client->username);
      } else {
        perror("recv failed");
      }
    }

    buffer[bytes_rcved] = '\0';

    broadcast(buffer);
  }

  close(client->socket);
  remove_client(client->socket);
  free(client);

  return NULL;
}

int main() {
  int server_socket, client_socket;
  struct sockaddr_in server_address, client_address;
  socklen_t client_address_len;
  pthread_t thread_id;

  // Create server socket
  server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket < 0) {
    perror("Socket creation failed");
    exit(EXIT_FAILURE);
  }

  int opt = 1;
  setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  // Bind and listen
  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(PORT);

  if (bind(server_socket, (struct sockaddr *)&server_address,
           sizeof(server_address)) < 0) {
    perror("Bind failed");
    exit(EXIT_FAILURE);
  }

  if (listen(server_socket, 10) < 0) {
    perror("Listen failed");
    exit(EXIT_FAILURE);
  }

  printf("Thread-based server listening on port %d\n", PORT);

  while (1) {
    client_address_len = sizeof(client_address);
    client_socket = accept(server_socket, (struct sockaddr *)&client_address,
                           &client_address_len);

    if (client_socket < 0) {
      perror("Accept failed");
      continue;
    }

    printf("New connection from %s:%d\n", inet_ntoa(client_address.sin_addr),
           ntohs(client_address.sin_port));

    // Allocate memory for client info
    client_t *client = (client_t *)malloc(sizeof(client_t));
    if (client == NULL) {
      perror("Memory allocation failed");
      close(client_socket);
      continue;
    }

    client->socket = client_socket;
    client->address = client_address;

    // Create detached thread to handle client
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(
        &attr, PTHREAD_CREATE_DETACHED); // So that zombie threads are cleaned
                                         // up automatically by OS

    if (pthread_create(&thread_id, &attr, handle_client, client) != 0) {
      perror("Thread creation failed");
      free(client);
      close(client_socket);
    } else {
      printf("Created thread (ID: %lu) to handle client\n\n", thread_id);
    }

    pthread_attr_destroy(&attr);
  }

  close(server_socket);
  return 0;
}
