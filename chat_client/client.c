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
    long long send_time;
    char *text_part;
    char *endptr;

    // 1. Extract the number (base 10)
    // endptr will point to the character immediately after the number
    send_time = strtoll(buffer, &endptr, 10);
    while (*endptr == ' ') {
      endptr++;
    }
    text_part = endptr;
    printf("%s", text_part);
    if (strncmp(text_part, "MSG", 3) == 0 ||
        strncmp(text_part, "BROADCAST", 9) == 0) {
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);

      long long recv_time = ts.tv_sec * 1000000000LL + ts.tv_nsec;

      double latency_ms = (recv_time - send_time) / 1e6;
      printf("send_time: %lld\n", send_time);
      printf("recv_time: %lld\n", recv_time);

      printf("Latency: %.3f ms\n", latency_ms);

      FILE *lat_file = fopen("../logs/latency_thread.txt", "a");
      if (!lat_file) {
        perror("fopen failed");
        exit(1);
      }
      fprintf(lat_file, "%.3f\n", latency_ms);
      fflush(lat_file);
    }
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
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);

  long long send_time = ts.tv_sec * 1000000000LL + ts.tv_nsec;
  if (flag == 2)
    sprintf(buffer, "%lld AUTH %s %s\n", send_time, username, password);
  else
    sprintf(buffer, "%lld REGISTER %s %s\n", send_time, username, password);
  if (send(disc_sock, buffer, strlen(buffer), 0) < 0) {
    printf("Send failed\n");
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

  char login_msg[100];
  clock_gettime(CLOCK_MONOTONIC, &ts);
  send_time = ts.tv_sec * 1000000000LL + ts.tv_nsec;

  sprintf(login_msg, "%lld LOGIN %s\n", send_time, username);
  if (send(client_socket, login_msg, strlen(login_msg), 0) < 0) {
    printf("Send failed\n");
  }

  pthread_t recv_thread;
  pthread_create(&recv_thread, NULL, receive_handler, &client_socket);

  while (fgets(buffer, BUFFER_SIZE, stdin) != NULL) {

    // Remove trailing newline
    buffer[strcspn(buffer, "\n")] = '\0';

    // Get timestamp AFTER reading input
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    long long send_time = ts.tv_sec * 1000000000LL + ts.tv_nsec;

    char new_buffer[BUFFER_SIZE];

    // Prepend timestamp
    snprintf(new_buffer, BUFFER_SIZE, "%lld %s", send_time, buffer);

    if (send(client_socket, new_buffer, strlen(new_buffer), 0) < 0) {

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
