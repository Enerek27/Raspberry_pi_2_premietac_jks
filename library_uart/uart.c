#include "uart.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

int uart_init(void) {
  int fd = open(UART_DEVICE, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) {
    perror("Chyba otvorenia UART");
    return -1;
  }

  struct termios tty;
  if (tcgetattr(fd, &tty) != 0) {
    perror("tcgetattr");
    close(fd);
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

  // Neblokujúci raw režim
  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 0;

  if (tcsetattr(fd, TCSANOW, &tty) != 0) {
    perror("tcsetattr");
    close(fd);
    return -1;
  }

  // Vyprázdni vstupný buffer – zahodíme garbage pri štarte
  tcflush(fd, TCIFLUSH);

  return fd;
}

void uart_close(int fd) { close(fd); }

// Blokujúce čítanie riadku – ponechané pre prípadné použitie
int uart_read_line(int fd, char *line, int max_len) {
  int pos = 0;
  char c;

  while (pos < max_len - 1) {
    int n = read(fd, &c, 1);
    if (n < 0) {
      perror("Chyba čítania UART");
      return -1;
    }
    if (n == 0)
      continue; // non-blocking, čakaj
    if (c == '\n')
      break;
    if (c != '\r')
      line[pos++] = c;
  }

  line[pos] = '\0';
  return pos;
}

// Prečíta čo je dostupné (až do max_len bajtov), bez blokovania.
// Vráti počet prečítaných bajtov, 0 ak nič, -1 pri chybe.
int uart_read_available(int fd, char *buf, int max_len) {
  fd_set set;
  struct timeval tv = {.tv_sec = 0, .tv_usec = 0};
  FD_ZERO(&set);
  FD_SET(fd, &set);

  int rv = select(fd + 1, &set, NULL, NULL, &tv);
  if (rv < 0) {
    perror("select");
    return -1;
  }
  if (rv == 0)
    return 0;

  int n = read(fd, buf, max_len - 1);
  if (n < 0) {
    perror("read");
    return -1;
  }
  buf[n] = '\0';
  return n;
}

// Kompatibilná wrapper funkcia – číta jeden riadok neblokujúco
// Vracia 0 ak žiadne dáta, -1 pri chybe, inak dĺžku riadku
int uart_read_line_nonblock(int fd, char *line, int max_len) {
  fd_set set;
  struct timeval tv = {.tv_sec = 0, .tv_usec = 0};
  FD_ZERO(&set);
  FD_SET(fd, &set);

  int rv = select(fd + 1, &set, NULL, NULL, &tv);
  if (rv < 0) {
    perror("select");
    return -1;
  }
  if (rv == 0)
    return 0;

  // Čítaj znak po znaku s krátkym timeoutom
  int pos = 0;
  char c;
  while (pos < max_len - 1) {
    int n = read(fd, &c, 1);
    if (n <= 0)
      break; // nič viac nie je k dispozícii
    if (c == '\n')
      break;
    if (c != '\r')
      line[pos++] = c;
  }

  line[pos] = '\0';
  return pos; // vráti 0 ak sme prečítali prázdny riadok – volajúci to ignoruje
}