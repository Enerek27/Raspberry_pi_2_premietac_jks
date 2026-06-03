#pragma once

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define UART_DEVICE "/dev/serial0"
#define UART_BAUD_RATE 115200
#define UART_BUF_SIZE 2000

typedef struct {

  pthread_mutex_t mutex;
  pthread_cond_t zapisuj;
  pthread_cond_t kontroluj;
  int fd;
  atomic_bool pracuj;
  char *nacitane;
  int max_znakov;
  int aktual_znak;

} Uart_chladnicka_t;

bool navis_chladnicku(Uart_chladnicka_t *ch);

int uart_init(void);
void uart_close(int fd);
int uart_read_line(int fd, char *line, int max_len);
void *uart_worker(void *arg);
int uart_read_line_nonblock(int fd, char *line, int max_len);