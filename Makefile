all: test.cc logging.h logging.cc
	g++ -o test test.cc logging.cc -std=c++11 -ggdb3 -I./ -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -lpthread

clean:
	rm -rf test *.o log/
