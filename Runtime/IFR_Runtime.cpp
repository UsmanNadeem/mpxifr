#include <signal.h>//for siginfo_t and sigaction
#include <stdarg.h>//for varargs
#include <stdio.h>//for fprintf
#include <stdlib.h>//for malloc
#include <string.h>//for memset
#include <unistd.h>//For rand()
#include <execinfo.h>//for backtrace() and backtrace_symbols()

#include <assert.h>
#include <stdbool.h>

#include <glib.h>//for GHashTable



/*MAC OSX Pthread barrier hack -- 
http://blog.albertarmea.com/post/47089939939/using-pthread-barrier-on-mac-os-x
*/
#ifdef __APPLE__

#ifndef PTHREAD_BARRIER_H_
#define PTHREAD_BARRIER_H_

#include <pthread.h>
#include <errno.h>

typedef int pthread_barrierattr_t;
typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int count;
    int tripCount;
} pthread_barrier_t;


int pthread_barrier_init(pthread_barrier_t *barrier, const pthread_barrierattr_t *attr, unsigned int count)
{
    if(count == 0)
    {
        errno = EINVAL;
        return -1;
    }
    if(pthread_mutex_init(&barrier->mutex, 0) < 0)
    {
        return -1;
    }
    if(pthread_cond_init(&barrier->cond, 0) < 0)
    {
        pthread_mutex_destroy(&barrier->mutex);
        return -1;
    }
    barrier->tripCount = count;
    barrier->count = 0;

    return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier)
{
    pthread_cond_destroy(&barrier->cond);
    pthread_mutex_destroy(&barrier->mutex);
    return 0;
}

int pthread_barrier_wait(pthread_barrier_t *barrier)
{
    pthread_mutex_lock(&barrier->mutex);
    ++(barrier->count);
    if(barrier->count >= barrier->tripCount)
    {
        barrier->count = 0;
        pthread_cond_broadcast(&barrier->cond);
        pthread_mutex_unlock(&barrier->mutex);
        return 1;
    }
    else
    {
        pthread_cond_wait(&barrier->cond, &(barrier->mutex));
        pthread_mutex_unlock(&barrier->mutex);
        return 0;
    }
}

#endif // PTHREAD_BARRIER_H_
#endif // __APPLE__




#include "IFR.h"
#include "IFR_Runtime.h"


#define NDEBUG

//#define DEBUG
#undef DEBUG

//#define RACESTACK
//#undef RACESTACK

unsigned SRATE;
unsigned SOFF;

#ifdef DEBUG
#define dbprintf(...) fprintf(__VA_ARGS__)
#else
#define dbprintf(...)
#endif

pthread_key_t dkey;


#define CHECK_FOR_RACES
#ifdef CHECK_FOR_RACES
  #define VARG_MASK_BITS 5
  #ifdef VARG_MASK_BITS
    unsigned long VARG_MASK = (((1 << VARG_MASK_BITS) - 1) << 3);
    #define NUM_VARG_MASKS (1 << VARG_MASK_BITS)
    pthread_mutex_t drMutex[NUM_VARG_MASKS];
    GHashTable *ActiveMustWriteIFR[NUM_VARG_MASKS];
    GHashTable *ActiveMayWriteIFR[NUM_VARG_MASKS];
  #else
    pthread_mutex_t drMutex;
    GHashTable *ActiveMustWriteIFR;//A hash table mapping from variable -> list of ifrs
    GHashTable *ActiveMayWriteIFR;//A hash table mapping from variable -> list of ifrs
  #endif
#endif

#ifdef CHECK_FOR_RACES
  #ifdef VARG_MASK_BITS
    #define TAKE_VARG_MASK(varg) ((varg & VARG_MASK) >> 3)
    #define LOCK_GLOBAL_INFO(varg) pthread_mutex_lock(&drMutex[TAKE_VARG_MASK(varg)])
    #define UNLOCK_GLOBAL_INFO(varg) pthread_mutex_unlock(&drMutex[TAKE_VARG_MASK(varg)])
    #define ACTIVE_MAY_WRITE_TABLE(varg) (ActiveMayWriteIFR[TAKE_VARG_MASK(varg)])
    #define ACTIVE_MUST_WRITE_TABLE(varg) (ActiveMustWriteIFR[TAKE_VARG_MASK(varg)])
  #else
    #define LOCK_GLOBAL_INFO(varg) pthread_mutex_lock(&drMutex)
    #define UNLOCK_GLOBAL_INFO(varg) pthread_mutex_unlock(&drMutex)
    #define ACTIVE_MAY_WRITE_TABLE(varg) ActiveMayWriteIFR
    #define ACTIVE_MUST_WRITE_TABLE(varg) ActiveMustWriteIFR
  #endif
