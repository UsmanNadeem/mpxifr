#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

int Global;
int Global2;

void foo(void *data) {
  if (data == 0) printf("%s\n","Hello\n" );
}
void *Thread1(void *x) {
  for (int i = 0; i < 5000000; ++i)
  {
  // Global++;
  	Global2 = Global+1;
  	foo((void*)&Global2);
  }
  return NULL;
}

void *Thread2(void *x) {
for (int i = 0; i < 5000000; ++i)
  {
  Global--;
  }  
  return NULL;
}

int main() {
  pthread_t t[2];
  // usleep(1);
  pthread_create(&t[1], NULL, Thread2, NULL);
  pthread_create(&t[0], NULL, Thread1, NULL);
  pthread_join(t[0], NULL);
  pthread_join(t[1], NULL);
}