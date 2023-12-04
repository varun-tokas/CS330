#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  if (argc == 1) {
    printf("Unable to execute\n");
    exit(1);
  }
  char *endptr;
  unsigned long operand = strtoul(argv[argc - 1], &endptr, 10);
  if (*endptr != '\0') {
    printf("Unable to execute\n");
    exit(1);
  }
  unsigned long output = operand * operand;
  if (argc == 2) {
    printf("%lu\n", output);
    exit(1);
  } else {
    argv = argv + 1;
    sprintf(argv[argc - 2], "%lu", output);
    if (execv(argv[0], argv) < 0) {
      printf("Unable to execute\n");
      exit(1);
    }
  }
  return 0;
}
