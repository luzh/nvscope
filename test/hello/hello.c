#include <stdio.h>
#include <stdlib.h>

void hello() { printf("Hello World!\n"); }

int main(int argc, char **argv) {
  hello();

  int a = 2, b = 3;

  if (argc > 1)
    a = atoi(argv[1]);
  if (argc > 2)
    b = atoi(argv[2]);

  // This addition should be changed to multiplication by the bin2mul pass.
  int res = a + b;
  printf("%d + %d = %d\n", a, b, res);

  if (res != a * b) {
    printf("%d * %d != %d\n", a, b, a * b);
    return 1;
  }

  return 0;
}
