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
#ifndef LOGGING_H_
#define LOGGING_H_

#include <pthread.h>

#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/syscall.h>

#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace logging {
/*
 *  simple logging interface, two classes, lean concept:
 *
 *  logger  => format one line | a logic module | an ordinary object
 *     n
 *     |
 *    0/1 (*)
 *  backend => format filename | create/write/flush/rotate file | a posix thread (if working under async mode)
 *
 *  (*) a logger with no backend direct output to std::cout.
 *  modify source directly to customize formatter.
 *
 */
class logger;
class backend;

enum level { DEBUG, INFO, ERROR, FATAL };

level _enabled = INFO;
void enable(level level) { _enabled = level; }

extern logger stdout, stderr, stdlog;

#define LOG_DEBUG(l) if(logging::_enabled<=logging::DEBUG) logging::formatter(l,"DEBUG",__FILE__,__LINE__,__FUNCTION__).stream()
#define LOG_INFO(l)  if(logging::_enabled<=logging::INFO ) logging::formatter(l,"INFO ",__FILE__,__LINE__,__FUNCTION__).stream()
#define LOG_ERROR(l) if(logging::_enabled<=logging::ERROR) logging::formatter(l,"ERROR",__FILE__,__LINE__,__FUNCTION__).stream()
#define LOG_FATAL(l) if(logging::_enabled<=logging::FATAL) logging::formatter(l,"FATAL",__FILE__,__LINE__,__FUNCTION__).stream()

/*****************************************************************************/

/*
 *  implementation
 */
using std::string;
using std::ostream;
using std::ostringstream;
using std::unique_ptr;
using std::vector;

class stream
{
  ostream &os_;

public:
  stream(ostream &os) :os_(os) {}
  template<typename T>
  stream& operator<<(const T &t) { os_ << t; return *this; }
};

class buf
{
private:
  buf(const buf&) = delete;
  const buf& operator=(const buf&) = delete;
  size_t idx;
  const size_t capacity;
  char *m;
  bool full_;

public:
  buf(size_t size) :idx(0), capacity(size), m(nullptr), full_(false) { m = new char[capacity]; }
  void append(const char *s, size_t len) { memcpy(m + idx, s, len); idx += len; }
  ~buf()              { delete[] m; }
  size_t rest()       { return capacity - idx; }
  const char *c_str() { return m; }
  size_t size()       { return idx; }
  void reuse()        { idx = 0; full_ = false; }
  bool full()         { return full_; }
  void filled()       { full_ = true; }
};

typedef unique_ptr<buf> buf_ptr;
typedef vector<buf_ptr> bufs;

void* _starter(void *arg);

class backend
{
private:
  const bool   async_;
  const string dir_;
  const string prefix_;
  const string suffix_;
  const size_t rotate_sz_;
  const bool   rotate_byday_;
  const bool   rotate_byhour_;
  const size_t buf_capacity_;
  const string name_;
  const size_t flush_interval_;

  pthread_t       pthreadid_;
  pid_t           tid_;
  pthread_mutex_t mutex_;
  pthread_cond_t  cond_;

  bool running_;
  int  fd_;
  char fn_buf_[512] = {0};
  char time_buf_[32] = {0};

  struct tm tm_last_;
  struct tm tm_now_;
  struct timeval now_;

  bufs bufs_;
  bufs bufs_backend_;

public:
  backend( bool   async=false
	  ,string dir="./log/"
	  ,string prefix="log"
	  ,string name="log"
	  ,string suffix=".log"
	  ,size_t rotate_M=100
	  ,bool   rotate_byday=true
	  ,bool   rotate_byhour=false
	  ,size_t bufsz_K=1
	  ,size_t flush_sec=3)
    :async_(async)
    ,dir_(dir)
    ,prefix_(prefix)
    ,suffix_(suffix)
    ,rotate_sz_(1024*1024*rotate_M)
    ,rotate_byday_(rotate_byday)
    ,rotate_byhour_(rotate_byhour)
    ,buf_capacity_(1024*bufsz_K)
    ,name_(name)
    ,flush_interval_(flush_sec)
    ,pthreadid_(-1)
    ,tid_(-1)
    ,running_(false)
    ,fd_(-1)
  {
    mkdir_unless_exsit(dir_.c_str());
    update_time();
    fd_ = rotate_file(fd_,dir_.c_str(),prefix_.c_str(),fn_buf_,sizeof(fn_buf_),time_buf_,suffix_.c_str());
    if (pthread_mutex_init(&mutex_, NULL) != 0) throw "mutex init error";
    if (pthread_cond_init(&cond_, NULL) != 0) throw "condition init error";
    if (async_) start();
  }

