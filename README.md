# Introduction

This software allows you to control a [Slicky USB Light](https://www.lexcelon.com/products/slicky) over [OSC](https://en.wikipedia.org/wiki/Open_Sound_Control). OSC is a commonly used protocol in stage, theater, and audio production. Here in San Francisco, we use the slicky light as a cue light to tell performers what to do on many of our shows. 

It can be used as a traffic light of sorts, telling people when to stop and go backstage. We typically send commands to this software via Bitfocus' [Companion](https://bitfocus.io/companion) but you can use any compatible OSC software to talk to this server.

For testing You can talk to this software using the included OSC client (oscclient) and issue commands like so:

```
# make the light red
./oscclient setcolor ff0000

# make the light blink
./oscclient blink 1

# make the light stop blinking
./oscclient blink 0
```

# OSC Commands Recognized

The server typically listens for commands on all interfaces, UDP port 9000.

|OSC Command|Argument Type|Usage|
|-----------|-------------|-----|
|setcolorint|int32|Sets lamp color using an integer|
|setcolorhex|hex string|Sets LED color using a RGB hex string (e.g. ff0000 = red. 00ff00 = green. 0000ff = blue)
|blink|int32, 0 or 1, default 0|Sets the LED light to blink indefinately|
|blink_on_change|int32, 0 or 1, default 1|Blinks whenever setcolorint or setcolorhex is called. Default is three blinks.|

Invalid commands will be logged with an error message and dropped.

# Compiling

You need log.c as well as hidapi. On Mac OS, install hidapi via brew
and update the Makefile to reflect the location of hidapi and the
proper version by changing the INCLUDES and LIBS lines accordingly.

Installing HIDAPI
```
brew install hidapi
```

Installing the log.c submodule

```
git submodule init
git submodule update
```

After all requirements above are satisfied, type `make`.

# Running

To run the server, just type `./oscserver`


