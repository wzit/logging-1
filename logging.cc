/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 wardenlym
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <logging.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <sys/stat.h>

using namespace std;
using namespace logging;

volatile level logging::enabled_level = INFO;
logger logging::stdout("stdout",stream(std::cout));

static void mkdir_unless_exsit(const char *dir);
static int  rotate_file(int fd, const char* path, const char* prefix, char* fnbuf, size_t bufsz, const char* time, const char* suffix);
static long kernel_mktime(struct tm * tm);

logging_backend::logging_backend(bool async,string dir,string prefix,string backend_name, string suffix,int rotate_M,int bufsz_K,int flush_sec)
  :async_(async)
  ,dir_(dir)
  ,prefix_(prefix)
  ,suffix_(suffix)
  ,rotate_sz_(1024*1024*rotate_M)
  ,buf_capacity_(1024*bufsz_K)
  ,name_(backend_name)
  ,flush_interval_(flush_sec)
  ,pthreadid_(-1)
  ,tid_(-1)
  ,running_(false)
  ,fd_(-1)
{
  mkdir_unless_exsit(dir_.c_str());
  update_time();
  fd_ = rotate_file(fd_,dir_.c_str(),prefix_.c_str(),filename_buf_,sizeof(filename_buf_),time_buf_,suffix_.c_str());
  if (pthread_mutex_init(&mutex_, NULL) != 0) throw "mutex init error";
  if (pthread_cond_init(&cond_, NULL) != 0) throw "condition init error";
  if (async_) start();
}

logging_backend::~logging_backend()
{
  if (async_) {
    stop_and_join();
    pthread_detach(pthreadid_);
  }
  pthread_mutex_destroy(&mutex_);
  pthread_cond_destroy(&cond_);
  close(fd_);
}

void* logging::_starter(void *arg)
{
  logging_backend *self = static_cast<logging_backend*>(arg);
  self->thread_main();
  return NULL;
}

bool logging_backend::start()
{
  if (not async_) return true;

  if (pthread_create(&pthreadid_, NULL, _starter, this) == 0) {
    running_ = true;
    return true;
  } else {
    running_ = false;
    return false;
  }
}

void logging_backend::stop_and_join()
{
  if (not async_)  return;

  if (running_) {
    pthread_mutex_lock(&mutex_);
    running_ = false;
    pthread_cond_signal(&cond_);
    pthread_mutex_unlock(&mutex_);
    pthread_join(pthreadid_, NULL);
  }
}

void logging_backend::append(const char* line, size_t len)
{
  if (not async_) {
      pthread_mutex_lock(&mutex_);
      sync_to_file(line,len);
      pthread_mutex_unlock(&mutex_);
      return;
  }

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

static int rotate_file(int fd, const char* path, const char* prefix, char* fnbuf, size_t bufsz, const char* time, const char* suffix)
{
  if(fd != -1) ::close(fd);
  snprintf(fnbuf, bufsz, "%s%s.%s%s", path, prefix, time, suffix);
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
      if(-1 == system(mkdir.c_str()))
	printf("%s\n", strerror(errno));
  }
}

static bool need_rotate_by_time(const struct tm *last,const struct tm *now,bool rotate_by_hour=true,bool rotate_by_day=true)
{
  if (rotate_by_day && not rotate_by_hour) {
    struct tm l = *last; l.tm_hour=0;l.tm_min=0;l.tm_sec = 0;
    struct tm n = *now;  n.tm_hour=0;n.tm_min=0;n.tm_sec = 0;
    return kernel_mktime(&l) < kernel_mktime(&n);
  } else if (rotate_by_hour) {
      struct tm l = *last; l.tm_min=0;l.tm_sec = 0;
      struct tm n = *now;  n.tm_min=0;n.tm_sec = 0;
      return kernel_mktime(&l) < kernel_mktime(&n);
  } else {
    return false;
  }
}

static bool need_rotate_by_size(int fd,size_t size)
{
  // may not stat every time;
  struct stat stat;
  if (fstat(fd, &stat) != 0)
    return true;
  else
    return (size_t)stat.st_size > size ? true : false;
}

static void write_noreturn(int fd, const char *m, size_t s)
{
  int total = 0;
  while(s > 0) {
    int written = ::write(fd,m,s);
    if (written == -1) {
	if (errno == EINTR) continue;
	printf("%d,%s\n",fd,strerror(errno));return;
    }
    m += written;
    total += written;
    s -= written;
  }
}

