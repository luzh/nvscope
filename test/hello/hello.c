#include <stdio.h>
#include <stdlib.h>

void hello() { printf("Hello World!\n"); }

int main(int argc, char** argv) {
  hello();

  int a = 2, b = 3;

  if (argc > 1) a = atoi(argv[1]);
  if (argc > 2) b = atoi(argv[2]);

  int res = a + b;
  printf("%d + %d = %d\n", a, b, res);

  return 0;
}
