#include "../monitoring/monitor.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define PORT 8000
#define HISTORY_FILE "../logs/history.json"

typedef struct
{
  int socket;
  struct sockaddr_in address;
  char username[32];
  int authenticated;
  char status[16];
} client_t;

typedef struct
{
  char *to;
  char *from;
  char *msg;
  char *msgType;
  char timestamp[32];
} HistItem;

client_t *clients[100];

HistItem *history = NULL;
size_t history_len = 0;
size_t history_cap = 0;

void add_client(client_t *client)
{
  for (int i = 0; i < 100; i++)
  {
    if (!clients[i])
    {
      clients[i] = client;
      break;
    }
  }
}

void remove_client(int socket)
{
  for (int i = 0; i < 100; i++)
  {
    if (clients[i] && clients[i]->socket == socket)
    {
      clients[i] = NULL;
      break;
    }
  }
}

static void write_history_json(const char *msgType, const char *from,
                               const char *to, const char *msg,
                               const char *timestamp)
{
  FILE *f = fopen(HISTORY_FILE, "a");
  if (!f)
    return;
  char safe[BUFFER_SIZE * 2];
  int di = 0;
  for (int si = 0; msg[si] && di < (int)sizeof(safe) - 2; si++)
  {
    if (msg[si] == '"' || msg[si] == '\\')
      safe[di++] = '\\';
    safe[di++] = msg[si];
  }
  safe[di] = '\0';
  fprintf(f,
          "{\"type\":\"%s\",\"from\":\"%s\",\"to\":\"%s\","
          "\"msg\":\"%s\",\"timestamp\":\"%s\"}\n",
          msgType, from, to, safe, timestamp);
  fclose(f);
}

