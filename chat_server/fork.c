#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define PORT 8000

typedef struct {
  int socket;                 // client socket
  struct sockaddr_in address; // client address
  char username[32];
  int authenticated; // 1->yes; 0->no
  int pipe_fd;
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
  char formatted[BUFFER_SIZE];

  snprintf(formatted, sizeof(formatted), "BROADCAST: %s %s\n", from_user,
           message);

  for (int j = 0; j < 100; j++) {
    if (clients[j] && clients[j]->authenticated && clients[j]->socket != -1) {
      send(clients[j]->socket, formatted, strlen(formatted), 0);
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

void accept_new_connection(int server_socket) {
  int client_socket;
  struct sockaddr_in client_address;
  socklen_t addrlen = sizeof(client_address);

  client_socket =
      accept(server_socket, (struct sockaddr *)&client_address, &addrlen);

  if (client_socket < 0) {
    perror("accept failed");
    return;
  }

  int pipefd[2];
  pipe(pipefd);

  pid_t pid;

  pid = fork();
  if (pid < 0) {
    // Fork failed
    perror("Fork failed");
    close(client_socket);
  } else if (pid == 0) {
    // Child process
    close(pipefd[0]);
    close(server_socket); // Child doesn't need the listening socket

    char buffer[BUFFER_SIZE];
    int bytes;

    while ((bytes = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0) {
      write(pipefd[1], buffer, bytes);
    }

    close(client_socket);
    close(pipefd[1]);
    exit(0);
  } else {
    // Parent process
    close(pipefd[1]); // Parent does not need the client socket
    printf("Forked child process (PID: %d) to handle client\n\n", pid);
    // store client in clients[]
    for (int i = 0; i < 100; i++) {
      if (clients[i]->socket == -1) {
        clients[i]->socket = client_socket;
        clients[i]->pipe_fd = pipefd[0];
        clients[i]->authenticated = 0;
        break;
      }
    }
  }
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

void handle_pipe_message(int index) {
  char buffer[BUFFER_SIZE];

  int bytes = read(clients[index]->pipe_fd, buffer, BUFFER_SIZE - 1);

  if (bytes <= 0) {
    // Client disconnected
    printf("[PID %d]: Client %s disconnected\n", getpid(),
           clients[index]->username);
    close(clients[index]->socket);
    close(clients[index]->pipe_fd);
    clients[index]->socket = -1;
    return;
  }

  buffer[bytes] = '\0';

  if (strlen(buffer) <= 1)
    return;

  char *line = strtok(buffer, "\n");

  while (line != NULL) {
    if (strlen(line) > 0) {
      parse_and_route(index, line);
    }

    line = strtok(NULL, "\n");
  }
}

int main() {
  signal(SIGCHLD, SIG_IGN);
  int server_socket, client_socket;
  struct sockaddr_in server_address, client_address;
  socklen_t client_address_len;
  pid_t pid;

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

  printf("Fork-based server listening on port %d\n", PORT);

  fd_set readfds;
  int max_fd;
  // for (int i = 0; i < 100; i++) {
  //   clients[i]->socket = -1;
  // }
  for (int i = 0; i < 100; i++) {
    clients[i] = malloc(sizeof(client_t)); // Allocate memory for the pointer
    if (clients[i] == NULL) {
      perror("malloc failed");
      exit(EXIT_FAILURE);
    }
    clients[i]->socket = -1;
    clients[i]->pipe_fd = -1;
    clients[i]->authenticated = 0;
    memset(clients[i]->username, 0, 32);
  }

  while (1) {
    FD_ZERO(&readfds);

    // 1️⃣ Monitor server socket (for new connections)
    FD_SET(server_socket, &readfds);
    max_fd = server_socket;

    // 2️⃣ Monitor all client pipes
    for (int i = 0; i < 100; i++) {
      if (clients[i]->socket != -1) {
        FD_SET(clients[i]->pipe_fd, &readfds);

        if (clients[i]->pipe_fd > max_fd)
          max_fd = clients[i]->pipe_fd;
      }
    }

    // 3️⃣ Wait for activity
    int activity = select(max_fd + 1, &readfds, NULL, NULL, NULL);

    if (activity < 0) {
      perror("select error");
      continue;
    }

    // 4️⃣ Check new connection
    if (FD_ISSET(server_socket, &readfds)) {
      accept_new_connection(server_socket);
    }

    // 5️⃣ Check client pipes
    for (int i = 0; i < 100; i++) {
      if (clients[i]->socket != -1 && FD_ISSET(clients[i]->pipe_fd, &readfds)) {
        handle_pipe_message(i);
      }
    }
  }

  close(server_socket);
  return 0;
}
