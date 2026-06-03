
#include "uart.h"
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
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

bool navis_chladnicku(Uart_chladnicka_t *ch) {
  int navis = ch->max_znakov + 100;
  char *temp = realloc(ch->nacitane, navis * sizeof(char));
  if (temp == NULL) {
    perror("Chyba zvysenia priestoru pre chladnicku");
    return false;
  }
  ch->nacitane = temp;
  ch->max_znakov = navis;
  return true;
}

void *uart_worker(void *arg) {
  Uart_chladnicka_t *chladnicka = arg;
  char c;
  pthread_mutex_lock(&chladnicka->mutex);
  int fd = chladnicka->fd;
  pthread_mutex_unlock(&chladnicka->mutex);

  while (atomic_load(&chladnicka->pracuj)) {

    int n = read(fd, &c, 1);
    if (n <= 0) {
      break;
    }

    // preskoč \r aj \n, do protokolu ich nechceme
    if (c == '\r' || c == '\n') {
      continue;
    }

    pthread_mutex_lock(&chladnicka->mutex);
    if (!(chladnicka->aktual_znak < chladnicka->max_znakov - 1)) {
      if (!navis_chladnicku(chladnicka)) {
        atomic_store(&chladnicka->pracuj, 0);
        pthread_mutex_unlock(&chladnicka->mutex);
        pthread_exit(NULL);
      }
    }
    chladnicka->nacitane[chladnicka->aktual_znak++] = c;
    pthread_cond_signal(&chladnicka->kontroluj);
    pthread_mutex_unlock(&chladnicka->mutex);
  }
  pthread_exit(NULL);
}
