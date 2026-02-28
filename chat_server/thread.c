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
  int authenticated; // 1->yes; 0->no
} client_t;

typedef struct {
  char username[32];
  char password[32];
} user_record_t;

// Assumung maximum clients are 100
client_t *clients[100];
user_record_t users[3] = {
    {"alice", "0123"}, {"bob", "1234"}, {"charlie", "12345"}};
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
void broadcast(char *message, char *from_user) {
  pthread_mutex_lock(&lock);
  for (int i = 0; i < 100; i++) {
    if (clients[i] && clients[i]->authenticated) {
      char formatted[BUFFER_SIZE];
      snprintf(formatted, sizeof(formatted), "BROADCAST: %s %s\n", from_user,
               message);

      if (send(clients[i]->socket, formatted, strlen(formatted), 0) < 0) {
        perror("Broadcast failed");
      }
    }
  }
  pthread_mutex_unlock(&lock);
}

// Authunticate handler
int authenticate(char *username, char *password) {
  for (int i = 0; i < 3; i++) {
    if (strcmp(users[i].username, username) == 0 &&
        strcmp(users[i].password, password) == 0) {
      return 1;
    }
  }
  return 0;
}

// Private message handler
void private_message(char *to_user, char *from_user, char *message) {
  pthread_mutex_lock(&lock);
  for (int i = 0; i < 100; i++) {
    if (clients[i] && clients[i]->authenticated &&
        strcmp(clients[i]->username, to_user) == 0) {
      char formatted[BUFFER_SIZE];
      snprintf(formatted, sizeof(formatted), "MSG %s %s\n", from_user, message);

      if (send(clients[i]->socket, formatted, strlen(formatted), 0) < 0) {
        perror("Send failed");
      }
      break;
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
        printf("[Thread %lu]: Client %s disconnected\n", pthread_self(),
               client->username);
      } else {
        perror("recv failed");
      }
      break;
    }

    buffer[bytes_rcved] = '\0';

    char command[BUFFER_SIZE], arg1[BUFFER_SIZE], arg2[BUFFER_SIZE];
    sscanf(buffer, "%s %s %[^\n]", command, arg1, arg2);

    printf("$ %s %s %s\n", command, arg1, arg2);

    if (strcmp(command, "LOGIN") == 0) {
      if (authenticate(arg1, arg2)) {
        printf("YES\n");
        strcpy(client->username, arg1);
        client->authenticated = 1;
        if (send(client->socket, "AUTH_OK\n", 8, 0) < 0) {
          printf("send failed\n");
        }
      } else {
        printf("NO\n");
        if (send(client->socket, "AUTH_FAIL\n", 10, 0) < 0) {
          printf("send failed\n");
        }
      }
    } else if (strcmp(command, "BROADCAST") == 0) {
      if (!client->authenticated) {
        if (send(client->socket, "ERROR Not authenticated\n", 25, 0) < 0) {
          printf("Send failed\n");
        }
        continue;
      }
      broadcast(arg1, client->username);
    } else if (strcmp(command, "PRIVATE") == 0) {
      if (!client->authenticated) {
        if (send(client->socket, "ERROR Not authenticated\n", 25, 0) < 0) {
          printf("Send failed\n");
        }
        continue;
      }
      private_message(arg1, client->username, arg2);
    } else if (strcmp(command, "LIST") == 0) {
      char response[BUFFER_SIZE] = "ONLINE ";
      pthread_mutex_lock(&lock);
      for (int i = 0; i < 100; i++) {
        if (clients[i] && clients[i]->authenticated) {
          strcat(response, clients[i]->username);
          strcat(response, " ");
        }
      }
      pthread_mutex_unlock(&lock);
      strcat(response, "\n");
      if (send(client->socket, response, strlen(response), 0) < 0) {
        printf("send Failed\n");
      }
    } else {
      printf("Invalid Command\n");
    }
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
