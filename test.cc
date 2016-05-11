#include <logging.h>

int main()
{
  logging::logging_backend b;
  logging::logger log2("mod1234",&b);

  int i=0;
  while (i++<500000) {
      LOG_INFO(log2) << i;
  }

  //b.stop_and_join();
  return 0;
}