#else
  #define LOCK_GLOBAL_INFO(varg) do { } while(0)
  #define UNLOCK_GLOBAL_INFO(varg) do { } while(0)
#endif



#define IFRIT_HASH_TABLE
#ifdef IFRIT_HASH_TABLE
__thread GHashTable *myWriteIFRs;
__thread GHashTable *myReadIFRs;
#endif



__thread IFR *raceCheckIFR;

#define SAMPLING
#ifdef SAMPLING
bool gSampleState;
pthread_t samplingAlarmThread;
#endif

pthread_mutex_t allThreadsLock;
pthread_t allThreads[MAX_THDS];

#define SINGLE_THREADED_OPT
#ifdef SINGLE_THREADED_OPT
int num_threads;
#endif

void print_trace(){
  void *array[10];
  int size;
  char **strings;
  int i;

  size = backtrace (array, 10);
  strings = backtrace_symbols (array, size);

  for (i = 2; i < size; i++){
    fprintf (stderr,"  %s\n", strings[i]);
  }

  free (strings);
}

void IFRit_end_ifrs_internal(unsigned long numMay, unsigned long numMust, va_list *ap);

typedef struct _threadInitData {
  void *(*start_routine)(void*);
  void *arg;
} threadInitData;

void *threadStartFunc(void *data){
  /*This looks weird, but it sets the value associated with dkey to 0x1
   *  forcing thd_dtr() to run when the thread terminates.  */
  pthread_setspecific(dkey,(void*)0x1);


#ifdef IFRIT_HASH_TABLE
  myWriteIFRs = g_hash_table_new(g_direct_hash, g_direct_equal);
  myReadIFRs = g_hash_table_new(g_direct_hash, g_direct_equal);
#endif

  pthread_mutex_lock(&allThreadsLock);
  int i = 0;
  for(i = 0; i < MAX_THDS; i++){
    if( allThreads[i] == (pthread_t)0 ){
      allThreads[i] = pthread_self();
      break;
    }
  }

#ifdef SINGLE_THREADED_OPT
  num_threads++;
#endif

  pthread_mutex_unlock(&allThreadsLock);

  raceCheckIFR = new_ifr(pthread_self(), 0, 0, 0);


  void *(*start_routine)(void*) = ((threadInitData *)data)->start_routine;
  void *arg = ((threadInitData *) data)->arg;
  free(data);
  return (start_routine(arg));
}

#ifdef SAMPLING
void *sample(void *v) {

  sigset_t set;
  sigfillset(&set);
  pthread_sigmask(SIG_BLOCK, &set, NULL); 

  char *csrate = getenv("IFR_SRATE");
  char *csoff = getenv("IFR_SOFF");

  if (csrate && csoff) {
    SRATE = atoi( csrate );
    SOFF = atoi( csoff );
    fprintf(stderr, "[IFRit] Sampling enabled with SRATE=%u, SOFF=%u (rate=%f)\n",
	    SRATE, SOFF, (float)SRATE / ((float)(SOFF + SRATE)));
  } else {
    gSampleState = true;
    fprintf(stderr, "[IFRit] Sampling disabled\n");
    return NULL;
  }

  gSampleState = true;
  while (1) {
    if (gSampleState) {
      usleep(SRATE);//On time
    } else {
      usleep(SOFF);
    }
    gSampleState = !gSampleState;
  }
}
#endif

void thd_dtr(void*d){
  /*Destructor*/
  pthread_mutex_lock(&allThreadsLock);
  int i = 0;
  for(i = 0; i < MAX_THDS; i++){
    if( allThreads[i] != (pthread_t)0 &&
        pthread_equal(allThreads[i],pthread_self()) ){
      allThreads[i] = 0;
      break;
    }
  }

#ifdef SINGLE_THREADED_OPT
  num_threads--;
#endif

  pthread_mutex_unlock(&allThreadsLock);

  IFRit_end_ifrs_internal(0, 0, NULL);

  //fprintf(stderr, "[IFRit] total: %lu redundant: %lu stack: %lu\n", totalStarts, alreadyActive, stackAddress);
  //fprintf(stderr, "[IFRit] Rough insertion weight (thread %p): %lu\n", pthread_self(), insertionCount);



#ifdef IFRIT_HASH_TABLE
  if( myWriteIFRs != NULL ){
    g_hash_table_destroy(myWriteIFRs);
  }
  if( myReadIFRs != NULL ){
    g_hash_table_destroy(myReadIFRs);
  }
#endif


  delete_ifr(raceCheckIFR);

}

