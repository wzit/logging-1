#ifndef LOGGING_H_
#define LOGGING_H_

#include <sys/time.h>
#include <iostream>
#include <sstream>
#include <string>

namespace logging {

class logger;

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
  logging_backend(std::string path,int rotate_M=100,int bufsz_M=4,std::string prefix="", std::string suffix="");
  void append(const std::string &line) {}
  void detach(logger* logger) {}
};

class logger
{
private:
  stream os_;
  logging_backend *backend_;
public:
  logger(stream os) :os_(os),backend_(nullptr) {}
  logger(logging_backend *backend) :os_(std::cout),backend_(backend) {}

  void append(const std::string &line)
  {
    if (backend_) {
      os_ <<" should not use this! logging backend not implement !";
      backend_->append(line);
    } else {
      os_ << line;
    }
  }
  ~logger() { if(backend_) backend_->detach(this); }
};

class appender
{
private:
  std::ostringstream os_;
  logger &logger_;
  char buf[32] = {0};
  //TODO: may not format every time;
  char* now()
  {
    struct timeval t;
    gettimeofday(&t,NULL);
    struct tm tm_time;
    localtime_r(&t.tv_sec, &tm_time);
    int usec = static_cast<int>(t.tv_usec % (1000 * 1000));
    snprintf(buf, sizeof(buf), "%4d%02d%02d %02d:%02d:%02d.%06d",
	     tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
	     tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec,
	     usec);
    return buf;
  }

public:
  appender(logger &logger,const char* level,const char* file,int line,const char* func) :logger_(logger) {
    os_ <<now()<<" "<<level<<" "<<file<<" ("<<line<<")" <<" "<< func<<" # ";
  }
  std::ostringstream& stream() {return os_;}
  ~appender() { os_ << std::endl; logger_.append(os_.str()); }
};

// default
static logger stdout(stream(std::cout));
}

#define DEBUG() if(logging::enabled_level<=logging::DEBUG) logging::appender(logging::stdout,"DEBUG",__FILE__,__LINE__,__FUNCTION__).stream()
#define INFO()  if(logging::enabled_level<=logging::INFO ) logging::appender(logging::stdout,"INFO ",__FILE__,__LINE__,__FUNCTION__).stream()
#define ERROR() if(logging::enabled_level<=logging::ERROR) logging::appender(logging::stdout,"ERROR",__FILE__,__LINE__,__FUNCTION__).stream()
#define FATAL() if(logging::enabled_level<=logging::FATAL) logging::appender(logging::stdout,"FATAL",__FILE__,__LINE__,__FUNCTION__).stream()

#define LOG_DEBUG(logger) if(logging::enabled_level<=logging::DEBUG) logging::appender(logger,"DEBUG",__FILE__,__LINE__,__FUNCTION__).stream()
#define LOG_INFO(logger)  if(logging::enabled_level<=logging::INFO ) logging::appender(logger,"INFO ",__FILE__,__LINE__,__FUNCTION__).stream()
#define LOG_ERROR(logger) if(logging::enabled_level<=logging::ERROR) logging::appender(logger,"ERROR",__FILE__,__LINE__,__FUNCTION__).stream()
#define LOG_FATAL(logger) if(logging::enabled_level<=logging::FATAL) logging::appender(logger,"FATAL",__FILE__,__LINE__,__FUNCTION__).stream()

#endif
