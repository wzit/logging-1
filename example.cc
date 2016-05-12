#include <logging.h>

int main()
{

  char buf[2048] = {0};
  for (int i = 0;i <2047; ++i) buf[i] = '1';
  logging::backend b(false);
  logging::logger log2("mod1234",&b);

  int i=0;
  while (i++<100) {
      LOG_INFO(log2) << i <<" " <<buf;
  }

  return 0;
}
