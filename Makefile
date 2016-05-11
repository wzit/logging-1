all: test.cc logging.h logging.cc
	g++ -o test test.cc logging.cc -std=c++11 -I./ -Wall -Wextra -lpthread

clean:
	rm -rf test *.o log/
