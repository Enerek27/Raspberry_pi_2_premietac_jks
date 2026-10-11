
#include "wifi.h"
#include <asm-generic/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>





int uart_read_line(int fd, char *line, int max_len) {
  int pos = 0;
  char c;

  while (pos < max_len - 1) {
    int n = read(fd, &c, 1);
    if (n < 0) {
      perror("Chyba čítania UART");
      return -1;
    }
    if (c == '\n') {
      break;
    }
    if (c != '\r') {
      line[pos++] = c;
    }
  }

  line[pos] = '\0';
  return pos;
}

bool navis_chladnicku(Wifi_chladnicka_t *ch) {
  int navis = ch->max_znakov + 200;
  char *temp = realloc(ch->nacitane, navis * sizeof(char));
  if (temp == NULL) {
    perror("Chyba zvysenia priestoru pre chladnicku");
    return false;
  }
  ch->nacitane = temp;
  ch->max_znakov = navis;
  return true;
}

void *wifi_worker(void *arg) {
  Wifi_chladnicka_t *chladnicka = (Wifi_chladnicka_t *)arg;


  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("[WiFi] chyba vytvorenie socketu");
    atomic_store(&chladnicka->pracuj, false);
    pthread_exit(NULL);
  }

  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in address;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(WIFI_PORT);

  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("[wifi] chyba bind");
    close(server_fd);
    atomic_store(&chladnicka->pracuj, false);
    pthread_exit(NULL);
  }

  if (listen(server_fd, 1) < 0) {
    perror("[wifi] chyba listen");
    close(server_fd);
    atomic_store(&chladnicka->pracuj, false);
    pthread_exit(NULL);
  }

  printf("[WiFi server] počúvam na porte %d...\n", WIFI_PORT);

  while (atomic_load(&chladnicka->pracuj)) {
    socklen_t addrlen = sizeof(address);
    int client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen);
    if (client_fd < 0) {
      if (!atomic_load(&chladnicka->pracuj)) {
        break;
      }
      continue;
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &address.sin_addr, client_ip, sizeof(client_ip));
    printf("[WiFi Server] Klient pripojený z IP: %s\n", client_ip);

    char buffer[4096];
    while (atomic_load(&chladnicka->pracuj)) {
      ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);

      if (bytes_read <= 0) {
        printf("[WiFi server] Klient sa odpojil.\n");
        close(client_fd);
        break;
      }

      pthread_mutex_lock(&chladnicka->mutex);

      for (ssize_t i = 0; i < bytes_read; i++) {
        char c = buffer[i];

        if (c == '\r' || c == '\n') {
          continue;
        }

        if (chladnicka->aktual_znak >= chladnicka->max_znakov -1) {
          if (!navis_chladnicku(chladnicka)) {
            atomic_store(&chladnicka->pracuj, false);
            pthread_mutex_unlock(&chladnicka->mutex);
            close(client_fd);
            close(server_fd);
            pthread_exit(NULL);
          }
        }
        chladnicka->nacitane[chladnicka->aktual_znak++] = c;

      }

      pthread_cond_signal(&chladnicka->kontroluj);
      pthread_mutex_unlock(&chladnicka->mutex);


    }


  }
  close(server_fd);
  pthread_exit(NULL);
}
