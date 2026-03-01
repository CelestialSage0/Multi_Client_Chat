#include <arpa/inet.h>
#include <netinet/in.h>
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

// Add client handler
void add_client(client_t *client) {
  for (int i = 0; i < 100; i++) {
    if (!clients[i]) {
      clients[i] = client;
      break;
    }
  }
}

// Remove client handler
void remove_client(int socket) {
  for (int i = 0; i < 100; i++) {
    if (clients[i] && clients[i]->socket == socket) {
      clients[i] = NULL;
      break;
    }
  }
}

// Broadcast handler
void broadcast(char *message, char *from_user) {
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
}

void accept_new_client(int server_socket) {
  int client_socket;
  struct sockaddr_in client_address;
  socklen_t addrlen = sizeof(client_address);

  client_socket =
      accept(server_socket, (struct sockaddr *)&client_address, &addrlen);

  if (client_socket < 0)
    return;

  for (int i = 0; i < 100; i++) {
    if (!clients[i]) {
      clients[i] = malloc(sizeof(client_t));
      clients[i]->socket = client_socket;
      clients[i]->authenticated = 0;
      clients[i]->username[0] = '\0';
      break;
    }
  }

  printf("New connection on socket %d\n", client_socket);
}

void parse_and_route(int index, char *buffer) {
  char command[32];
  char arg1[BUFFER_SIZE];
  char arg2[BUFFER_SIZE];

  sscanf(buffer, "%s %s %[^\n]", command, arg1, arg2);

  printf("$ cmd:%s arg1:%s arg2:%s\n", command, arg1, arg2);

  if (strcmp(command, "LOGIN") == 0) {
    if (authenticate(arg1, arg2)) {
      strcpy(clients[index]->username, arg1);
      clients[index]->authenticated = 1;
      send(clients[index]->socket, "AUTH_OK\n", 8, 0);
    } else {
      send(clients[index]->socket, "AUTH_FAIL\n", 10, 0);
    }
  } else if (strcmp(command, "BROADCAST") == 0) {
    if (!clients[index]->authenticated) {
      if (send(clients[index]->socket, "ERROR Not authenticated\n", 25, 0) <
          0) {
        printf("Send failed\n");
      }
      return;
    }
    broadcast(arg1, clients[index]->username);
  } else if (strcmp(command, "PRIVATE") == 0) {
    private_message(arg1, clients[index]->username, arg2);
  } else if (strcmp(command, "LIST") == 0) {
    char response[BUFFER_SIZE] = "ONLINE ";
    for (int i = 0; i < 100; i++) {
      if (clients[i] && clients[i]->authenticated) {
        strcat(response, clients[i]->username);
        strcat(response, " ");
      }
    }
    strcat(response, "\n");
    if (send(clients[index]->socket, response, strlen(response), 0) < 0) {
      printf("send Failed\n");
    }
    printf("\n");
  }
}

void handle_client_message(int i) {
  char buffer[BUFFER_SIZE];

  int bytes = recv(clients[i]->socket, buffer, BUFFER_SIZE - 1, 0);

  if (bytes <= 0) {
    printf("Client %s disconnected (Socket %d)\n", clients[i]->username,
           clients[i]->socket);
    close(clients[i]->socket);
    free(clients[i]);
    clients[i] = NULL;
    return;
  }

  buffer[bytes] = '\0';

  // Handle multiple commands
  char *line = strtok(buffer, "\n");

  while (line != NULL) {
    if (strlen(line) > 0)
      parse_and_route(i, line);

    line = strtok(NULL, "\n");
  }
}

int main() {
  int server_socket, client_socket;
  struct sockaddr_in server_address, client_address;
  socklen_t client_address_len;

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

  printf("Select-based server listening on port %d\n", PORT);

  fd_set readfds;
  int max_fd;

  while (1) {
    FD_ZERO(&readfds);

    // Monitor server socket
    FD_SET(server_socket, &readfds);
    max_fd = server_socket;

    // Monitor all client sockets
    for (int i = 0; i < 100; i++) {
      if (clients[i] && clients[i]->socket != -1) {
        FD_SET(clients[i]->socket, &readfds);

        if (clients[i]->socket > max_fd)
          max_fd = clients[i]->socket;
      }
    }

    select(max_fd + 1, &readfds, NULL, NULL, NULL);

    // 1️⃣ New connection
    if (FD_ISSET(server_socket, &readfds)) {
      accept_new_client(server_socket);
    }

    // 2️⃣ Existing clients
    for (int i = 0; i < 100; i++) {
      if (clients[i] && clients[i]->socket != -1 &&
          FD_ISSET(clients[i]->socket, &readfds)) {
        handle_client_message(i);
      }
    }
  }

  close(server_socket);
  return 0;
}
