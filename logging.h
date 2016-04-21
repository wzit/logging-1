#ifndef LOGGING_H_
#define LOGGING_H_

#include <pthread.h>
#include <sys/time.h>
#include <string.h>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace logging {

class logger;
class logging_backend;

enum level{
  DEBUG=0,
  INFO,
  ERROR,
  FATAL
};

static bool enabled_level = INFO;
static void set_level(enum level level) { enabled_level = level; }

class stream
{
private:
  std::ostream &os_;

public:
  stream(std::ostream &os) :os_(os) {}
  template<typename T>
  stream& operator<<(T t) { os_ << t; return *this; }
};

// a logging backend means a real thread
class logging_backend
{
public:
  logging_backend(std::string path,std::string prefix="",std::string backend_name="logging",std::string suffix=".log",int rotate_M=100,int bufsz_M=1,int flush_sec=3);
  ~logging_backend();
  void start();
  void stop_and_join();
  // interface
  void append(const char* line, size_t len);
  // thread main
  void thread_func(void);

private:
  // noncopyable
  logging_backend( const logging_backend& ) = delete;
  const logging_backend& operator=( const logging_backend& ) = delete;
  pthread_t  pthreadid_;
  pid_t      tid_;
  pthread_mutex_t mutex_;
  pthread_cond_t cond_;
  std::string path_;
  std::string prefix_;
  std::string suffix_;
  int rotate_sz_;
  int buf_sz_;
  std::string name_;
  int flush_interval_;
  bool running_;
};

class logger
{
private:
  stream os_;
  logging_backend *backend_;
  char name_[7] = {0};
public:
  logger(const char name[7],stream os) :os_(os),backend_(nullptr) { strncpy(name_,name,6); }
  logger(const char name[7],logging_backend *backend) :os_(std::cout),backend_(backend) { strncpy(name_,name,6); }

  void append(const std::string &line)
  {
    if (backend_) {
      os_ <<" should not use this! logging backend not implement !";
      backend_->append(line.c_str(),line.size());
    } else {
      os_ << line;
    }
  }
  ~logger() {}
  const char* name() { return name_; }
};

class formatter
{
private:
  std::ostringstream os_;
  logger &logger_;
  char buf[32] = {0};
  //TODO: may not format every time;
  char* header()
  {
    struct timeval t;
    gettimeofday(&t,NULL);
    struct tm tm_time;
    localtime_r(&t.tv_sec, &tm_time);
    int usec = static_cast<int>(t.tv_usec % (1000 * 1000));
    snprintf(buf, sizeof(buf), "%4d%02d%02d %02d:%02d:%02d.%06d %6s",
	     tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
	     tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec,usec,
	     logger_.name());
    return buf;
  }

public:
  formatter(logger &logger,const char* level,const char* file,int line,const char* func) :logger_(logger) {
    os_ <<header()<<" "<<level<<" "<<file<<":"<<line<<"("<<func<<") # ";
  }
  std::ostringstream& stream() {return os_;}
  ~formatter() { os_ << std::endl; logger_.append(os_.str()); }
};

// default logger
static logger stdout("stdout",stream(std::cout));
}

#define DEBUG() if(logging::enabled_level<=logging::DEBUG) logging::formatter(logging::stdout,"DEBUG",__FILE__,__LINE__,__FUNCTION__).stream()
#define INFO()  if(logging::enabled_level<=logging::INFO ) logging::formatter(logging::stdout,"INFO ",__FILE__,__LINE__,__FUNCTION__).stream()
#define ERROR() if(logging::enabled_level<=logging::ERROR) logging::formatter(logging::stdout,"ERROR",__FILE__,__LINE__,__FUNCTION__).stream()
#define FATAL() if(logging::enabled_level<=logging::FATAL) logging::formatter(logging::stdout,"FATAL",__FILE__,__LINE__,__FUNCTION__).stream()

#define LOG_DEBUG(logger) if(logging::enabled_level<=logging::DEBUG) logging::formatter(logger,"DEBUG",__FILE__,__LINE__,__FUNCTION__).stream()
#define LOG_INFO(logger)  if(logging::enabled_level<=logging::INFO ) logging::formatter(logger,"INFO ",__FILE__,__LINE__,__FUNCTION__).stream()
#define LOG_ERROR(logger) if(logging::enabled_level<=logging::ERROR) logging::formatter(logger,"ERROR",__FILE__,__LINE__,__FUNCTION__).stream()
#define LOG_FATAL(logger) if(logging::enabled_level<=logging::FATAL) logging::formatter(logger,"FATAL",__FILE__,__LINE__,__FUNCTION__).stream()

#endif