void logging_backend::update_time()
{
  gettimeofday(&now_,NULL);
  tm_last_ = tm_now_;
  localtime_r(&now_.tv_sec, &tm_now_);
  int usec = static_cast<int>(now_.tv_usec % (1000 * 1000));
  snprintf(time_buf_, sizeof(time_buf_), "%4d%02d%02d%02d.%02d.%02d%02d%06d",
	   tm_now_.tm_year + 1900,tm_now_.tm_mon + 1,tm_now_.tm_mday,tm_now_.tm_hour,
	   tm_now_.tm_hour, tm_now_.tm_min, tm_now_.tm_sec, usec);
}

void logging_backend::sync_to_file(const char* line, size_t len)
{
  update_time();
  if (need_rotate_by_size(fd_,rotate_sz_) ||
      need_rotate_by_time(&tm_last_,&tm_now_)) {
      fd_ = rotate_file(fd_,dir_.c_str(),prefix_.c_str(),filename_buf_,sizeof(filename_buf_),time_buf_,suffix_.c_str());
  }
  write_noreturn(fd_,line,len);
}

void logging_backend::thread_main(void)
{
  ::prctl(PR_SET_NAME, name_.c_str());
  this->tid_ = static_cast<pid_t>(::syscall(SYS_gettid));
  bool looping = true;

  do{
      { // swap front/back-end buffer in the CS.
	pthread_mutex_lock(&mutex_);
	// maybe? use CLOCK_MONOTONIC or CLOCK_MONOTONIC_RAW to prevent time rewind.
	struct timespec abstime;
	clock_gettime(CLOCK_REALTIME, &abstime);
	abstime.tv_sec += flush_interval_;
	pthread_cond_timedwait(&cond_, &mutex_, &abstime);
	swap(buf_vec_,buf_vec_backend_);
	if (not running_) looping = false;
	pthread_mutex_unlock(&mutex_);
      }

    if (buf_vec_backend_[0]->size()==0 && looping)
      continue;

    update_time();

    if (need_rotate_by_size(fd_,rotate_sz_) ||
        need_rotate_by_time(&tm_last_,&tm_now_)) {
        fd_ = rotate_file(fd_,dir_.c_str(),prefix_.c_str(),filename_buf_,sizeof(filename_buf_),time_buf_,suffix_.c_str());
    }

    for (size_t i=0; i < buf_vec_backend_.size(); ++i) {
	buf_ptr &buf = buf_vec_backend_.at(i);
	if (buf->size() != 0)
	  ::write_noreturn(fd_,buf->c_str(),buf->size());
	buf->reuse();
    }
  } while(looping);
  close(fd_);
  fd_=-1;
}

/*
 *  linux/kernel/mktime.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <time.h>

/*
 * This isn't the library routine, it is only used in the kernel.
 * as such, we don't care about years<1970 etc, but assume everything
 * is ok. Similarly, TZ etc is happily ignored. We just do everything
 * as easily as possible. Let's find something public for the library
 * routines (although I think minix times is public).
 */
/*
 * PS. I hate whoever though up the year 1970 - couldn't they have gotten
 * a leap-year instead? I also hate Gregorius, pope or no. I'm grumpy.
 */
#define MINUTE 60
#define HOUR (60*MINUTE)
#define DAY (24*HOUR)
#define YEAR (365*DAY)

/* interestingly, we assume leap-years */
static int month[12] = {
    0,
    DAY*(31),
    DAY*(31+29),
    DAY*(31+29+31),
    DAY*(31+29+31+30),
    DAY*(31+29+31+30+31),
    DAY*(31+29+31+30+31+30),
    DAY*(31+29+31+30+31+30+31),
    DAY*(31+29+31+30+31+30+31+31),
    DAY*(31+29+31+30+31+30+31+31+30),
    DAY*(31+29+31+30+31+30+31+31+30+31),
    DAY*(31+29+31+30+31+30+31+31+30+31+30)
};

static long kernel_mktime(struct tm * tm)
{
    long res;
    int year;

    year = tm->tm_year - 70;
/* magic offsets (y+1) needed to get leapyears right.*/
    res = YEAR*year + DAY*((year+1)/4);
    res += month[tm->tm_mon];
/* and (y+2) here. If it wasn't a leap-year, we have to adjust */
    if (tm->tm_mon>1 && ((year+2)%4))
        res -= DAY;
    res += DAY*(tm->tm_mday-1);
    res += HOUR*tm->tm_hour;
    res += MINUTE*tm->tm_min;
    res += tm->tm_sec;
    return res;
}
