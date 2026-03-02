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
    if (bytes <= 0) {
      printf("Recv failed\n");
      break;
    }

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

  printf("Welcome\n1.Register\n2.Login\n");
  int flag;
  scanf("%d", &flag);

  char username[50];
  char password[50];

  printf("Username: ");
  scanf("%s", username);

  printf("Password: ");
  scanf("%s", password);

  // Connecting to discovery server
  int disc_sock = socket(AF_INET, SOCK_STREAM, 0);

  struct sockaddr_in disc_addr;
  disc_addr.sin_family = AF_INET;
  disc_addr.sin_port = htons(8080);
  disc_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  if (connect(disc_sock, (struct sockaddr *)&disc_addr, sizeof(disc_addr)) <
      0) {
    perror("Connection failed");
    exit(EXIT_FAILURE);
  }
  if (flag == 2)
    sprintf(buffer, "AUTH %s %s\n", username, password);
  else
    sprintf(buffer, "REGISTER %s %s\n", username, password);

  if (send(disc_sock, buffer, strlen(buffer), 0) < 0) {
    printf("Send failed\n");
  }
  // Creating socket
  client_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (client_socket < 0) {
    perror("Socket creation failed");
    exit(EXIT_FAILURE);
  }
  int bytes = recv(disc_sock, buffer, sizeof(buffer) - 1, 0);
  buffer[bytes] = '\0';

  if (flag == 2) {
    if (strncmp(buffer, "AUTH_SUCCESS", 12) != 0) {
      printf("Authentication failed\n");
      close(disc_sock);
      exit(0);
    }
  } else {
    printf("%s\n", buffer);
  }
  close(disc_sock);

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

  char login_msg[100];
  sprintf(login_msg, "LOGIN %s\n", username);
  if (send(client_socket, login_msg, strlen(login_msg), 0) < 0) {
    printf("Send failed\n");
  }

  pthread_t recv_thread;
  pthread_create(&recv_thread, NULL, receive_handler, &client_socket);

  while (fgets(buffer, BUFFER_SIZE, stdin) != NULL) {
    if (send(client_socket, buffer, strlen(buffer), 0) < 0) {
      perror("Send failed");
      break;
    }

    // bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    // if (bytes_received <= 0) {
    //   printf("Server disconnected\n");
    //   break;
    // }

    // buffer[bytes_received] = '\0';
  }

  close(client_socket);
  printf("\nDisconnected from server\n");
  return 0;
}
