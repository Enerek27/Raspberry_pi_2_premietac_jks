#include "uart.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

int uart_init(void) {
  int fd = open(UART_DEVICE, O_RDWR | O_NOCTTY | O_SYNC);
  if (fd < 0) {
    perror("Chyba otvorenia UART");
    return -1;
  }

  struct termios tty;
  if (tcgetattr(fd, &tty) != 0) {
    perror("tcgetattr");
    return -1;
  }

  cfsetispeed(&tty, B115200);
  cfsetospeed(&tty, B115200);

  tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
  tty.c_cflag |= (CLOCAL | CREAD);
  tty.c_cflag &= ~(PARENB | PARODD);
  tty.c_cflag &= ~CSTOPB;
  tty.c_cflag &= ~CRTSCTS;

  tty.c_iflag &= ~(IXON | IXOFF | IXANY);
  tty.c_iflag &= ~(IGNBRK | BRKINT | ICRNL | INLCR | PARMRK | INPCK | ISTRIP);

  tty.c_oflag = 0;
  tty.c_lflag = 0;

  tty.c_cc[VMIN] = 1;
  tty.c_cc[VTIME] = 0;

  if (tcsetattr(fd, TCSANOW, &tty) != 0) {
    perror("tcsetattr");
    return -1;
  }

  return fd;
}

void uart_close(int fd) { close(fd); }

// Číta znak po znaku kým nenájde '\n', vráti dĺžku riadku
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