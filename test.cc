#include <logging.h>

int main()
{

  char buf[2048] = {0};
  for (int i = 0;i <2048; ++i) buf[i] = '1';
  logging::backend b(true);
  logging::logger log2("mod1234",&b);

  int i=0;
  while (i++<400000) {
      LOG_INFO(log2) << i <<" " <<buf;
  }

  return 0;
}
