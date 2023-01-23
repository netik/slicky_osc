CC=gcc
SRCS=main.c
INCLUDES=-I/opt/homebrew/Cellar/hidapi/0.12.0/include -I./log.c/src
LIBS=-L/opt/homebrew/Cellar/hidapi/0.12.0/lib -lhidapi 
CFLAGS=-Wall -g -DLOG_USE_COLOR

all: rainbow oscserver oscclient

rainbow: rainbow.c
	${CC} ${CFLAGS} $< -o rainbow ${LIBS}

oscserver: oscserver.c
	${CC} ${CFLAGS} $< tinyosc.c ./log.c/src/log.c -o oscserver ${INCLUDES} ${LIBS} 

oscclient: oscclient.c
	${CC} ${CFLAGS} $< tinyosc.c -o oscclient ${INCLUDES} ${LIBS} 

clean:
	-rm rainbow
	-rm oscserver
	-rm oscclient
	-rm *.o
	-rm -rf *.dSYM