  ~backend() {
    if (async_) {
      stop_and_join();
      pthread_detach(pthreadid_);
    }
    pthread_mutex_destroy(&mutex_);
    pthread_cond_destroy(&cond_);
    close(fd_);
  }

  void append(const char* line, size_t len) {
    if (not async_) {
      pthread_mutex_lock(&mutex_);
      sync_append(line,len);
      pthread_mutex_unlock(&mutex_);
      return;
    }

    pthread_mutex_lock(&mutex_);
    size_t i = 0;
/* find the first of write-able buffer in vector. */
    for (; i < bufs_.size(); ++i)
      if(not bufs_[i]->full()) break;

/* all buffer are full or no buffer exsits, new one. */
    if (i >= bufs_.size())
      bufs_.push_back(buf_ptr(new buf(std::max(buf_capacity_, len*2))));

    if (bufs_[i]->rest() > len) {
      bufs_[i]->append(line,len);
    } else {
      bufs_[i]->filled();
/* rest not enough, new one. */
      if (i == (bufs_.size()-1))
        bufs_.push_back(buf_ptr(new buf(std::max(buf_capacity_, len*2))));

      bufs_[++i]->append(line,len);
      pthread_cond_signal(&cond_);
    }
    pthread_mutex_unlock(&mutex_);
  }

private:
  backend(const backend&) = delete;
  const backend& operator=(const backend&) = delete;

  friend void *_starter(void *arg);
  bool start() {
    if (not async_) return true;
    if (pthread_create(&pthreadid_, NULL, _starter, this) == 0) {
      pthread_mutex_lock(&mutex_);
      running_ = true;
      pthread_mutex_unlock(&mutex_);
      return true;
    } else {
      return false;
    }
  }

  void stop_and_join() {
    if (not async_) return;
    if (running_) {
      pthread_mutex_lock(&mutex_);
      running_ = false;
      pthread_cond_signal(&cond_);
      pthread_mutex_unlock(&mutex_);
      pthread_join(pthreadid_, NULL);
    }
  }

  void sync_append(const char* line, size_t len) {
    update_time();
    if (need_rotate_by_size(fd_,rotate_sz_) or
        need_rotate_by_time(&tm_last_,&tm_now_,rotate_byday_,rotate_byhour_)) {
      fd_ = rotate_file(fd_,dir_.c_str(),prefix_.c_str(),fn_buf_,sizeof(fn_buf_),time_buf_,suffix_.c_str());
    }
    write_noreturn(fd_,line,len);
  }

  void update_time() {
    ::gettimeofday(&now_,NULL);
    tm_last_ = tm_now_;
    ::localtime_r(&now_.tv_sec, &tm_now_);
    int usec = static_cast<int>(now_.tv_usec % (1000 * 1000));
    // TODO: may not format every msec, store prefix ymdhms
    snprintf(time_buf_, sizeof(time_buf_), "%4d%02d%02d%02d.%02d.%02d%02d%06d",
	     tm_now_.tm_year + 1900,tm_now_.tm_mon + 1,tm_now_.tm_mday,tm_now_.tm_hour,
	     tm_now_.tm_hour, tm_now_.tm_min, tm_now_.tm_sec, usec);
  }

  void mkdir_unless_exsit(const char *dir) {
    if (-1 == ::access(dir,F_OK)) {
      string mkdir = string("mkdir -p ") + dir;
      if(-1 == system(mkdir.c_str())) printf("%s\n", strerror(errno));
    }
  }
  
