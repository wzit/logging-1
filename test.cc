#include <logging.h>

int main()
{
  logging::logging_backend b;
  logging::logger log2("mod1234",&b);
  b.start();

  int i=10000;
  while (i--) {
      LOG_ERROR(log2) << "i am log 2";
  }

  b.stop_and_join();
  return 0;
}
