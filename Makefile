all: example.cc logging.h
	g++ -o example example.cc -std=c++11 -I./ -Wall -Wextra -lpthread

clean:
	rm -f example