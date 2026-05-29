#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../library/uart.h"

int main(void) {
  int fd = uart_init();
  if (fd < 0)
    return EXIT_FAILURE;

  printf("Čakám na dáta z ESP...\n");

  char line[UART_BUF_SIZE];

  while (1) {
    int len = uart_read_line(fd, line, sizeof(line));
    if (len < 0)
      break;
    if (len == 0)
      continue;

    printf("Prijato: %s\n", line);

    // Parsovanie formátu "counter:message"
    char *sep = strchr(line, ':');
    if (sep != NULL) {
      *sep = '\0';
      int counter = atoi(line);
      char *message = sep + 1;
      printf("  Counter: %d\n", counter);
      printf("  Správa:  %s\n", message);
    }
  }

  uart_close(fd);
  return EXIT_SUCCESS;
}