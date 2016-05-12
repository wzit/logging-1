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
class backend;

enum level {
  DEBUG,
  INFO,
  ERROR,
  FATAL
};

extern level enabled_level;
void enable(enum level level);

extern logger stdout;

#define LOG_DEBUG(_l) if(logging::enabled_level<=logging::DEBUG) logging::formatter((_l),"DEBUG",__FILE__,__LINE__,__FUNCTION__).stream()
#define LOG_INFO(_l)  if(logging::enabled_level<=logging::INFO ) logging::formatter((_l),"INFO ",__FILE__,__LINE__,__FUNCTION__).stream()
#define LOG_ERROR(_l) if(logging::enabled_level<=logging::ERROR) logging::formatter((_l),"ERROR",__FILE__,__LINE__,__FUNCTION__).stream()
#define LOG_FATAL(_l) if(logging::enabled_level<=logging::FATAL) logging::formatter((_l),"FATAL",__FILE__,__LINE__,__FUNCTION__).stream()

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
  buf(const buf&) = delete;
  const buf& operator=(const buf&) = delete;
  size_t index;
  const size_t capacity;
  char *m;
  bool full_;

public:
  buf(size_t size) :index(0), capacity(size), m(nullptr), full_(false) { m = new char[capacity]; }
  ~buf() { delete[] m; }
  size_t rest() { return capacity - index; }
  const char *c_str() { return m; }
  size_t size() { return index; }
  void reuse() { index = 0; full_ = false; }
  bool full() { return full_; }
  void filled() { full_ = true; }
  void push_back(const char *s, size_t len) { memcpy(m + index, s, len); index += len; }
};
typedef unique_ptr<buf> buf_ptr;
typedef vector<buf_ptr> buf_vec;

void* _starter(void *arg);
class backend
{
public:
  backend(bool async=false
	  ,string dir="./log/"
	  ,string prefix="log"
	  ,string backend_name="logbe"
	  ,string suffix=".log"
	  ,size_t rotate_M=100
	  ,size_t bufsz_K=1
	  ,size_t flush_sec=3);
  ~backend();
  void append(const char* line, size_t len);

private:
  backend(const backend&) = delete;
  const backend& operator=(const backend&) = delete;

  bool start();
  void stop_and_join();
  friend void *_starter(void *arg);
  void thread_main(void);
  void sync_to_file(const char* line, size_t len);
  void update_time();
  
  const bool   async_;
  const string dir_;
  const string prefix_;
  const string suffix_;
  const size_t rotate_sz_;
  const size_t buf_capacity_;
  const string name_;
  const size_t flush_interval_;

  pthread_t pthreadid_;
  pid_t     tid_;
  pthread_mutex_t mutex_;
  pthread_cond_t  cond_;

  bool running_;
  int  fd_;
  char filename_buf_[512] = {0};
  char time_buf_[32] = {0};

  struct tm tm_last_;
  struct tm tm_now_;
  struct timeval now_;  
  
  buf_vec buf_vec_;
  buf_vec buf_vec_backend_;
};

class logger
{
  stream os_;
  backend *be_;
  char name_[7] = {0};

public:
  logger(const char name[7], stream os)
    :os_(os), be_(nullptr) { strncpy(name_, name, 6); }
  logger(const char name[7],backend *be)
    :os_(std::cout), be_(be) { strncpy(name_, name,6); }

  const char* name() { return name_; }
  void append(const string &line) {
    if (be_)  be_->append(line.c_str(), line.size());
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
    return buf;
  }

public:
  formatter(logger &logger, const char* level, const char* file, int line, const char* func)
    :logger_(logger) {
    os_<<header()<<" "<<level<<" "<<file<<":"<<line<<"("<<func<<") # ";
  }
  ostringstream& stream() { return os_; }
  ~formatter() { os_ << std::endl; logger_.append(os_.str()); }
};

} /* logging */

#endif
