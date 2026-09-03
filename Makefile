CC=gcc
SRCS=main.c
INCLUDES=-I/opt/homebrew/Cellar/hidapi/0.13.1/include -I./log.c/src
LIBS=-L/opt/homebrew/Cellar/hidapi/0.13.1/lib -lhidapi 
CFLAGS=-g

UNAME := $(shell uname)
CUENET_LDFLAGS :=
ifneq ($(UNAME),Darwin)
  CUENET_LDFLAGS += -ldns_sd
endif

all: rainbow oscserver oscclient

rainbow: rainbow.c
	${CC} ${CFLAGS} $< -o rainbow ${LIBS}

oscserver: oscserver.c cli.c ssdp.c led.c state.c tinyosc.c cuenet.c
	${CC} ${CFLAGS} oscserver.c cli.c ssdp.c led.c state.c tinyosc.c cuenet.c ./log.c/src/log.c -o oscserver ${INCLUDES} ${LIBS} ${CUENET_LDFLAGS}

oscclient: oscclient.c
	${CC} ${CFLAGS} $< tinyosc.c -o oscclient ${INCLUDES} ${LIBS} 

clean:
	-rm rainbow
	-rm oscserver
	-rm oscclient
	-rm *.o
