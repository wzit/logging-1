#include <logging.h>

int main()
{

  char buf[2048] = {0};
  for (int i = 0;i <2048; ++i) buf[i] = 1;
  logging::logging_backend b(false);
  logging::logger log2("mod1234",&b);
  b.start();

  int i=0;
  while (i++<100000) {
      LOG_INFO(log2) << i <<" " <<buf;
  }

  //b.stop_and_join();
  return 0;
}