void sigint(int sig) {
  fprintf(stderr, "[IFRit] Received signal\n");
  pthread_cancel(samplingAlarmThread);
  pthread_join(samplingAlarmThread,NULL);
  exit(0);
}

/*extern "C" */void __attribute__((constructor)) IFR_Init(void){
  signal(SIGINT, sigint);
  signal(SIGKILL, sigint);

  dbprintf(stderr,"Initializing IFR Runtime\n");



#ifdef IFRIT_HASH_TABLE
  fprintf(stderr, "[IFRit] Hash-table-based implementation in use.\n");
#endif

#ifdef SINGLE_THREADED_OPT
  fprintf(stderr, "[IFRit] Single-threaded optimization enabled.\n");
#endif

#ifdef CHECK_FOR_RACES
#ifdef VARG_MASK_BITS
  fprintf(stderr, "[IFRit] Partitioning global state into %d partitions.\n",
	  NUM_VARG_MASKS);
#endif
#else
  fprintf(stderr, "[IFRit] Not checking for races.\n");
#endif

  g_thread_init(NULL);
  
  pthread_mutex_init(&allThreadsLock, NULL);
  int i ;
  for(i = 0; i < MAX_THDS; i++){
    allThreads[i] = (pthread_t)0;
  }

  allThreads[0] = pthread_self();

#ifdef SINGLE_THREADED_OPT
  num_threads = 1;
#endif

#ifdef SAMPLING
  //Release allThreads to samplingAlarmThread
  pthread_create(&samplingAlarmThread,NULL,sample,NULL);
#else
  fprintf (stderr, "[IFRit] Sampling disabled.\n");
#endif

  pthread_key_create(&dkey,thd_dtr);

#ifdef IFRIT_HASH_TABLE
  myWriteIFRs = g_hash_table_new(g_direct_hash, g_direct_equal);
  myReadIFRs = g_hash_table_new(g_direct_hash, g_direct_equal);
#endif

  raceCheckIFR = new_ifr(pthread_self(), 0, 0, 0);

}

/*extern "C" */void __attribute__((destructor)) IFR_Exit(void){
  fprintf(stderr, "[IFRit] Bye!\n");
}



#ifdef CHECK_FOR_RACES


void IFR_raceCheck(gpointer key, gpointer value, gpointer data){
  IFR *me = (IFR *) data;
  IFR *ifr = (IFR *) value;
  if (!pthread_equal(ifr->thread, me->thread)) {
    //raceCount++;
    fprintf(stderr,"[IFRit] %lu %lu : %p %p\n", me->id, ifr->id,
	    (void *) me->instAddr, (void *) ifr->instAddr);
#ifdef RACESTACK
    print_trace();
#endif
  }
}
#endif


#ifdef IFRIT_HASH_TABLE
#define GET_NUM_ACTIVE_IFRS \
  (g_hash_table_size(myWriteIFRs) + g_hash_table_size(myReadIFRs))
#endif

#define IFR_TABLES_VALID (myWriteIFRs != NULL && myReadIFRs != NULL)



#ifdef IFRIT_HASH_TABLE
void add_ifrs_to_local_state(int num_new_ifrs, unsigned long *new_ifrs, int write) {
  GHashTable *myIFRs = write ? myWriteIFRs : myReadIFRs;
  int v;
  for (v = 0; v < num_new_ifrs; v++) {
    gpointer varg = (gpointer) new_ifrs[v];
    assert(varg != NULL);
    assert(g_hash_table_lookup(myIFRs, varg) == NULL);
    g_hash_table_insert(myIFRs, varg, varg);
    assert(g_hash_table_lookup(myIFRs, varg) == varg);
  }
}
#endif




