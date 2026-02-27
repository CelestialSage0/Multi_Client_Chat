#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SERVER_IP "127.0.0.1" // Loopback
#define PORT 8000
#define BUFFER_SIZE 1024

// recieve handler
void *receive_handler(void *arg) {
  int client_socket = *((int *)arg);
  char buffer[1024];

  while (1) {
    int bytes = recv(client_socket, buffer, sizeof(buffer), 0);
    if (bytes <= 0)
      break;

    buffer[bytes] = '\0';
    printf("%s", buffer);
  }
  return NULL;
}

int main() {
  int client_socket;
  struct sockaddr_in server_address;
  char buffer[BUFFER_SIZE];
  int bytes_received;

  // Creating socket
  client_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (client_socket < 0) {
    perror("Socket creation failed");
    exit(EXIT_FAILURE);
  }

  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(PORT);

  // Convert IP address from text to binary form
  if (inet_pton(AF_INET, SERVER_IP, &server_address.sin_addr) <= 0) {
    perror("Invalid address");
    exit(EXIT_FAILURE);
  }

  if (connect(client_socket, (struct sockaddr *)&server_address,
              sizeof(server_address)) < 0) {
    perror("Connection failed");
    exit(EXIT_FAILURE);
  }

  pthread_t recv_thread;
  pthread_create(&recv_thread, NULL, receive_handler, &client_socket);

  while (fgets(buffer, BUFFER_SIZE, stdin) != NULL) {
    if (send(client_socket, buffer, strlen(buffer), 0) < 0) {
      perror("Send failed");
      break;
    }

    bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received <= 0) {
      printf("Server disconnected\n");
      break;
    }

    buffer[bytes_received] = '\0';
  }

  close(client_socket);
  printf("\nDisconnected from server\n");
  return 0;
}
