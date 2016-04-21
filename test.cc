#include <logging.h>

int main()
{
  logging::logging_backend b;
  logging::logger log2("mod1234",&b);
  b.start();

  int i=0;
  while (i++<1000000) {
      LOG_ERROR(log2) << "i am log 2 " <<i;
  }

  b.stop_and_join();
  return 0;
}
