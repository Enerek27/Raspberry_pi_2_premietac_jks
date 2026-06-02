#pragma once

#include <stdint.h>

#define UART_DEVICE "/dev/serial0"
#define UART_BAUD_RATE 115200
#define UART_BUF_SIZE 256

int uart_init(void);
void uart_close(int fd);
int uart_read_line(int fd, char *line, int max_len);
int uart_read_available(int fd, char *buf, int max_len);
int uart_read_line_nonblock(int fd, char *line, int max_len);