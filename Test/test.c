/* Test code for IFRit */

#include "stdio.h"
#include "pthread.h"
#include "inttypes.h"
#include "string.h"
#include <unistd.h>
uint64_t x;

// void foo(void *data) {
  // if (data != 0) printf("%s\n","Hello\n" );
// }
void *thread(void *data) {
  for (uint64_t i = 0; i < 1000000; i++) {
    // usleep(1);
    x += 1;
    // foo(data);
    // x += 1;
    // foo(data);
    // x += 1;
    // foo(data);
    // x += 1;
    // foo(data);
    // x += 1;
    // foo(data);
    // x += 1;
    // foo(data);
    // x += 1;
    // foo(data);
    // x += 1;
    // foo(data);
    // x += 1;
    // foo(data);
    // x += 1;
    // foo(data);
    // x += 1;
    // foo(data);
    // x += 1;
    // foo(data);
    // printf("%lu\n", x);
  }

  return 0;
}

int main() {
  printf("Hello world\n");
  x = 1;
  pthread_t t1, t2, t3, t4;
  pthread_create(&t1, NULL, thread, NULL);
  pthread_create(&t2, NULL, thread, NULL);
  // pthread_create(&t3, NULL, thread, NULL);
  // pthread_create(&t4, NULL, thread, NULL);


  pthread_join(t1, NULL);
  pthread_join(t2, NULL);
  // pthread_join(t3, NULL);
  // pthread_join(t4, NULL);
  printf("Final value of x: %lu\n", x);

  return 0;
}
