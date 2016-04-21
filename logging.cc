#include <logging.h>
#include <time.h>
#include <errno.h>

using namespace std;
using namespace logging;

static bool wait_for_seconds(int seconds, pthread_mutex_t *mutex, pthread_cond_t *cond)
{
  struct timespec abstime;
  // FIXME: use CLOCK_MONOTONIC or CLOCK_MONOTONIC_RAW to prevent time rewind.
  clock_gettime(CLOCK_REALTIME, &abstime);
  abstime.tv_sec += seconds;
  pthread_mutex_lock(mutex);
  bool rv = (ETIMEDOUT == pthread_cond_timedwait(cond, mutex, &abstime));
  pthread_mutex_lock(mutex);
  return rv;
}

static void notify(pthread_mutex_t *mutex, pthread_cond_t *cond)
{
  pthread_mutex_lock(mutex);
  pthread_cond_signal(cond);
  pthread_mutex_unlock(mutex);
}

logging_backend::logging_backend(std::string path,string prefix,string backend_name, string suffix,int rotate_M,int bufsz_M,int flush_sec)
  :path_(path)
  ,prefix_(prefix)
  ,suffix_(suffix)
  ,rotate_sz_(1024*1024*rotate_M)
  ,buf_sz_(1024*1024*bufsz_M)
  ,name_(backend_name)
  ,flush_interval_(flush_sec)
  ,running_(false)
{
  pthread_mutex_init(&mutex_, NULL);
  pthread_cond_init(&cond_, NULL);
//  if (0 != pthread_create(&pthreadid_, NULL, thr_fn, NULL)) {
//      throw "log thread create failure";
//  }
}

logging_backend::~logging_backend()
{

}

void logging_backend::start()
{

}

void logging_backend::stop_and_join()
{

}

void logging_backend::append(const char* line, size_t len)
{

}

void logging_backend::backend_thread(void)
{

}
