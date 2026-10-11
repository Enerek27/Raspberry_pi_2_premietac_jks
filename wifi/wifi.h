#pragma once

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define WIFI_PORT 2020

typedef struct {

  pthread_mutex_t mutex;
  pthread_cond_t zapisuj;
  pthread_cond_t kontroluj;
  int fd;
  atomic_bool pracuj;
  char *nacitane;
  int max_znakov;
  int aktual_znak;

} Wifi_chladnicka_t;

bool navis_chladnicku(Wifi_chladnicka_t *ch);


int uart_init(void);
void uart_close(int fd);
int uart_read_line(int fd, char *line, int max_len);
void *wifi_worker(void *arg);
int uart_read_line_nonblock(int fd, char *line, int max_len);