// **********************************************************************************************************************************
/*extern "C" */void IFRit_begin_ifrs(unsigned long id,
				     unsigned long num_reads,
				     unsigned long num_writes, ... ){


  // CHECK_SAMPLE_STATE;
  #ifdef SAMPLING
    if (!gSampleState) {
        return;
    }
  #endif


  #ifdef SINGLE_THREADED_OPT
    if (num_threads == 1) {
      return;
    }
  #endif

  unsigned int i;
  va_list ap;

#ifdef CHECK_FOR_RACES
  unsigned long all_rvargs[num_reads];
  unsigned long all_wvargs[num_writes];
  int numNewReads = 0;
  int numNewWrites = 0;
#endif
#ifdef CHECK_FOR_RACES

  va_start(ap, num_writes);

  // todo add only non duplicate read ifrs for (gconstpointer) varg for current thread and check races 
  // see info in read and write funcs 

  // for (i = 0; i < num_reads; i++) {
  //   unsigned long varg = va_arg(ap, unsigned long);
  //   assert(varg);
  //   //todo
  // }


  // todo add only non duplicate read ifrs for (gconstpointer) varg for current thread and check races 
  // see info in read and write funcs 

  // for (i = 0; i < num_writes; i++) {
  //   unsigned long varg = va_arg(ap, unsigned long);
  //   assert(varg);
  //   //todo
  // }


#endif
}

// **********************************************************************************************************************************

// **********************************************************************************************************************************
/*extern "C" */void IFRit_begin_one_read_ifr(unsigned long id,
					     unsigned long varg) {

    // CHECK_SAMPLE_STATE;
  #ifdef SAMPLING
    if (!gSampleState) {
        return;
    }
  #endif;

#ifdef SINGLE_THREADED_OPT
  if (num_threads == 1) {
    return;
  }
#endif


// todo: return if read IFR for current thread exists 

  g_hash_table_insert(myReadIFRs, (gpointer) varg, (gpointer) varg);  /*READ_IFR_INSERT(varg)*/

#ifdef CHECK_FOR_RACES
  void *curProgPC = __builtin_return_address(0);
  raceCheckIFR->id = id;
  raceCheckIFR->instAddr = (unsigned long) curProgPC;

// todo: check for races with WRITE IFRs

  // Check for read/write races.
  // LOCK_GLOBAL_INFO(varg);
  /* Start IFR by adding it to the hash table. */
  // activateReadIFR(varg, curProgPC, id);
  // assert(varg);

  // see write for more info

  // UNLOCK_GLOBAL_INFO(varg);
#endif
}
// **********************************************************************************************************************************

// **********************************************************************************************************************************
/*extern "C" */void IFRit_begin_one_write_ifr(unsigned long id, 
					      unsigned long varg) {

    // CHECK_SAMPLE_STATE;
  #ifdef SAMPLING
    if (!gSampleState) {
        return;
    }
  #endif;

  #ifdef SINGLE_THREADED_OPT
    if (num_threads == 1) {
      return;
    }
  #endif


  // todo: if write ifr for this variable for current threadexists then return
  // if (g_hash_table_lookup(myWriteIFRs, (gconstpointer) varg)  WRITE_IFR_EXISTS(varg)) {
  //   return;
  // }

  // todo: else check for races add it
  // g_hash_table_insert(myWriteIFRs, (gpointer) varg, (gpointer) varg);  /*WRITE_IFR_INSERT(varg)*/

#ifdef CHECK_FOR_RACES
  void *curProgPC = __builtin_return_address(0);
  raceCheckIFR->id = id;
  raceCheckIFR->instAddr = (unsigned long) curProgPC;

  // Check for read/write and write/write data races.
  // todo: perhaps take a lock or use a transaction



  // LOCK_GLOBAL_INFO(varg);

  // IFR_raceCheck((gpointer) varg , IFR *other write IFR, IFR *me/raceCheckIFR)  race if not equal(ifr->thread, me->thread)
  // IFR_raceCheck((gpointer) varg , IFR *other READ IFR, IFR *me/raceCheckIFR)  race if not equal(ifr->thread, me->thread)
  
  /* Start IFR by adding it to the hash table. */

  // void activateWriteIFR(unsigned long varg, void * curProgPC, unsigned long ifrID id){
  // assert(varg);
  // new_ifr(pthread_t tid pthread_self(), ifrID id, (unsigned long) PC, data pointer unsigned long varg)
  // IFR *new_ifr(pthread_t tid, unsigned long id, unsigned long iAddr, unsigned long dAddr){

  // do something with same thread old ifr if it exists

}

  // UNLOCK_GLOBAL_INFO(varg);
#endif
}
// **********************************************************************************************************************************

