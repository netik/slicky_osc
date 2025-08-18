## OSC SLicky

This allows for OSC control over a USB Slicky Light. 

## Building

This project depends on log.c which is a submodule. Install that via this command, after checkout:

```
git submodule update --init --recursive
```

You also need hidapi so we can talk to the USB bus:
```
brew install hidapi
```

After that a simple `make` should suffice.

## OSC Commands supported

setcolorint int
setcolorhex str
blink int
blink_on_change int

