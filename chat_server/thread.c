#include "../monitoring/monitor.h"
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

HistItem *history = NULL;
size_t history_len = 0;
size_t history_cap = 0;

client_t *clients[100];
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void add_client(client_t *client)
{
  pthread_mutex_lock(&lock);
  for (int i = 0; i < 100; i++)
  {
    if (!clients[i])
    {
      clients[i] = client;
      break;
    }
  }
  pthread_mutex_unlock(&lock);
}

void remove_client(int socket)
{
  pthread_mutex_lock(&lock);
  for (int i = 0; i < 100; i++)
  {
    if (clients[i] && clients[i]->socket == socket)
    {
      clients[i] = NULL;
      break;
    }
  }
  pthread_mutex_unlock(&lock);
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

static void add_history_locked(const char *msgType, const char *from,
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

void send_history(const char *client)
{
  pthread_mutex_lock(&lock);

  for (int j = 0; j < 100; j++)
  {
    if (clients[j] && strcmp(clients[j]->username, client) == 0 &&
        clients[j]->authenticated)
    {
      char buf[2048];
      for (size_t i = 0; i < history_len; i++)
      {

        if (strcmp(history[i].from, client) == 0 ||
            strcmp(history[i].to, client) == 0)
        {
          snprintf(buf, sizeof(buf), "[%s]   [%s] %s -> %s: %s\n",
                   history[i].timestamp, history[i].msgType,
                   history[i].from, history[i].to, history[i].msg);
          if (send(clients[j]->socket, buf, strlen(buf), 0) < 0)
            perror("send history failed");
        }
      }
      break;
    }
  }
  pthread_mutex_unlock(&lock);
}

void broadcast(char *message, char *from_user, long long timestamp)
{
  pthread_mutex_lock(&lock);
  for (int i = 0; i < 100; i++)
  {
    if (clients[i] && clients[i]->authenticated)
    {
      char formatted[BUFFER_SIZE];
      snprintf(formatted, sizeof(formatted), "%lld BROADCAST: %s %s\n",
               timestamp, from_user, message);
      add_history_locked("BROADCAST", from_user, clients[i]->username, message);
      if (send(clients[i]->socket, formatted, strlen(formatted), 0) < 0)
        perror("Broadcast failed");
    }
  }
  pthread_mutex_unlock(&lock);
}

void private_message(char *to_user, char *from_user, char *message,
                     long long timestamp)
{
  pthread_mutex_lock(&lock);
  for (int i = 0; i < 100; i++)
  {
    if (clients[i] && clients[i]->authenticated &&
        strcmp(clients[i]->username, to_user) == 0)
    {
      char formatted[BUFFER_SIZE];
      snprintf(formatted, sizeof(formatted), "%lld MSG %s %s\n",
               timestamp, from_user, message);
      add_history_locked("PRIVATE", from_user, clients[i]->username, message);
      if (send(clients[i]->socket, formatted, strlen(formatted), 0) < 0)
        perror("Send failed");
      break;
    }
  }
  pthread_mutex_unlock(&lock);
}

void *handle_client(void *arg)
{
  client_t *client = (client_t *)arg;
  char buffer[BUFFER_SIZE];
  int bytes_rcved;

  add_client(client);

  while (1)
  {
    bytes_rcved = recv(client->socket, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_rcved <= 0)
    {
      if (bytes_rcved == 0)
        printf("[Thread %lu]: Client %s disconnected\n",
               pthread_self(), client->username);
      else
        perror("recv failed");
      break;
    }
    buffer[bytes_rcved] = '\0';

    char command[BUFFER_SIZE], arg1[BUFFER_SIZE], arg2[BUFFER_SIZE];
    long long timestamp;
    arg1[0] = arg2[0] = '\0';
    sscanf(buffer, "%lld %s %s %[^\n]", &timestamp, command, arg1, arg2);
    printf("$ %lld %s %s %s\n", timestamp, command, arg1, arg2);

    if (strcmp(command, "LOGIN") == 0)
    {
      client->authenticated = 1;
      strncpy(client->username, arg1, sizeof(client->username) - 1);
      strncpy(client->status, "available", sizeof(client->status) - 1);
      if (send(client->socket, "LOGIN_SUCCESS\n", 14, 0) < 0)
        printf("Send failed\n");
    }
    else if (strcmp(command, "BROADCAST") == 0)
    {
      if (!client->authenticated)
      {
        send(client->socket, "ERROR Not authenticated\n", 25, 0);
        continue;
      }
      broadcast(arg1, client->username, timestamp);
    }
    else if (strcmp(command, "PRIVATE") == 0)
    {
      if (!client->authenticated)
      {
        send(client->socket, "ERROR Not authenticated\n", 25, 0);
        continue;
      }
      private_message(arg1, client->username, arg2, timestamp);
    }
    else if (strcmp(command, "LIST") == 0)
    {
      char response[BUFFER_SIZE] = "ONLINE";
      pthread_mutex_lock(&lock);
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
      pthread_mutex_unlock(&lock);
      strncat(response, "\n", sizeof(response) - strlen(response) - 1);
      if (send(client->socket, response, strlen(response), 0) < 0)
        printf("send Failed\n");
    }
    else if (strcmp(command, "HISTORY") == 0)
    {
      if (!client->authenticated)
      {
        send(client->socket, "ERROR Not authenticated\n", 25, 0);
        continue;
      }
      send_history(client->username);
    }
    else if (strcmp(command, "STATUS") == 0)
    {
      if (!client->authenticated)
      {
        send(client->socket, "ERROR Not authenticated\n", 25, 0);
        continue;
      }
      if (strcmp(arg1, "available") != 0 &&
          strcmp(arg1, "busy") != 0 &&
          strcmp(arg1, "away") != 0)
      {
        const char *err = "ERROR Status must be available, busy, or away\n";
        send(client->socket, err, strlen(err), 0);
        continue;
      }
      strncpy(client->status, arg1, sizeof(client->status) - 1);
      char ok[64];
      snprintf(ok, sizeof(ok), "STATUS_OK %s\n", client->status);
      send(client->socket, ok, strlen(ok), 0);
      printf("[Thread %lu]: %s changed status to %s\n",
             pthread_self(), client->username, client->status);
    }
    else
    {
      printf("Invalid Command\n");
    }
  }

  close(client->socket);
  remove_client(client->socket);
  free(client);
  return NULL;
}

int main()
{
  start_monitor("./logs/metrics_thread.txt");
  int server_socket, client_socket;
  struct sockaddr_in server_address, client_address;
  socklen_t client_address_len;
  pthread_t thread_id;

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

  printf("Thread-based server listening on port %d\n", PORT);

  while (1)
  {
    client_address_len = sizeof(client_address);
    client_socket = accept(server_socket, (struct sockaddr *)&client_address,
                           &client_address_len);
    if (client_socket < 0)
    {
      perror("Accept failed");
      continue;
    }

    printf("New connection from %s:%d\n",
           inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));

    client_t *client = malloc(sizeof(client_t));
    if (!client)
    {
      perror("malloc failed");
      close(client_socket);
      continue;
    }

    client->socket = client_socket;
    client->address = client_address;
    client->authenticated = 0;
    client->username[0] = '\0';
    strncpy(client->status, "available", sizeof(client->status) - 1);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    if (pthread_create(&thread_id, &attr, handle_client, client) != 0)
    {
      perror("Thread creation failed");
      free(client);
      close(client_socket);
    }
    else
    {
      printf("Created thread (ID: %lu) to handle client\n\n", thread_id);
    }
    pthread_attr_destroy(&attr);
  }

  close(server_socket);
  return 0;
}