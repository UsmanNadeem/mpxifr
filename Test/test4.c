#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

int Global;
int Global2;
pthread_mutex_t lock;

void foo(void *data) {
  if (data == 0) printf("%s\n","Hello\n" );
}
void *Thread1(void *x) {
  Global2 = Global2+1;
  pthread_mutex_lock(&lock);
  Global = Global+1;
  pthread_mutex_unlock(&lock);
  Global = Global+1;

  pthread_mutex_lock(&lock);
  pthread_mutex_unlock(&lock);
  Global = Global+1;
  Global2 = Global2+1;

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
  if (pthread_mutex_init(&lock, NULL) != 0)
  {
      printf("\n mutex init failed\n");
      return 1;
  }
  pthread_t t[2];
  // usleep(1);
  pthread_create(&t[1], NULL, Thread2, NULL);
  pthread_create(&t[0], NULL, Thread1, NULL);
  pthread_join(t[0], NULL);
  pthread_join(t[1], NULL);

  pthread_mutex_destroy(&lock);

}