static void add_history(const char *msgType, const char *from,
                        const char *to, const char *msg)
{
  if (history_len == history_cap)
  {
    history_cap = history_cap ? history_cap * 2 : 256;
    history = realloc(history, history_cap * sizeof(*history));
  }
  HistItem *item = &history[history_len++];
  item->msgType = strdup(msgType);
  item->from = strdup(from);
  item->to = strdup(to);
  item->msg = strdup(msg);
  time_t t = time(NULL);
  strftime(item->timestamp, sizeof(item->timestamp),
           "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
  write_history_json(msgType, from, to, msg, item->timestamp);
}

static void send_history(int client_index)
{
  const char *username = clients[client_index]->username;
  char buf[2048];
  for (size_t i = 0; i < history_len; i++)
  {
    if (strcmp(history[i].from, username) == 0 ||
        strcmp(history[i].to, username) == 0)
    {
      snprintf(buf, sizeof(buf), "[%s]   [%s] %s -> %s: %s\n",
               history[i].timestamp, history[i].msgType,
               history[i].from, history[i].to, history[i].msg);
      if (send(clients[client_index]->socket, buf, strlen(buf), 0) < 0)
        perror("send history failed");
    }
  }
}

void broadcast(char *message, char *from_user, long long timestamp)
{
  for (int i = 0; i < 100; i++)
  {
    if (clients[i] && clients[i]->authenticated)
    {
      char formatted[BUFFER_SIZE];
      snprintf(formatted, sizeof(formatted), "%lld BROADCAST: %s %s\n",
               timestamp, from_user, message);
      add_history("BROADCAST", from_user, clients[i]->username, message);
      if (send(clients[i]->socket, formatted, strlen(formatted), 0) < 0)
        perror("Broadcast failed");
    }
  }
}

void private_message(char *to_user, char *from_user, char *message,
                     long long timestamp)
{
  for (int i = 0; i < 100; i++)
  {
    if (clients[i] && clients[i]->authenticated &&
        strcmp(clients[i]->username, to_user) == 0)
    {
      char formatted[BUFFER_SIZE];
      snprintf(formatted, sizeof(formatted), "%lld MSG %s %s\n",
               timestamp, from_user, message);
      add_history("PRIVATE", from_user, clients[i]->username, message);
      if (send(clients[i]->socket, formatted, strlen(formatted), 0) < 0)
        perror("Send failed");
      break;
    }
  }
}

void accept_new_client(int server_socket)
{
  int client_socket;
  struct sockaddr_in client_address;
  socklen_t addrlen = sizeof(client_address);

  client_socket =
      accept(server_socket, (struct sockaddr *)&client_address, &addrlen);
  if (client_socket < 0)
    return;

  for (int i = 0; i < 100; i++)
  {
    if (!clients[i])
    {
      clients[i] = malloc(sizeof(client_t));
      clients[i]->socket = client_socket;
      clients[i]->authenticated = 0;
      clients[i]->username[0] = '\0';
      strncpy(clients[i]->status, "available",
              sizeof(clients[i]->status) - 1); // NEW
      break;
    }
  }
  printf("New connection on socket %d\n", client_socket);
}

void parse_and_route(int index, char *buffer)
{
  char command[BUFFER_SIZE], arg1[BUFFER_SIZE], arg2[BUFFER_SIZE];
  long long timestamp;
  arg1[0] = arg2[0] = '\0';
  sscanf(buffer, "%lld %s %s %[^\n]", &timestamp, command, arg1, arg2);
  printf("$ %lld %s %s %s\n", timestamp, command, arg1, arg2);

  if (strcmp(command, "LOGIN") == 0)
  {
    clients[index]->authenticated = 1;
    strncpy(clients[index]->username, arg1,
            sizeof(clients[index]->username) - 1);
    strncpy(clients[index]->status, "available",
            sizeof(clients[index]->status) - 1);
    if (send(clients[index]->socket, "LOGIN_SUCCESS\n", 14, 0) < 0)
      printf("Send failed\n");
  }
  else if (strcmp(command, "BROADCAST") == 0)
  {
    if (!clients[index]->authenticated)
    {
      send(clients[index]->socket, "ERROR Not authenticated\n", 25, 0);
      return;
    }
    broadcast(arg1, clients[index]->username, timestamp);
  }
  else if (strcmp(command, "PRIVATE") == 0)
  {
    if (!clients[index]->authenticated)
    {
      send(clients[index]->socket, "ERROR Not authenticated\n", 25, 0);
      return;
    }
    private_message(arg1, clients[index]->username, arg2, timestamp);
  }
  else if (strcmp(command, "LIST") == 0)
  {
    char response[BUFFER_SIZE] = "ONLINE";
    for (int i = 0; i < 100; i++)
    {
      if (clients[i] && clients[i]->authenticated)
      {
        char entry[64];
        snprintf(entry, sizeof(entry), " %s(%s)",
                 clients[i]->username, clients[i]->status);
        strncat(response, entry, sizeof(response) - strlen(response) - 1);
      }
    }
    strncat(response, "\n", sizeof(response) - strlen(response) - 1);
    if (send(clients[index]->socket, response, strlen(response), 0) < 0)
      printf("send Failed\n");
  }
  else if (strcmp(command, "HISTORY") == 0)
  {
    if (!clients[index]->authenticated)
    {
      send(clients[index]->socket, "ERROR Not authenticated\n", 25, 0);
      return;
    }
    send_history(index);
  }
  else if (strcmp(command, "STATUS") == 0)
  {
    if (!clients[index]->authenticated)
    {
      send(clients[index]->socket, "ERROR Not authenticated\n", 25, 0);
      return;
    }
    if (strcmp(arg1, "available") != 0 &&
        strcmp(arg1, "busy") != 0 &&
        strcmp(arg1, "away") != 0)
    {
      const char *err = "ERROR Status must be available, busy, or away\n";
      send(clients[index]->socket, err, strlen(err), 0);
      return;
    }
    strncpy(clients[index]->status, arg1,
            sizeof(clients[index]->status) - 1);
    char ok[64];
    snprintf(ok, sizeof(ok), "STATUS_OK %s\n", clients[index]->status);
    send(clients[index]->socket, ok, strlen(ok), 0);
    printf("%s changed status to %s\n",
           clients[index]->username, clients[index]->status);
  }
  else
  {
    printf("Invalid Command\n");
  }
}

void handle_client_message(int i)
{
  char buffer[BUFFER_SIZE];
  int bytes = recv(clients[i]->socket, buffer, BUFFER_SIZE - 1, 0);
  if (bytes <= 0)
  {
    printf("Client %s disconnected (Socket %d)\n",
           clients[i]->username, clients[i]->socket);
    close(clients[i]->socket);
    free(clients[i]);
    clients[i] = NULL;
    return;
  }
  buffer[bytes] = '\0';
  char *line = strtok(buffer, "\n");
  while (line != NULL)
  {
    if (strlen(line) > 0)
      parse_and_route(i, line);
    line = strtok(NULL, "\n");
  }
}

int main()
{
  start_monitor("./logs/metrics_select.txt");
  int server_socket;
  struct sockaddr_in server_address;

  server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket < 0)
  {
    perror("Socket creation failed");
    exit(EXIT_FAILURE);
  }

  int opt = 1;
  setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(PORT);

  if (bind(server_socket, (struct sockaddr *)&server_address,
           sizeof(server_address)) < 0)
  {
    perror("Bind failed");
    exit(EXIT_FAILURE);
  }
  if (listen(server_socket, 10) < 0)
  {
    perror("Listen failed");
    exit(EXIT_FAILURE);
  }

  printf("Select-based server listening on port %d\n", PORT);

  fd_set readfds;
  int max_fd;

  while (1)
  {
    FD_ZERO(&readfds);
    FD_SET(server_socket, &readfds);
    max_fd = server_socket;

    for (int i = 0; i < 100; i++)
    {
      if (clients[i] && clients[i]->socket != -1)
      {
        FD_SET(clients[i]->socket, &readfds);
        if (clients[i]->socket > max_fd)
          max_fd = clients[i]->socket;
      }
    }

    select(max_fd + 1, &readfds, NULL, NULL, NULL);

    if (FD_ISSET(server_socket, &readfds))
      accept_new_client(server_socket);

    for (int i = 0; i < 100; i++)
    {
      if (clients[i] && clients[i]->socket != -1 &&
          FD_ISSET(clients[i]->socket, &readfds))
        handle_client_message(i);
    }
  }

  close(server_socket);
  return 0;
}