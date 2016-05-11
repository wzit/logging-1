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
#include <sys/time.h>
#include <string.h>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>

namespace logging {
using std::string;
using std::ostream;
using std::ostringstream;
using std::unique_ptr;
using std::vector;

/*
 *  logging front-end & back-end interface.
 */
class logger;
class logging_backend;

enum level{
  DEBUG,
  INFO,
  ERROR,
  FATAL
};

extern volatile level enabled_level;
void enable(enum level level);

extern logger stdout;

#define DEBUG()  if(logging::enabled_level<=logging::DEBUG) logging::formatter(logging::stdout,"DEBUG",__FILE__,__LINE__,__FUNCTION__).stream()
#define INFO()   if(logging::enabled_level<=logging::INFO ) logging::formatter(logging::stdout,"INFO ",__FILE__,__LINE__,__FUNCTION__).stream()
#define ERROR()  if(logging::enabled_level<=logging::ERROR) logging::formatter(logging::stdout,"ERROR",__FILE__,__LINE__,__FUNCTION__).stream()
#define FATAL()  if(logging::enabled_level<=logging::FATAL) logging::formatter(logging::stdout,"FATAL",__FILE__,__LINE__,__FUNCTION__).stream()

#define LOG_DEBUG(logger) if(logging::enabled_level<=logging::DEBUG) logging::formatter(logger,"DEBUG",__FILE__,__LINE__,__FUNCTION__).stream()
#define LOG_INFO(logger)  if(logging::enabled_level<=logging::INFO ) logging::formatter(logger,"INFO ",__FILE__,__LINE__,__FUNCTION__).stream()
#define LOG_ERROR(logger) if(logging::enabled_level<=logging::ERROR) logging::formatter(logger,"ERROR",__FILE__,__LINE__,__FUNCTION__).stream()
#define LOG_FATAL(logger) if(logging::enabled_level<=logging::FATAL) logging::formatter(logger,"FATAL",__FILE__,__LINE__,__FUNCTION__).stream()

/*
 *  implementation
 */
class stream
{
  ostream &os_;

public:
  stream(ostream &os) :os_(os) {}
  template<typename T>
  stream& operator<<(const T &t) { os_ << t; return *this; }
};

struct buf
{
  buf( const buf& ) = delete;
  const buf& operator=( const buf& ) = delete;
  size_t index;
  const size_t capacity;
  char *m;
  bool full_;

public:
  buf(size_t size) :index(0),capacity(size),m(nullptr),full_(false) { m=new char[capacity]; }
  ~buf() { delete[] m; }
  size_t rest() { return capacity-index; }
  const char *c_str() { return m; }
  size_t size() { return index; }
  void reuse() { index=0; full_ = false; }
  bool full() { return full_; }
  void filled() { full_ = true; }
  void push_back(const char *s,size_t len) { memcpy(m+index, s, len); index+=len; }
};
typedef unique_ptr<buf> buf_ptr;
typedef vector<buf_ptr> buf_vec;

void* _starter(void *arg);
class logging_backend
{
public:
  logging_backend(bool async=false
		  ,string dir="./log/"
		  ,string prefix="log"
		  ,string backend_name="logbe"
		  ,string suffix=".log"
		  ,int rotate_M=100
		  ,int bufsz_K=4
		  ,int flush_sec=3);
  ~logging_backend();
  void append(const char* line, size_t len);

private:
  logging_backend( const logging_backend& ) = delete;
  const logging_backend& operator=( const logging_backend& ) = delete;

  bool start();
  void stop_and_join();
  friend void *_starter(void *arg);
  void thread_main(void);

  const bool   async_;
  const string dir_;
  const string prefix_;
  const string suffix_;
  const int    rotate_sz_;
  const int    buf_capacity_;
  const string name_;
  const int    flush_interval_;

  pthread_t pthreadid_;
  pid_t     tid_;
  pthread_mutex_t mutex_;
  pthread_cond_t  cond_;

  volatile bool running_;
  int     fd_;
  char    filename_buf_[512] = {0};
  char    time_buf_[32] = {0};

  struct tm tm_last_;
  struct timeval now_;
  struct tm tm_now_;
  void update_time();

  buf_vec buf_vec_;
  buf_vec buf_vec_backend_;

  void sync_to_file(const char* line, size_t len);
};

class logger
{
  stream os_;
  logging_backend *b_;
  char name_[7] = {0};

public:
  logger(const char name[7],stream os)
    :os_(os),b_(nullptr) { strncpy(name_,name,6); }
  logger(const char name[7],logging_backend *backend)
    :os_(std::cout),b_(backend) { strncpy(name_,name,6); }

  const char* name() { return name_; }
  void append(const string &line)
  {
    if (b_)  b_->append(line.c_str(),line.size());
    else     os_ << line;
  }
};

class formatter
{
  ostringstream os_;
  logger &logger_;
  char buf[32] = {0};

  char* header() {
    struct timeval t;
    gettimeofday(&t,NULL);
    struct tm tm_time;
    localtime_r(&t.tv_sec, &tm_time);
    int usec = static_cast<int>(t.tv_usec % (1000 * 1000));
    // TODO: may not format every msec, store prefix ymdhms
    snprintf(buf, sizeof(buf), "%4d%02d%02d %02d:%02d:%02d.%06d %6s",
	     tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
	     tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec, usec, logger_.name());
    return buf; }

public:
  formatter(logger &logger,const char* level,const char* file,int line,const char* func) :logger_(logger) {
    os_ <<header()<<" "<<level<<" "<<file<<":"<<line<<"("<<func<<") # ";
  }
  ostringstream& stream() {return os_;}
  ~formatter() { os_ << std::endl; logger_.append(os_.str()); }
};

} /* logging */

#endif
