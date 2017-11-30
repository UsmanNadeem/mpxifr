#include <signal.h>//for siginfo_t and sigaction
#include <stdarg.h>//for varargs
#include <stdio.h>//for fprintf
#include <stdlib.h>//for malloc
#include <string.h>//for memset
#include <unistd.h>//For rand()
#include <execinfo.h>//for backtrace() and backtrace_symbols()

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>

#include <glib.h>//for GHashTable
#include "mash.h"
#include <immintrin.h>



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
  #define LOCK_GLOBAL_INFO(varg) do { } while(0)
  #define UNLOCK_GLOBAL_INFO(varg) do { } while(0)
#endif



#define IFRIT_HASH_TABLE
#ifdef IFRIT_HASH_TABLE
__thread GHashTable *myWriteIFRs;
__thread GHashTable *myReadIFRs;
#endif


__thread int threadID;
__thread IFR *raceCheckIFR;

pthread_mutex_t availabilityLock;
pthread_t threadAvailability[MAX_THDS];

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
  pthread_mutex_lock(&availabilityLock);
  for (int i = 0; i < MAX_THDS; ++i)   
  {
    if( threadAvailability[i] == (pthread_t)0 ){
      threadID = i;
      threadAvailability[i] = pthread_self();
      break;
    }
  }
  pthread_mutex_unlock(&availabilityLock);


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

  pthread_mutex_lock(&availabilityLock);
  threadAvailability[threadID] = (pthread_t)0;
  pthread_mutex_unlock(&availabilityLock);
  
  pthread_mutex_lock(&allThreadsLock);
  int i = 0;
  for(i = 0; i < MAX_THDS; i++){
    if( allThreads[i] != (pthread_t)0 && pthread_equal(allThreads[i],pthread_self()) ){
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

void sigseg(int sig) {
  fprintf(stderr, "[IFRit] Received signal SIGSEGV\n");
  // pthread_cancel(samplingAlarmThread);
  // pthread_join(samplingAlarmThread,NULL);
  print_trace();
  exit(0);
}

/*extern "C" */void __attribute__((constructor)) IFR_Init(void){
  signal(SIGINT, sigint);
  signal(SIGKILL, sigint);
  signal(SIGSEGV, sigseg);
  // _mash_dummy();
  // mpxrt_prepare();
  fprintf(stderr, "[IFRit] Initializing IFR Runtime\n");
  dbprintf(stderr,"Initializing IFR Runtime\n");



#ifdef IFRIT_HASH_TABLE
  fprintf(stderr, "[IFRit] MPX-based implementation in use.\n");
#endif

#ifdef SINGLE_THREADED_OPT
  fprintf(stderr, "[IFRit] Single-threaded optimization enabled.\n");
#endif

#ifndef CHECK_FOR_RACES
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

  pthread_mutex_init(&availabilityLock,NULL);


  for (i = 0; i < MAX_THDS; ++i) {
    threadAvailability[i] = (pthread_t)0;
  }
  threadAvailability[0] = pthread_self();
  threadID = 0;

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

  }
}
#endif


#ifdef IFRIT_HASH_TABLE
void add_ifrs_to_local_state(int num_new_ifrs, unsigned long *new_ifrs) {
  int v;
  for (v = 0; v < num_new_ifrs; v++) {
    gpointer varg = (gpointer) new_ifrs[v];
    assert(varg != NULL);
    /*todo check this assert*/
    assert(g_hash_table_lookup(myReadIFRs, varg) == NULL);
    g_hash_table_insert(myReadIFRs, varg, varg);
    assert(g_hash_table_lookup(myReadIFRs, varg) == varg);
  }
}
#endif




// **********************************************************************************************************************************
/*extern "C" */void IFRit_begin_ifrs(unsigned long id,
				     unsigned long num_reads,
				     unsigned long num_writes, ... ){

  // fprintf(stderr,"[IFRit] IFRit_begin_ifrs(ID=%lu, num_reads=%lu, num_writes=%lu) : PC: %p \n", id, num_reads, num_writes,  __builtin_return_address(0));

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

  for (i = 0; i < num_reads; i++) {
    unsigned long varg = va_arg(ap, unsigned long);
    assert(varg);
    IFRit_begin_one_read_ifr(id, varg);
  }


  for (i = 0; i < num_writes; i++) {
    unsigned long varg = va_arg(ap, unsigned long);
    assert(varg);
    IFRit_begin_one_write_ifr(id, varg);
  }


#endif
}

// **********************************************************************************************************************************

// **********************************************************************************************************************************
__attribute__(( always_inline )) int IFRit_begin_one_read_ifr_CS(unsigned long varg, unsigned long id) {
  /* Check if other write IFR active in MPX table*/
  unsigned char buf_fetch[17];  
  _mash_get((unsigned long)varg, (unsigned long)varg, buf_fetch);  
  
  uint64_t mask = 0xffffffffffffffff; 
  uint64_t currThreadBitPosition = ((uint64_t)0b1) << threadID; 
  mask = (mask & (~currThreadBitPosition)); 
  
  uint64_t writeBound = *((uint64_t*)buf_fetch);   
  uint64_t writeActive = writeBound&mask; 

  /* Get READ IFR active in MPX table*/ 
  uint64_t readBound = *((uint64_t*)buf_fetch+8); 

  /*Add READ IFR to MPX table*/ 
  readBound = readBound|currThreadBitPosition;  
  *((uint64_t*) buf_fetch+8) = readBound; 
  _mash_store((unsigned long)varg, (unsigned long)varg, buf_fetch);  

  if (writeActive==0) return 0;
  else return 1;
}


/*extern "C" */__attribute__(( always_inline )) void IFRit_begin_one_read_ifr(unsigned long id,
               unsigned long varg) {

  // fprintf(stderr,"[IFRit] IFRit_begin_one_read_ifr(ID=%lu, ptr=%p) : PC: %p \n", id, (void*)varg,  __builtin_return_address(0));
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
assert(varg);


  /*Return if read IFR for current thread exists*/ 
  if (g_hash_table_lookup(myReadIFRs, (gconstpointer) varg))
  {
    return;
  }

  int race = 0;
  unsigned status = _XABORT_EXPLICIT;
  if ((status = _xbegin ()) == _XBEGIN_STARTED) 
  {
    race = IFRit_begin_one_read_ifr_CS(varg, id);
    _xend ();
  } else {
    pthread_mutex_lock(&availabilityLock);
    race = IFRit_begin_one_read_ifr_CS(varg, id);
    pthread_mutex_unlock(&availabilityLock);
  }
  
  /* Add IFR to thread local READ IFR hashtable */
    // fprintf(stderr,"NOT Active in local read %p %p ***\n", myReadIFRs, (gpointer)varg);
    // assert(g_hash_table_lookup(myReadIFRs, (gconstpointer) varg) == NULL);


  g_hash_table_insert(myReadIFRs, (gpointer)varg, (gpointer)varg);  // same key,val = data ptr
    

    // assert(g_hash_table_lookup(myReadIFRs, (gconstpointer) varg) == (gconstpointer) varg);
    // fprintf(stderr,"stored in local read ***\n");

  /* datarace */  
  if (race) {
    void *curProgPC = __builtin_return_address(0);  
    fprintf(stderr,"***[IFRit] [RW] IFR ID: %lu  PC: %p threadID: %08x Data: %p \n", id, curProgPC, pthread_self(), (void*)varg); 
    // print_trace();  
  }
}
// **********************************************************************************************************************************

// **********************************************************************************************************************************
__attribute__(( always_inline )) int IFRit_begin_one_write_ifr_CS(unsigned long varg, unsigned long id) {
  /* Check if other write IFR active in MPX table*/ 
  unsigned char buf_fetch[17];  
  _mash_get((unsigned long)varg, (unsigned long)varg, buf_fetch);  

  uint64_t mask = 0xffffffffffffffff; 
  uint64_t currThreadBitPosition = ((uint64_t)0b1) << threadID; 
  mask = (mask & (~currThreadBitPosition)); 

  /* Check if other write IFR active in MPX table*/
  uint64_t writeBound = *((uint64_t*)buf_fetch);  
  uint64_t writeActive = writeBound&mask; 

  /* Check if other read IFR active in MPX table*/
  uint64_t readBound = *((uint64_t*)buf_fetch+8);
  uint64_t readActive = readBound&mask;

  /*Add WRITE IFR to MPX table*/
  writeBound = writeBound|currThreadBitPosition;
  *((uint64_t*) buf_fetch) = writeBound;
  _mash_store((unsigned long)varg, (unsigned long)varg, buf_fetch); 

  if (writeActive != 0) 
    return 1;
  if (readActive != 0) 
    return 2;
  return 0;
}

    // fprintf(stderr,"***[IFRit] [WR] %lu ", writeBound); \
    // fprintf(stderr,"%lu ***\n", writeBound); \
    // fprintf(stderr,"stored in mpx***\n", writeBound); \

/*extern "C" */__attribute__(( always_inline )) void IFRit_begin_one_write_ifr(unsigned long id, 
                unsigned long varg) {

  // fprintf(stderr,"[IFRit] IFRit_begin_one_write_ifr(ID=%lu, ptr=%p) : PC: %p \n", id, (void*)varg,  __builtin_return_address(0));
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
  assert(varg);

  /*Return if write IFR for current thread exists*/ 
  if (g_hash_table_lookup(myWriteIFRs, (gconstpointer) varg))
  {
    // fprintf(stderr,"alreadyActive in local ***\n");
    return;
  }
  
  int race = 0;

  unsigned status = _XABORT_EXPLICIT;
  if ((status = _xbegin ()) == _XBEGIN_STARTED) 
  {
    race = IFRit_begin_one_write_ifr_CS(varg, id);
    _xend ();
  } else {
    pthread_mutex_lock(&availabilityLock);
    race = IFRit_begin_one_write_ifr_CS(varg, id);
    pthread_mutex_unlock(&availabilityLock);
  }

  if (race == 1) {
    void *curProgPC = __builtin_return_address(0);  
    fprintf(stderr,"***[IFRit] [WW] IFR ID: %lu  PC: %p threadID: %08x Data: %p \n", id, curProgPC, pthread_self(), (void*)varg); 
  }
  if (race == 2) {
    void *curProgPC = __builtin_return_address(0);  
    fprintf(stderr,"***[IFRit] [WR] IFR ID: %lu  PC: %p threadID: %08x Data: %p \n", id, curProgPC, pthread_self(), (void*)varg);
  }
  
  /*print_trace();*/ 

  /* Add IFR to thread local WRITE IFR hashtable */

    // fprintf(stderr,"NOT Active in local write%p %p ***\n", myWriteIFRs, (gpointer)varg);
    // assert(g_hash_table_lookup(myWriteIFRs, (gconstpointer) varg) == NULL);

  g_hash_table_insert(myWriteIFRs, (gpointer)varg, (gpointer)varg);    // same key,val = data ptr
  
    // assert(g_hash_table_lookup(myWriteIFRs, (gconstpointer) varg) == (gconstpointer) varg);
    // fprintf(stderr,"stored in local write***\n");


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

__attribute__(( always_inline )) void process_end_read_CS(unsigned long varg) {
  unsigned char buf_fetch[17];  
  _mash_get((unsigned long)varg, (unsigned long)varg, buf_fetch);  

  uint64_t mask = 0xffffffffffffffff; 
  uint64_t currThreadBitPosition = ((uint64_t)0b1) << threadID;
  mask = (mask & (~currThreadBitPosition));


  /* Get READ IFR active in MPX table*/
  uint64_t readBound = *((uint64_t*)buf_fetch+8);

  /*Remove READ IFR from MPX table*/
  readBound = readBound&currThreadBitPosition;
  *((uint64_t*) buf_fetch+8) = readBound;

  _mash_store((unsigned long)varg, (unsigned long)varg, buf_fetch);
}


/* Process an active read IFR for an end IFRs action. Returns true if
   the IFR should be deleted from local state. */
gboolean process_end_read(gpointer key, gpointer value, gpointer user_data) {
  unsigned long varg = (unsigned long) key;
  struct EndIFRsInfo *endIFRsInfo = (struct EndIFRsInfo *) user_data;

  // Check to see if read or write IFRs for this varg continue through
  //this release.
  bool keepMay = false;
  int q;

  for (q = 0; q < endIFRsInfo->numMay; q++){
    if (endIFRsInfo->mayArgs[q] == varg) {
      keepMay = true;
      break;
    }
  }

  if (!keepMay) {
    for (q = 0; q < endIFRsInfo->numMust; q++){
      if (endIFRsInfo->mustArgs[q] == varg) {
  keepMay = true;
  break;
      }
    }
  }

  if (keepMay) {
    return FALSE;
  }

  unsigned status = _XABORT_EXPLICIT;
  if ((status = _xbegin ()) == _XBEGIN_STARTED) 
  {
    process_end_read_CS(varg);
    _xend ();
  } else {
    pthread_mutex_lock(&availabilityLock);
    process_end_read_CS(varg);
    pthread_mutex_unlock(&availabilityLock);
  }

  return TRUE;
}

__attribute__(( always_inline )) void process_end_write_CS(unsigned long varg, struct EndIFRsInfo* endIFRsInfo, bool downgrade) {

  unsigned char buf_fetch[17]; 

  _mash_get((unsigned long)varg, (unsigned long)varg, buf_fetch);  
  
  uint64_t mask = 0xffffffffffffffff;
  uint64_t currThreadBitPosition = ((uint64_t)0b1) << threadID;
  mask = (mask & (~currThreadBitPosition));

  uint64_t writeBound = *((uint64_t*)buf_fetch);
  
  /*delete write from MPX*/
  writeBound = writeBound&mask;
  *((uint64_t*) buf_fetch) = writeBound;


  if (downgrade) {
    /* Get READ IFR active in MPX table*/
    uint64_t readBound = *((uint64_t*)buf_fetch+8);

    /*Add READ IFR to MPX table*/
    readBound = readBound|currThreadBitPosition;
    *((uint64_t*) buf_fetch+8) = readBound;

    endIFRsInfo->downgradeVars[endIFRsInfo->numDowngrade] = varg;
    endIFRsInfo->numDowngrade = endIFRsInfo->numDowngrade + 1;
    assert(endIFRsInfo->numDowngrade <= endIFRsInfo->numMay);
  }

  _mash_store((unsigned long)varg, (unsigned long)varg, buf_fetch);
}

/* Process an active write IFR during end_ifrs. Returns true if the
   write should be deleted from the local state. */
gboolean process_end_write(gpointer key, gpointer value, gpointer user_data) {
  unsigned long varg = (unsigned long) key;
  struct EndIFRsInfo *endIFRsInfo = (struct EndIFRsInfo *) user_data;

  // Check to see if this IFR continues through this release.
  bool keepMust = false;
  int q;
  for (q = 0; q < endIFRsInfo->numMust; q++){
    if (endIFRsInfo->mustArgs[q] == varg){
      keepMust = true;
      break;
    }
  }

  if (keepMust) {
    return FALSE;
  }

  // If not, check if should be downgraded to a read IFR.
  bool downgrade = false;
  for (q = 0; q < endIFRsInfo->numMay; q++) {
    if (endIFRsInfo->mayArgs[q] == varg && !(g_hash_table_lookup(myReadIFRs, (gconstpointer) varg))) {
      downgrade = true;
      break;
    }
  }

  unsigned status = _XABORT_EXPLICIT;
  if ((status = _xbegin ()) == _XBEGIN_STARTED) 
  {
    process_end_write_CS(varg, endIFRsInfo, downgrade);
    _xend ();
  } else {
    pthread_mutex_lock(&availabilityLock);
    process_end_write_CS(varg, endIFRsInfo, downgrade);
    pthread_mutex_unlock(&availabilityLock);
  }

  return TRUE;
}

// **********************************************************************************************************************************
void IFRit_end_ifrs_internal(unsigned long numMay, unsigned long numMust, va_list *ap) {
  if ((myWriteIFRs != NULL && myReadIFRs != NULL) && (g_hash_table_size(myWriteIFRs) + g_hash_table_size(myReadIFRs)) == 0) {
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


  /*process_end_write*/
    /*We have to delete those write IFRs that are not in myWriteIFRs*/
    /*if mustArg in myWriteIFRs --> dont delete*/
    /*for all other elements in myWriteIFRs --> 
        if it is in mayArgs and !READ_IFR_EXISTS(element)
          downgrade --> activate readIFR+MPX and delete in write IFR+MPX
    */
  g_hash_table_foreach_remove(myWriteIFRs, process_end_write, endIFRsInfo);


  /*Process_end_read*/
    /*dont delete if mayArg is in myReadIFRs*/
    /*dont delete if mustArg is in myReadIFRs*/
    /*else delete in both MPX + local*/
  g_hash_table_foreach_remove(myReadIFRs, process_end_read, endIFRsInfo);

  /*add downgraded IFRs*/
  add_ifrs_to_local_state(endIFRsInfo->numDowngrade, endIFRsInfo->downgradeVars);

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
  // fprintf(stderr, "[IFRit] Free\n");

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