/* Information about a release "end IFRs" action. */
struct EndIFRsInfo {
  int numMay;
  unsigned long *mayArgs;
  int numMust;
  unsigned long *mustArgs;
  int numDowngrade;
  unsigned long *downgradeVars;
};


// **********************************************************************************************************************************
void IFRit_end_ifrs_internal(unsigned long numMay, unsigned long numMust, va_list *ap) {
  if (IFR_TABLES_VALID && GET_NUM_ACTIVE_IFRS == 0) {
    return;
  }


  struct EndIFRsInfo *endIFRsInfo = (struct EndIFRsInfo *)
    malloc(sizeof (struct EndIFRsInfo));

  endIFRsInfo->numMay = numMay;
  endIFRsInfo->mayArgs = (unsigned long *) calloc(numMay,
						  sizeof(unsigned long));
  endIFRsInfo->numMust = numMust;
  endIFRsInfo->mustArgs = (unsigned long *) calloc(numMust,
						   sizeof(unsigned long));

  unsigned int v;
  for (v = 0; v < numMay; v++) {
    endIFRsInfo->mayArgs[v] = va_arg(*ap, unsigned long);
  }

  for (v = 0; v < numMust; v++) {
    endIFRsInfo->mustArgs[v] = va_arg(*ap, unsigned long);
  }

  endIFRsInfo->numDowngrade = 0;
  endIFRsInfo->downgradeVars = (unsigned long *)
    calloc(numMay, sizeof(unsigned long));


    // add if def

      // todo: 
      // process_end_write endIFRsInfo
        // search my thread's writeIFRs for mustArgs --> if it exists then dont delete else delete
        // If not, check if should be downgraded to a read IFR.
        // if mayArgs in mywriteIFRs AND not in myReadIFRs then downgrade to readIFR and activate readifr and deactivate writeifr

      // process_end_read endIFRsInfo
        // for all may and must arg pointers check to see if they are active for my READ IFRs
        // if they are active then dont delete
        // else delete

    // todo: since this is inverted we may need a thread local list of IFRs

    // end if def

  free(endIFRsInfo->mayArgs);
  free(endIFRsInfo->mustArgs);
  free(endIFRsInfo);
}
// **********************************************************************************************************************************

/*extern "C" */int IFRit_pthread_mutex_unlock(pthread_mutex_t *lock, unsigned long numMay, unsigned long numMust, ... ){
  va_list ap;
  va_start(ap, numMust);

  IFRit_end_ifrs_internal(numMay, numMust, &ap);

  return pthread_mutex_unlock(lock);
}

/*extern "C" */int IFRit_pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine) (void *), void *arg, unsigned long numMay, unsigned long numMust, ...){
  va_list ap;
  va_start(ap, numMust);

  IFRit_end_ifrs_internal(numMay, numMust, &ap);

  threadInitData *tid = (threadInitData*)malloc(sizeof(*tid));
  tid->start_routine = start_routine;
  tid->arg = arg;
  int ret = pthread_create(thread,attr,threadStartFunc,(void*)tid);
  return ret;
}

/*extern "C" */int IFRit_pthread_rwlock_unlock(pthread_rwlock_t *rwlock, unsigned long numMay, unsigned long numMust, ...){
  va_list ap;
  va_start(ap, numMust);

  IFRit_end_ifrs_internal(numMay, numMust, &ap);

  return pthread_rwlock_unlock(rwlock);
}

/*extern "C" */void IFRit_free(void *mem, unsigned long numMay, unsigned long numMust, ...) {
  va_list ap;
  va_start(ap, numMust);

  IFRit_end_ifrs_internal(numMay, numMust, &ap);

  free(mem);
}

/*extern "C" */int IFRit_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex){
  IFRit_end_ifrs_internal(0, 0, NULL);

  return pthread_cond_wait(cond, mutex);
}

/*extern "C" */int IFRit_pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime){
  IFRit_end_ifrs_internal(0, 0, NULL);

  return pthread_cond_timedwait(cond, mutex, abstime);
}

/*extern "C" */int IFRit_pthread_barrier_wait(pthread_barrier_t *barrier) {
  IFRit_end_ifrs_internal(0, 0, NULL);

  return pthread_barrier_wait(barrier);
}

/*extern "C" */void *IFRit_realloc(void *ptr, size_t size) {
  IFRit_end_ifrs_internal(0, 0, NULL);

  return realloc(ptr, size);
}

/*extern "C" */void IFRit_end_ifrs(){
  IFRit_end_ifrs_internal(0, 0, NULL);
}

