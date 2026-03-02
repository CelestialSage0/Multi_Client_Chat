#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 8080
#define BUFFER_SIZE 1024

typedef struct {
  char username[50];
  char password[50];
} user_entry;

user_entry users[100];
int user_count = 0;

int find_user(char *username) {
  for (int i = 0; i < user_count; i++) {
    if (strcmp(users[i].username, username) == 0)
      return i;
  }
  return -1;
}

int main() {
  int server_socket, client_sockets[100];
  fd_set readfds;
  int max_fd;

  for (int i = 0; i < 100; i++)
    client_sockets[i] = -1;

  server_socket = socket(AF_INET, SOCK_STREAM, 0);

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(PORT);

  if (bind(server_socket, (struct sockaddr *)&server_addr,
           sizeof(server_addr)) < 0) {
    printf("Bind failed\n");
  }

  listen(server_socket, 5);

  printf("Discovery server running on port %d\n", PORT);

  while (1) {
    FD_ZERO(&readfds);
    FD_SET(server_socket, &readfds);
    max_fd = server_socket;

    for (int i = 0; i < 100; i++) {
      if (client_sockets[i] != -1) {
        FD_SET(client_sockets[i], &readfds);
        if (client_sockets[i] > max_fd)
          max_fd = client_sockets[i];
      }
    }

    select(max_fd + 1, &readfds, NULL, NULL, NULL);

    // New connection
    if (FD_ISSET(server_socket, &readfds)) {
      int new_socket = accept(server_socket, NULL, NULL);

      for (int i = 0; i < 100; i++) {
        if (client_sockets[i] == -1) {
          client_sockets[i] = new_socket;
          break;
        }
      }
    }

    // Handle client messages
    for (int i = 0; i < 100; i++) {
      if (client_sockets[i] != -1 && FD_ISSET(client_sockets[i], &readfds)) {

        char buffer[BUFFER_SIZE];
        int bytes = recv(client_sockets[i], buffer, BUFFER_SIZE - 1, 0);

        if (bytes <= 0) {
          close(client_sockets[i]);
          client_sockets[i] = -1;
          continue;
        }

        buffer[bytes] = '\0';

        char command[20], username[50], password[50];
        sscanf(buffer, "%s %s %s", command, username, password);

        if (strcmp(command, "REGISTER") == 0) {

          if (find_user(username) != -1) {
            send(client_sockets[i], "USER_EXISTS\n", 12, 0);
          } else {
            strcpy(users[user_count].username, username);
            strcpy(users[user_count].password, password);
            user_count++;

            send(client_sockets[i], "REGISTERED\n", 11, 0);
          }
        }

        else if (strcmp(command, "AUTH") == 0) {

          int idx = find_user(username);

          if (idx != -1 && strcmp(users[idx].password, password) == 0) {

            send(client_sockets[i], "AUTH_SUCCESS\n", 13, 0);
          } else {
            send(client_sockets[i], "AUTH_FAILED\n", 12, 0);
          }
        }

        else {
          send(client_sockets[i], "Invalid command\n",
               sizeof("Invalid command\n"), 0);
        }
      }
    }
  }
}
