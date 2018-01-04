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


#include <atomic>
#include <unordered_map>
#include <vector>
#include <map>

#include <string>
#include <sstream>
#include <iostream>

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

// **********************************************************************************************************************************
void printBits(uint32_t num);
 
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




typedef struct _value
{
  void* PC;
  unsigned long IFR_ID;
  unsigned long pointer;
}VALUE;

#define IFRIT_MAP
#ifdef IFRIT_MAP
// __thread std::map<gpointer, VALUE>* myWriteIFRs = NULL;
// __thread std::map<gpointer, VALUE>* myReadIFRs = NULL;
thread_local std::unordered_map<unsigned long, VALUE> myWriteIFRs;
thread_local std::unordered_map<unsigned long, VALUE> myReadIFRs;
#endif
int lastThreadID;
__thread int threadID;
__thread bool inBeginIFRS;
__thread IFR *raceCheckIFR;

// todo change name of the lock
pthread_mutex_t availabilityLock;
pthread_t threadAvailability[MAX_THDS];

// #define IFRIT_HTM
#ifndef IFRIT_HTM
  // 32 locks, 512 locks
  // only one global lock if not defined
  // #define VARG_MASK_BITS 5
  #define VARG_MASK_BITS 9
#endif

#ifdef VARG_MASK_BITS
  unsigned long VARG_MASK = (((1 << VARG_MASK_BITS) - 1) << 3);
  #define NUM_VARG_MASKS (1 << VARG_MASK_BITS)
  pthread_mutex_t drMutex[NUM_VARG_MASKS];
// #else use availabilityLock
  // pthread_mutex_t drMutex;
#endif

#ifdef VARG_MASK_BITS
  #define TAKE_VARG_MASK(varg) ((varg & VARG_MASK) >> 3)
  #define LOCK_GLOBAL_INFO(varg) pthread_mutex_lock(&drMutex[TAKE_VARG_MASK(varg)])
  #define UNLOCK_GLOBAL_INFO(varg) pthread_mutex_unlock(&drMutex[TAKE_VARG_MASK(varg)])
#else
  #define LOCK_GLOBAL_INFO(varg) pthread_mutex_lock(&availabilityLock)
  #define UNLOCK_GLOBAL_INFO(varg) pthread_mutex_unlock(&availabilityLock)
#endif


#define SAMPLING
#ifdef SAMPLING
bool gSampleState;
pthread_t samplingAlarmThread;
#endif

typedef struct _request
{
  unsigned long pointer;  // data pointer
  unsigned long IFR_ID;
  unsigned int T_Index;
  void* PC;
}REQUEST;

std::vector<REQUEST*> requestsArray[MAX_THDS];

pthread_mutex_t requestLock[MAX_THDS];

// pthread_mutex_t allThreadsLock;
// pthread_t allThreads[MAX_THDS];

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

