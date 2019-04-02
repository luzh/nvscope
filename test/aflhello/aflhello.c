#include "headers.h"

int main() {
  char key[4] = {0, 0, 0, 0};

  printf("Key: ");
  int chars = scanf("%c%c%c", &key[0], &key[1], &key[2]);

  printf("Got %d chars, key = %s\n", chars, key);
  if (key[0] == 'a') {
    printf("Into path a\n");
    if (key[1] == 'b') {
      printf("Into path b\n");
      if (key[2] == 'c') {
        printf("Into path c\n");
        abort(); // simulating a bug causing a crash
      }
    }
  }

  printf("No bug found\n");

  return 0;
}
