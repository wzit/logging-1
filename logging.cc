#include <logging.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <fcntl.h>

using namespace std;
using namespace logging;

logging_backend::logging_backend(string dir,string prefix,string backend_name, string suffix,int rotate_M,int bufsz_M,int flush_sec)
  :dir_(dir)
  ,prefix_(prefix)
  ,suffix_(suffix)
  ,rotate_sz_(1024*1024*rotate_M)
  ,buf_capacity_(1024*1024*bufsz_M)
  ,name_(backend_name)
  ,flush_interval_(flush_sec)
  ,pthreadid_(-1)
  ,tid_(-1)
  ,running_(false)
  ,fd_(-1)
  ,num_(0)
{
  struct timeval now;
  gettimeofday(&now,NULL);
  localtime_r(&now.tv_sec, &tm_last_);
  if (pthread_mutex_init(&mutex_, NULL) != 0) throw "mutex init error";
  if (pthread_cond_init(&cond_, NULL) != 0) throw "condition init error";
}

logging_backend::~logging_backend()
{
  stop_and_join();
  pthread_detach(pthreadid_);
  pthread_cond_destroy(&cond_);
  pthread_mutex_destroy(&mutex_);
  close(fd_);
}

static void *_starter(void *arg)
{
  logging_backend *backend = static_cast<logging_backend*>(arg);
  backend->thread_main();
  return NULL;
}

bool logging_backend::start()
{
  running_ = true;
  if (pthread_create(&pthreadid_, NULL, _starter, this) == 0) {
    return true;
  } else {
    running_ = false;
    return false;
  }
}

void logging_backend::stop_and_join()
{
  if (running_) {
    running_ = false;
    pthread_mutex_lock(&mutex_);
    pthread_cond_signal(&cond_);
    pthread_mutex_unlock(&mutex_);
    pthread_join(pthreadid_, NULL);
  }
}

void logging_backend::append(const char* line, size_t len)
{
  pthread_mutex_lock(&mutex_);
  size_t i = 0;
  // find the first one not-full buf in vector.
  for (;i < buf_vec_.size();++i) {
      if(not buf_vec_[i]->full())
	  break;
  }
  // all buf are full or no buf exsit, new one
  if (i >= buf_vec_.size())
    buf_vec_.push_back(buf_ptr(new buf(buf_capacity_)));

  //
  if (buf_vec_[i]->rest() > len) {
      buf_vec_[i]->push_back(line,len);
  } else {
      buf_vec_[i]->filled();
      if (i == (buf_vec_.size()-1))
	buf_vec_.push_back(buf_ptr(new buf(buf_capacity_)));

      buf_vec_[++i]->push_back(line,len);
      pthread_cond_signal(&cond_);
  }
  pthread_mutex_unlock(&mutex_);
}

static int rotate_file(int fd, const char* path, const char* prefix, char* fnbuf, size_t bufsz, const char* time, size_t num, const char* suffix)
{
  if(fd != -1) ::close(fd);
  snprintf(fnbuf, bufsz, "%s%s.%s.%zu%s", path, prefix, time, num, suffix);
  fd = ::open(fnbuf, O_WRONLY|O_CREAT|O_TRUNC, 0644);
  if (fd == -1) {
      printf("%d,%s %s\n",fd,strerror(errno),fnbuf);
  }
  return fd;
}

static void mkdir_unless_exsit(const char *dir)
{
  if (-1 == access(dir,F_OK)) {
      string mkdir = string("mkdir -p ") + dir;
      system(mkdir.c_str());
  }
}

static bool time_need_rotate(const struct tm *,const struct tm *)
{
  return true;
}

static bool size_need_rotate(int fd,size_t size)
{
  return true;
}

void logging_backend::thread_main(void)
{
  ::prctl(PR_SET_NAME, name_.c_str());
  this->tid_ = static_cast<pid_t>(::syscall(SYS_gettid));

  //open file
  { // here will use check if file exsit and change file name
    mkdir_unless_exsit(dir_.c_str());
    struct timeval now;
    gettimeofday(&now,NULL);
    struct tm tm_now;
    localtime_r(&now.tv_sec, &tm_now);
    snprintf(time_buf_, sizeof(time_buf_), "%4d%02d%02d%02d",
	     tm_now.tm_year + 1900,tm_now.tm_mon + 1,tm_now.tm_mday,tm_now.tm_hour);
    fd_ = rotate_file(fd_,dir_.c_str(),prefix_.c_str(),filename_buf_,sizeof(filename_buf_),time_buf_,num_,suffix_.c_str());
  }

  do{
    { // swap front/back-end buffer in the cs.
      pthread_mutex_lock(&mutex_);

      // FIXME: use CLOCK_MONOTONIC or CLOCK_MONOTONIC_RAW to prevent time rewind.
      struct timespec abstime;
      clock_gettime(CLOCK_REALTIME, &abstime);
      abstime.tv_sec += flush_interval_;

      pthread_cond_timedwait(&cond_, &mutex_, &abstime);

      //printf("     front %zu | back %zu\n",buf_vec_.size(),buf_vec_backend_.size());
      swap(buf_vec_,buf_vec_backend_);
      //printf("swap:front %zu | back %zu\n",buf_vec_.size(),buf_vec_backend_.size());
      pthread_mutex_unlock(&mutex_);
    }

    if (running_ && buf_vec_backend_[0]->size()==0) {
      continue;
    }

    struct timeval now;
    gettimeofday(&now,NULL);
    struct tm tm_now;
    localtime_r(&now.tv_sec, &tm_now);

    mkdir_unless_exsit(dir_.c_str());

    bool need_rotate_by_size = size_need_rotate(fd_,rotate_sz_);
    bool need_rotate_by_time = time_need_rotate(&tm_last_,&tm_now);

    if (need_rotate_by_size) {
	num_+=1;
    }
    if (need_rotate_by_time) {
	num_=0;
    }

    if (0){//need_rotate_by_time or need_rotate_by_size or !running_) {
      snprintf(time_buf_, sizeof(time_buf_), "%4d%02d%02d%02d",
               tm_now.tm_year + 1900,tm_now.tm_mon + 1,tm_now.tm_mday,tm_now.tm_hour);
      fd_ = rotate_file(fd_,dir_.c_str(),prefix_.c_str(),filename_buf_,sizeof(filename_buf_),time_buf_,num_,suffix_.c_str());
      printf("rotate?\n");
    }

    for (size_t i=0; i < buf_vec_backend_.size(); ++i) {
	buf_ptr &buf = buf_vec_backend_.at(i);
	if (buf->size() != 0)
	  //printf("writing : %zu ,",i);
	  ::write(fd_,buf->c_str(),buf->size());
	buf->reuse();
    }
    //printf("\n");
    tm_last_ = tm_now;
  } while(running_);
  close(fd_);
  fd_=-1;
}