void printBits(uint32_t num, std::ostringstream& output)
{
   for(int bit=0;bit<(sizeof(uint32_t) * 8); bit++)
   {
      output << (num & 0x01); 
      num = num >> 1;
   }
   output << "\n";
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

#ifdef IFRIT_MAP
  // myWriteIFRs = new std::map<gpointer, VALUE>();
  // myReadIFRs = new std::map<gpointer, VALUE>();
  myWriteIFRs.clear();
  myReadIFRs.clear();
#endif

  pthread_mutex_lock(&availabilityLock);
  if (threadAvailability[(lastThreadID+1)%MAX_THDS] == (pthread_t)0) {
    threadID = (lastThreadID+1)%MAX_THDS;
    threadAvailability[(lastThreadID+1)%MAX_THDS] = pthread_self();
    lastThreadID = (lastThreadID+1)%MAX_THDS;
  } else {
    for (int i = 0; i < MAX_THDS; ++i)   
    {
      if( threadAvailability[i] == (pthread_t)0 ){
        threadID = i;
        threadAvailability[i] = pthread_self();
        lastThreadID = i;
        break;
      }
    }
  }


// fprintf(stderr, "threadID %d created\n", threadID);

  #ifdef SINGLE_THREADED_OPT
    num_threads++;
  #endif
  pthread_mutex_unlock(&availabilityLock);


  requestsArray[threadID].clear();
  inBeginIFRS = false;
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
// fprintf(stderr, "threadID %d destroyed\n", threadID);
  /*Destructor*/
  // if ((myWriteIFRs.size() + myReadIFRs.size()) != 0)
    // fprintf(stderr, "***** tid(%d) in destr myReadIFRs(%d) myWriteIFRs(%d)\n", threadID, myReadIFRs.size(), myWriteIFRs.size());
  // else
    // fprintf(stderr, "***** tid(%d) in dtr myReadIFRs(%d) myWriteIFRs(%d)\n", threadID, myReadIFRs.size(), myWriteIFRs.size());


  IFRit_end_ifrs_internal(0, 0, NULL);

  pthread_mutex_lock(&availabilityLock);
  threadAvailability[threadID] = (pthread_t)0;
  #ifdef SINGLE_THREADED_OPT
    num_threads--;
  #endif
  pthread_mutex_unlock(&availabilityLock);
  
  // pthread_mutex_lock(&allThreadsLock);
  // int i = 0;
  // for(i = 0; i < MAX_THDS; i++){
  //   if( allThreads[i] != (pthread_t)0 && pthread_equal(allThreads[i],pthread_self()) ){
  //     allThreads[i] = 0;
  //     break;
  //   }
  // }
  // pthread_mutex_unlock(&allThreadsLock);


  //fprintf(stderr, "[IFRit] total: %lu redundant: %lu stack: %lu\n", totalStarts, alreadyActive, stackAddress);
  //fprintf(stderr, "[IFRit] Rough insertion weight (thread %p): %lu\n", pthread_self(), insertionCount);

// #ifdef IFRIT_MAP
//   if( myWriteIFRs != NULL ){
//     delete myWriteIFRs;
//     myWriteIFRs = NULL;
//   }
//   if( myReadIFRs != NULL ){
//     delete myReadIFRs;
//     myReadIFRs = NULL;
//   }
// #endif
  delete_ifr(raceCheckIFR);
    // fprintf(stderr, "***** tid(%d) destroyed\n\n", threadID, myReadIFRs.size(), myWriteIFRs.size());

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

__attribute__(( always_inline )) std::string dataraceHandler(int sig) {
    // fprintf(stderr, "****tid(%d) in dataraceHandler requestsArray.size(%d)\n", threadID, requestsArray[threadID].size());
  #ifndef IFRIT_HTM
    pthread_mutex_lock(&requestLock[threadID]);
  #endif
  // if(requestsArray[threadID].size()>0) {
  //   // fprintf(stderr, "%lu %lu items in HT\n",g_hash_table_size(myWriteIFRs), g_hash_table_size(myReadIFRs) );
  // }

  std::ostringstream output;
  for(std::vector<REQUEST*>::iterator it = requestsArray[threadID].begin(); it != requestsArray[threadID].end(); )
  {
    REQUEST* req = *it;
    // fprintf(stderr, "####tid(%d) got req for %p from tid(%d) requestsArray.size(%d)\n", threadID,  (void*)(req->pointer), req->T_Index, requestsArray[threadID].size());

    #ifdef IFRIT_MAP
      std::unordered_map<unsigned long,VALUE>::iterator ite = myWriteIFRs.find(req->pointer);
      if (ite != myWriteIFRs.end()) {

        VALUE value = ite->second;
        output << "***[IFRit] IFR ID: " << req->IFR_ID << " " << value.IFR_ID << " PC: " << req->PC << " "  << value.PC << "\n";
        // fprintf(stderr,"***[IFRit] IFR ID: %lu %" PRIu32 " PC: %p %p\n", req->IFR_ID, value.IFR_ID, req->PC, value.PC);
        
      } else {
        ite = myReadIFRs.find(req->pointer);
        if (ite != myReadIFRs.end()) {

          VALUE value = ite->second;
          output << "***[IFRit] IFR ID: " << req->IFR_ID << " " << value.IFR_ID << " PC: " << req->PC << " "  << value.PC << "\n";
          // fprintf(stderr,"***[IFRit] IFR ID: %lu %" PRIu32 " PC: %p %p\n", req->IFR_ID, value.IFR_ID, req->PC, value.PC);

        } else {
          output << "****not found in dataraceHandler\n";
       //   output << "****tid("<< threadID <<") " << (void*)req->pointer << " not found in dataraceHandler\n";
          // unsigned char buf_fetch[17];
          // _mash_get((unsigned long)req->pointer, (unsigned long)req->pointer, buf_fetch);
          // uint32_t writeBound = *((uint32_t*)buf_fetch);
          // uint32_t readBound = *((uint32_t*)(buf_fetch+4));

       //   printBits(writeBound, output);
       //   printBits(readBound, output);
          // fprintf(stderr, "****not found in dataraceHandler\n");
        }
      }
    #endif
    
      // todo for debuging
      // fprintf(stderr, "****myWriteIFRs.size(%d), myReadIFRs.size(%d)\n", myWriteIFRs.size(), myReadIFRs.size());
      // for (auto i = myWriteIFRs.begin(); i != myWriteIFRs.end(); ++i)
      // {
     //    fprintf(stderr, "****myWriteIFRs %p --> %p\n", (void*)(req->pointer), (void*)i->second.pointer);
      //  if (i-> second.pointer == req->pointer)
      //     fprintf(stderr, "****FOUND in dataraceHandler\n");
      // }
      // for (auto i = myReadIFRs.begin(); i != myReadIFRs.end(); ++i)
      // {
     //    fprintf(stderr, "****myReadIFRs %p --> %p\n", (void*)(req->pointer), (void*)i->second.pointer);
      //  if (i-> second.pointer == req->pointer)
      //     fprintf(stderr, "****FOUND in dataraceHandler\n");
        
      // }
    free(req);
    it = requestsArray[threadID].erase(it);
  }
  #ifndef IFRIT_HTM
    pthread_mutex_unlock(&requestLock[threadID]);
  #endif
  // fprintf(stderr, "****tid(%d) exiting dataraceHandler requestsArray.size(%d)\n", threadID, requestsArray[threadID].size());
  return output.str();
}

void __attribute__((constructor)) IFR_Init(void){
  // signal(SIGINT, sigint);
  // signal(SIGKILL, sigint);
  // signal(SIGSEGV, sigseg);
  // signal(SIGUSR1, dataraceHandlerrrr);
  // _mash_dummy();
  // mpxrt_prepare();
  fprintf(stderr, "[IFRit] Initializing IFR Runtime\n");
  dbprintf(stderr,"Initializing IFR Runtime\n");



#ifdef IFRIT_MAP
  fprintf(stderr, "[IFRit] MPX+MAP-based implementation in use.\n");
#endif

#ifdef SINGLE_THREADED_OPT
  fprintf(stderr, "[IFRit] Single-threaded optimization enabled.\n");
#endif

#ifndef CHECK_FOR_RACES
  fprintf(stderr, "[IFRit] Not checking for races.\n");
#endif

#ifdef VARG_MASK_BITS
  fprintf(stderr, "[IFRit] Partitioning global state into %d partitions.\n",
    NUM_VARG_MASKS);
  
#else
  #ifndef IFRIT_HTM
    fprintf(stderr, "[IFRit] Partitioning global state into 1 partition.\n");
  #endif

#endif

  g_thread_init(NULL);
  
  // pthread_mutex_init(&allThreadsLock, NULL);
  // int i ;
  // for(i = 0; i < MAX_THDS; i++){
  //   allThreads[i] = (pthread_t)0;
  // }

  // allThreads[0] = pthread_self();

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

#ifdef IFRIT_MAP
  // myWriteIFRs = new std::map<gpointer, VALUE>();
  // myReadIFRs = new std::map<gpointer, VALUE>();
  myWriteIFRs.clear();
  myReadIFRs.clear();
#endif

  pthread_mutex_init(&availabilityLock,NULL);
  for (int i = 0; i < MAX_THDS; ++i) {
    threadAvailability[i] = (pthread_t)0;
    pthread_mutex_init(&requestLock[i], NULL);
  }
  threadAvailability[0] = pthread_self();
  threadID = 0;
  lastThreadID = 0;
  inBeginIFRS = false;

  raceCheckIFR = new_ifr(pthread_self(), 0, 0, 0);

#ifdef VARG_MASK_BITS
  for (int i = 0; i < NUM_VARG_MASKS; i++) {
    pthread_mutex_init(&drMutex[i], NULL);
  }
#endif

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

  va_start(ap, num_writes);
  // inBeginIFRS = true;
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

  // inBeginIFRS = false;
#endif
}

// **********************************************************************************************************************************

// **********************************************************************************************************************************
__attribute__(( always_inline )) void IFRit_begin_one_read_ifr_CS
  (unsigned long varg, uint32_t& _writeActive, unsigned long id, void* curProgPC) {

  unsigned char buf_fetch[17];  
  
  uint32_t mask = 0xffffffff; 
  uint32_t currThreadBitPosition = ((uint32_t)0b1) << threadID; 
  mask = (mask & (~currThreadBitPosition)); 
  
  /* Check if any other write IFR is active in the MPX table*/
  _mash_get((unsigned long)varg, (unsigned long)varg, buf_fetch);  
  uint32_t writeBound = *((uint32_t*)buf_fetch);   
  uint32_t writeActive = writeBound&mask; 
  _writeActive = writeActive;
  /* Get READ IFR active in MPX table*/ 
  uint32_t readBound = *((uint32_t*)(buf_fetch+4));

  /*Add READ IFR to MPX table*/ 
  readBound = readBound|currThreadBitPosition;  
  *((uint32_t*) (buf_fetch+4)) = readBound;

  _mash_store((unsigned long)varg, (unsigned long)varg, buf_fetch);

    /* datarace */
  if (writeActive != 0)
  {
    // printBits(writeActive);
    for(unsigned int otherTID=0;otherTID<32; otherTID++)
    {
      if (writeActive & 0x01)
      {
        REQUEST *req = (REQUEST*)malloc(sizeof(REQUEST));
        req->pointer = varg;
        req->T_Index = threadID;
        req->IFR_ID = id;
        req->PC = curProgPC;

        // pthread_mutex_lock(&requestLock[otherTID]);
        requestsArray[otherTID].push_back(req);
        // pthread_mutex_unlock(&requestLock[otherTID]);

        // fprintf(stderr, "****sending signal R-WA from tid(%d) to tid(%d) for %p\n", threadID, otherTID,  (void*)varg);
        // if (pthread_kill(threadAvailability[otherTID], SIGUSR1) != 0) {
        //   fprintf(stderr,"***[IFRit] error cant send signal other thread probably ended\n");
        // }
      }
      writeActive = writeActive >> 1;
    }
  }
}

void printBits(uint32_t num)
{
   for(int bit=0;bit<(sizeof(uint32_t) * 8); bit++)
   {
      fprintf(stderr, "%i ", num & 0x01);
      num = num >> 1;
   }
   fprintf(stderr, "\n");
}



__attribute__(( always_inline )) void IFRit_begin_one_read_ifr(unsigned long id,
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

  #ifdef IFRIT_MAP
    std::unordered_map<unsigned long,VALUE>::iterator it = myReadIFRs.find(varg);
    if (it != myReadIFRs.end()) {
      return;
    }
  #endif

  uint32_t writeActive = 0;

  void *curProgPC = __builtin_return_address(0);  

  #ifdef IFRIT_HTM
    unsigned status = _XABORT_EXPLICIT;
    if ((status = _xbegin ()) == _XBEGIN_STARTED) 
    {
      IFRit_begin_one_read_ifr_CS(varg, writeActive, id, curProgPC);
      _xend ();
    } else {
      pthread_mutex_lock(&availabilityLock);
      IFRit_begin_one_read_ifr_CS(varg, writeActive, id, curProgPC);
      pthread_mutex_unlock(&availabilityLock);
    }
  #else 
      LOCK_GLOBAL_INFO(varg);
      IFRit_begin_one_read_ifr_CS(varg, writeActive, id, curProgPC);
      UNLOCK_GLOBAL_INFO(varg);
  #endif

  #ifdef IFRIT_MAP
    VALUE val;
    val.IFR_ID = id;
    val.pointer = varg;
    val.PC = curProgPC;
    myReadIFRs[varg] = val;
  #endif

}

// **********************************************************************************************************************************
__attribute__(( always_inline )) void IFRit_begin_one_write_ifr_CS(
  unsigned long varg, uint32_t& _readActive, uint32_t& _writeActive, unsigned long id, void* curProgPC) {


  unsigned char buf_fetch[17];  
  uint32_t mask = 0xffffffff; 
  uint32_t currThreadBitPosition = ((uint32_t)0b1) << threadID; 
  mask = (mask & (~currThreadBitPosition)); 

  /* Check if any other write IFR is active in the MPX table*/
  _mash_get((unsigned long)varg, (unsigned long)varg, buf_fetch);  
  uint32_t writeBound = *((uint32_t*)buf_fetch);
  uint32_t writeActive = writeBound&mask; 
  _writeActive = writeActive;

  /* Check if any other read IFR is active in the MPX table*/
  uint32_t readBound = *((uint32_t*)(buf_fetch+4));


  uint32_t readActive = readBound&mask;
  _readActive = readActive;


  /*Add WRITE IFR to MPX table*/
  if (writeActive) {
    // fprintf(stderr, "Orig writeBound for %p\n", (void*) varg);
    // printBits(writeBound);
    // fprintf(stderr, "Orig readBound\n");
    // printBits(readBound);
    writeBound = writeBound|currThreadBitPosition;
    // todo remove
    // assert((void*)(*((uint64_t*) (buf_fetch+8))) == (void*)varg || (void*)(*((uint64_t*) (buf_fetch+8))) == 0);
    // fprintf(stderr, "After writeBound\n");
    // printBits(writeBound);
  } else {
    writeBound = writeBound|currThreadBitPosition;

  }
  // printf("New Write:      ");
  // printBits(writeBound);

  // todo debug remove
  // if(!(void*)(*((uint64_t*) (buf_fetch+8)))) {
  //  if(writeActive)
   //   printBits(writeActive);
  // }
  // if((void*)(*((uint64_t*) (buf_fetch+8))))
  //   fprintf(stderr, "%p->%p\n",(void*)(*((uint64_t*) (buf_fetch+8))), (void*)varg);
  // assert(varg);
    


  *((uint32_t*) buf_fetch) = writeBound;
  // todo debug remove
  // *((uint64_t*) (buf_fetch+8)) = (uint64_t)varg;
  _mash_store((unsigned long)varg, (unsigned long)varg, buf_fetch); 


  /* datarace */
  if (writeActive != 0) {
    for(unsigned int otherTID=0;otherTID<32; otherTID++)
    {
      if (writeActive & 0x01)
      {
        REQUEST* req = (REQUEST*)malloc(sizeof(REQUEST));
        req->pointer = varg;
        req->T_Index = threadID;
        req->IFR_ID = id;
        req->PC = curProgPC;
        
        // pthread_mutex_lock(&requestLock[otherTID]);
        requestsArray[otherTID].push_back(req);
        // pthread_mutex_unlock(&requestLock[otherTID]);

      }
      writeActive = writeActive >> 1;
    }
  }

  if (readActive != 0) {
    for(unsigned int otherTID=0;otherTID<32; otherTID++)
    {
      if (readActive & 0x01)
      {
        REQUEST* req = (REQUEST*)malloc(sizeof(REQUEST));
        req->pointer = varg;
        req->T_Index = threadID;
        req->IFR_ID = id;
        req->PC = curProgPC;

        // pthread_mutex_lock(&requestLock[otherTID]);
        requestsArray[otherTID].push_back(req);
        // pthread_mutex_unlock(&requestLock[otherTID]);
      }
      readActive = readActive >> 1;
    }
  }

}

    // fprintf(stderr,"***[IFRit] [WR] %lu ", writeBound); \
    // fprintf(stderr,"%lu ***\n", writeBound); \
    // fprintf(stderr,"stored in mpx***\n", writeBound); \

__attribute__(( always_inline )) void IFRit_begin_one_write_ifr(unsigned long id, 
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
  #ifdef IFRIT_MAP
    std::unordered_map<unsigned long,VALUE>::iterator it = myWriteIFRs.find(varg);
    if (it != myWriteIFRs.end()) {
      return;
    }
  #endif
  uint32_t readActive = 0;
  uint32_t writeActive = 0;
  
  void *curProgPC = __builtin_return_address(0);  

  #ifdef IFRIT_HTM
    unsigned status = _XABORT_EXPLICIT;
    if ((status = _xbegin ()) == _XBEGIN_STARTED) 
    {
      IFRit_begin_one_write_ifr_CS(varg, readActive, writeActive, id, curProgPC);
      _xend ();
    } else {
      pthread_mutex_lock(&availabilityLock);
      IFRit_begin_one_write_ifr_CS(varg, readActive, writeActive, id, curProgPC);
      pthread_mutex_unlock(&availabilityLock);
    }
  #else 
      LOCK_GLOBAL_INFO(varg);
      IFRit_begin_one_write_ifr_CS(varg, readActive, writeActive, id, curProgPC);
      UNLOCK_GLOBAL_INFO(varg);
  #endif


  /* Add IFR to thread local WRITE IFR hashtable */
  #ifdef IFRIT_MAP
    VALUE val;
    val.IFR_ID = id;
    val.pointer = varg;
    val.PC = curProgPC;
    myWriteIFRs[varg] = val;
    // myWriteIFRs.insert ( std::pair<gpointer,VALUE>((gpointer)varg, val));
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

__attribute__(( always_inline )) void process_end_read_CS(unsigned long varg) {

  unsigned char buf_fetch[17];  
  _mash_get((unsigned long)varg, (unsigned long)varg, buf_fetch);  

  uint32_t mask = 0xffffffff; 
  uint32_t currThreadBitPosition = ((uint32_t)0b1) << threadID;
  mask = (mask & (~currThreadBitPosition));


  /* Get READ IFR active in MPX table*/
  uint32_t readBound = *((uint32_t*)(buf_fetch+4));

  /*Remove READ IFR from MPX table*/
  readBound = readBound&mask;
  *((uint32_t*) (buf_fetch+4)) = readBound;

  _mash_store((unsigned long)varg, (unsigned long)varg, buf_fetch);
}


/* Process an active read IFR for an end IFRs action. Returns true if
   the IFR should be deleted from local state. */
gboolean process_end_read_map(unsigned long key, gpointer user_data) {
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

  #ifdef IFRIT_HTM
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
  #else 
      LOCK_GLOBAL_INFO(varg);
      process_end_read_CS(varg);
      UNLOCK_GLOBAL_INFO(varg);
  #endif

  return TRUE;
}

void delete_read(unsigned long varg) {
  #ifdef IFRIT_HTM
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
  #else 
      LOCK_GLOBAL_INFO(varg);
      process_end_read_CS(varg);
      UNLOCK_GLOBAL_INFO(varg);
  #endif
}


__attribute__(( always_inline )) void process_end_write_CS(unsigned long varg, struct EndIFRsInfo* endIFRsInfo, bool downgrade) {

  unsigned char buf_fetch[17]; 

  _mash_get((unsigned long)varg, (unsigned long)varg, buf_fetch);  
  
  uint32_t mask = 0xffffffff;
  uint32_t currThreadBitPosition = ((uint32_t)0b1) << threadID;
  mask = (mask & (~currThreadBitPosition));

  uint32_t writeBound = *((uint32_t*)buf_fetch);
  
  /*delete write from MPX*/
  // printBits(writeBound);
  writeBound = writeBound&mask;
  // printBits(writeBound);
  // fprintf(stderr, "tid(%d)\n", threadID);
  *((uint32_t*) buf_fetch) = writeBound;

  // uint64_t t = *((uint64_t*) (buf_fetch+8));
  // if(*((uint64_t*) (buf_fetch+8)) != (uint64_t)varg) {
  //   fprintf(stderr, "=%p\t%p\n", t, varg);
  // }
  // assert(*((uint64_t*) (buf_fetch+8)) == (uint64_t)varg);



  if (downgrade) {
    /* Get READ IFR active in MPX table*/
    uint32_t readBound = *((uint32_t*)(buf_fetch+4));

    /*Add READ IFR to MPX table*/
    readBound = readBound|currThreadBitPosition;
    *((uint32_t*) (buf_fetch+4)) = readBound;

    /*todo remove this*/
    endIFRsInfo->downgradeVars[endIFRsInfo->numDowngrade] = varg;
    endIFRsInfo->numDowngrade = endIFRsInfo->numDowngrade + 1;
    assert(endIFRsInfo->numDowngrade <= endIFRsInfo->numMay);
  }

  _mash_store((unsigned long)varg, (unsigned long)varg, buf_fetch);
}

/* Process an active write IFR during end_ifrs. Returns true if the
   write should be deleted from the local state. */
#ifdef IFRIT_MAP
bool process_end_write_map(unsigned long key, VALUE value, struct EndIFRsInfo * endIFRsInfo) {
  unsigned long varg = (unsigned long) key;

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

    if (endIFRsInfo->mayArgs[q] == varg && (myReadIFRs.find(key) == myReadIFRs.end())) {
      downgrade = true;
      break;
    }
  }

  #ifdef IFRIT_HTM
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
  #else 
      LOCK_GLOBAL_INFO(varg);
      process_end_write_CS(varg, endIFRsInfo, downgrade);
      UNLOCK_GLOBAL_INFO(varg);
  #endif


  /*Downgrade*/
  if (downgrade) {
    myReadIFRs[key] = value;
  }

  return TRUE;
}
#endif
void delete_write(unsigned long varg) {
  #ifdef IFRIT_HTM
    unsigned status = _XABORT_EXPLICIT;
    if ((status = _xbegin ()) == _XBEGIN_STARTED) 
    {
      process_end_write_CS(varg, NULL, false);
      _xend ();
    } else {
      pthread_mutex_lock(&availabilityLock);
      process_end_write_CS(varg, NULL, false);
      pthread_mutex_unlock(&availabilityLock);
    }
  #else 
      LOCK_GLOBAL_INFO(varg);
      process_end_write_CS(varg, NULL, false);
      UNLOCK_GLOBAL_INFO(varg);
  #endif
}

// **********************************************************************************************************************************
void IFRit_end_ifrs_internal(unsigned long numMay, unsigned long numMust, va_list *ap) {
// 
#ifdef IFRIT_MAP
  // if (myWriteIFRs == NULL || myReadIFRs == NULL) {
    // return;
  // }
  if ((myWriteIFRs.size() + myReadIFRs.size()) == 0) {
    return;
  }
#endif

  std::string output;
  #ifdef IFRIT_HTM
    unsigned status = _XABORT_EXPLICIT;
    if ((status = _xbegin ()) == _XBEGIN_STARTED) 
    {
      output = dataraceHandler(0);
      _xend ();
    } else {
      pthread_mutex_lock(&availabilityLock);
      output = dataraceHandler(0);
      pthread_mutex_unlock(&availabilityLock);
    }
  #else 
      // LOCK_GLOBAL_INFO(varg);
      output = dataraceHandler(0);
      // UNLOCK_GLOBAL_INFO(varg);
  #endif
      fprintf(stderr, "%s", output.c_str());
  // if (numMay == 0 && numMust == 0 && ap == NULL)
    // fprintf(stderr, "1. tid(%d) in dtr myReadIFRs(%d) myWriteIFRs(%d)\n", threadID, myReadIFRs.size(), myWriteIFRs.size());

  // in case of thread destruction
  // if (numMay == 0 && numMust == 0 && ap == NULL) {
  //  for(auto i=myWriteIFRs.cbegin(); i!=myWriteIFRs.cend();++i) {
      
   //    delete_write(i->first);
   //  }
   //  for(auto i=myReadIFRs.begin(); i!=myReadIFRs.end();) {    
   //    delete_read(i->first);
   //  }
   //  myReadIFRs.clear();
   //  myWriteIFRs.clear();
   //  return;
  // }

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


  #ifdef IFRIT_MAP
  for(auto i=myWriteIFRs.cbegin(); i!=myWriteIFRs.cend();)
  {
    if (process_end_write_map(i->first, i->second, endIFRsInfo)) {
      i = myWriteIFRs.erase(i);
    } else {
      ++i;
    }
  }

  for(auto i=myReadIFRs.begin(); i!=myReadIFRs.end();)
  {    
    if (process_end_read_map((*i).first, (gpointer)endIFRsInfo)) {
      i = myReadIFRs.erase(i);
    } else {
      ++i;
    }
  }
  /*Process_end_read*/
    /*dont delete if mayArg is in myReadIFRs*/
    /*dont delete if mustArg is in myReadIFRs*/
    /*else delete in both MPX + local*/
  #endif

  /*add downgraded IFRs*/
  // add_ifrs_to_local_state(endIFRsInfo->numDowngrade, endIFRsInfo->downgradeVars);

  free(endIFRsInfo->mayArgs);
  free(endIFRsInfo->mustArgs);
  free(endIFRsInfo);
  // if (numMay == 0 && numMust == 0 && ap == NULL)
  // fprintf(stderr, "2. tid(%d) in dtr myReadIFRs(%d) myWriteIFRs(%d)\n", threadID, myReadIFRs.size(), myWriteIFRs.size());

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

