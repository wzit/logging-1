#define private public

#include <logging.h>

int main()
{
  logging::backend b;

  b.update_time();
  printf("%d %d\n",b.now_.tv_sec,b.now_.tv_usec);
  return 0;
}
