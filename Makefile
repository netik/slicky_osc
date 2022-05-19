CC=gcc
SRCS=main.c
INCLUDES=-I/opt/homebrew/Cellar/hidapi/0.11.2/include -I./log.c/src
LIBS=-L/opt/homebrew/Cellar/hidapi/0.11.2/lib -lhidapi 
CFLAGS=-g

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