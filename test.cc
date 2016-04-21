#include <logging.h>

int main()
{
  logging::logger log2("mod1",std::cout);

  int i=10;
  while (i--) {
      INFO() << "what"<<" is 42.";
      LOG_ERROR(log2) << "i am log 2";
  }
  return 0;
}
