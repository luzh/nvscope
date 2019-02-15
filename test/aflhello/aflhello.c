#include "common.h"

#define BUFSIZE 100

int main() {
  char key[4] = {0, 0, 0, 0};

  printf("Key: ");
  int chars = scanf("%c%c%c", &key[0], &key[1], &key[2]);
  char *buf = malloc(BUFSIZE);

  printf("Got %d chars, key = %s\n", chars, key);
  if (key[0] == 'a') {
    printf("Into path a\n");
    if (key[1] == 'b') {
      printf("Into path b\n");
      if (key[2] == 'c') {
        printf("Into path c\n");
        memset(buf, 0xAC, BUFSIZE);
        _mm_sfence();
        abort();  // simulating a bug causing a crash
      }
    }
  }

  free(buf);

  printf("Normal Exit\n");

  return 0;
}