  int rotate_file(int fd, const char* path, const char* prefix, char* fnbuf, size_t bufsz, const char* time, const char* suffix) {
    if(fd != -1) ::close(fd);
    snprintf(fnbuf, bufsz, "%s%s.%s%s", path, prefix, time, suffix);
    fd = ::open(fnbuf, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (fd == -1) printf("%d,%s %s\n",fd,strerror(errno),fnbuf);
    return fd;
  }

  long fast_mktime(struct tm * tm) {
    const int MINUTE =60;
    const int HOUR =(60*MINUTE);
    const int DAY =(24*HOUR);
    const int YEAR =(365*DAY);

/* interestingly, we assume leap-years */
    static const int month[12] = {
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

  bool need_rotate_by_time(const struct tm *last, const struct tm *now, bool rotate_byday, bool rotate_byhour) {
    struct tm l = *last, n = *now;
    l.tm_min=0; l.tm_sec = 0; n.tm_min=0; n.tm_sec = 0;
    if (rotate_byday && not rotate_byhour) {
      l.tm_hour=0, n.tm_hour=0;
      return fast_mktime(&l) < fast_mktime(&n);
    } else if (rotate_byhour) {
      return fast_mktime(&l) < fast_mktime(&n);
    } else {
      return false;
    }
  }

  bool need_rotate_by_size(int fd,size_t size) {
    struct stat stat;
    if (::fstat(fd, &stat) != 0) return true;
    else
      return (size_t)stat.st_size > size ? true : false;
  }

  void write_noreturn(int fd, const char *buf, size_t size) {
    ssize_t total = 0;
    while(size > 0) {
      ssize_t written = ::write(fd, buf, size);
      if (written == -1) {
        if (errno == EINTR) continue;
        printf("%d,%s\n",fd,::strerror(errno)); return;
      }
      buf += written;
      total += written;
      size -= written;
    }
  }

  void _main(void) {
    ::prctl(PR_SET_NAME, name_.c_str());
    this->tid_ = static_cast<pid_t>(::syscall(SYS_gettid));
    bool looping = true;

    do{

      {
/* swap front/back-end buffer in the CS. */
      pthread_mutex_lock(&mutex_);
/* maybe? use CLOCK_MONOTONIC or CLOCK_MONOTONIC_RAW to prevent time rewind. */
      struct timespec abstime;
      clock_gettime(CLOCK_REALTIME, &abstime);
      abstime.tv_sec += flush_interval_;
      pthread_cond_timedwait(&cond_, &mutex_, &abstime);
      std::swap(bufs_,bufs_backend_);
      if (not running_) looping = false;
      pthread_mutex_unlock(&mutex_);
      }

      if (looping && bufs_backend_.size() == 0)     continue;
      if (looping && bufs_backend_[0]->size() == 0) continue;

      update_time();

      if (need_rotate_by_size(fd_,rotate_sz_) or
          need_rotate_by_time(&tm_last_,&tm_now_,rotate_byday_,rotate_byhour_)) {
        fd_ = rotate_file(fd_,dir_.c_str(),prefix_.c_str(),fn_buf_,sizeof(fn_buf_),time_buf_,suffix_.c_str());
      }

      for (size_t i = 0; i < bufs_backend_.size(); ++i) {
        buf_ptr &buf = bufs_backend_.at(i);
        if (buf->size() != 0)
  	write_noreturn(fd_,buf->c_str(),buf->size());
        buf->reuse();
      }
    } while(looping);
    ::close(fd_);
  }
};

void* _starter(void *arg)
{
  backend *be = static_cast<backend*>(arg);
  be->_main();
  return NULL;
}

class logger
{
private:
  stream os_;
  backend *const be_;
  char name_[7] = {0};

public:
  logger()
    :os_(std::cout), be_(nullptr) { strncpy(name_, "module", 6); }

  logger(const char name[7])
    :os_(std::cout), be_(nullptr) { strncpy(name_, name, 6); }

  logger(backend *const be)
    :os_(std::cout), be_(be) { strncpy(name_, "module", 6); }

  logger(const char name[7],backend *const be)
    :os_(std::cout), be_(be) { strncpy(name_, name, 6); }

  logger(const char name[7], stream os)
    :os_(os), be_(nullptr) { strncpy(name_, name, 6); }

  const char* name() { return name_; }

  void append(const string &line) {
    if (be_) be_->append(line.c_str(), line.size());
    else     os_ << line;
  }
};

class formatter
{
private:
  ostringstream os_;
  logger &logger_;
  char buf[32] = {0};

  char* head() {
    struct timeval t;
    gettimeofday(&t,NULL);
    struct tm tm_time;
    localtime_r(&t.tv_sec, &tm_time);
    int usec = static_cast<int>(t.tv_usec % (1000 * 1000));
    // TODO: may not format every msec, store prefix ymdhms
    snprintf(buf, sizeof(buf), "%4d%02d%02d %02d:%02d:%02d.%06d %6s",
	     tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
	     tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec, usec, logger_.name());
    return buf;
  }

public:
  formatter(logger &logger, const char* level, const char* file, int line, const char* func)
    :logger_(logger) {
    os_<<head()<<" "<<level<<" "<<file<<":"<<line<<"("<<func<<") # ";
  }
  ostringstream& stream() { return os_; }
  ~formatter() { os_ << std::endl; logger_.append(os_.str()); }
};

logger stdout("stdout",stream(std::cout));
logger stderr("stderr",stream(std::cerr));
logger stdlog("stdlog",stream(std::clog));

} /* logging */

#endif
