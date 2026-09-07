CC=gcc
CFLAGS=-g
INCLUDES=-I/opt/homebrew/Cellar/hidapi/0.13.1/include -Ivendor/log
LIBS=-L/opt/homebrew/Cellar/hidapi/0.13.1/lib -lhidapi

UNAME := $(shell uname)
CUENET_LDFLAGS :=
ifneq ($(UNAME),Darwin)
  CUENET_LDFLAGS += -ldns_sd
endif

OSCSERVER_SRCS=oscserver.c cli.c led.c state.c tinyosc.c cuenet.c vendor/log/log.c
OSCSERVER_OBJS=$(OSCSERVER_SRCS:.c=.o)

OSCCLIENT_SRCS=oscclient.c tinyosc.c
OSCCLIENT_OBJS=$(OSCCLIENT_SRCS:.c=.o)

.PHONY: all clean

all: rainbow oscserver oscclient

rainbow: rainbow.c
	$(CC) $(CFLAGS) $< -o $@ $(LIBS)

oscserver: $(OSCSERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS) $(CUENET_LDFLAGS)

oscclient: $(OSCCLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

-include $(OSCSERVER_OBJS:.o=.d)
-include $(OSCCLIENT_OBJS:.o=.d)

clean:
	rm -f rainbow oscserver oscclient
	rm -f *.o *.d
	rm -f vendor/log/*.o vendor/log/*.d
