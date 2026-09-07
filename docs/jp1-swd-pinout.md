# Slicky JP1 SWD pinout

JP1 is a 4-pin **SWD** header (not full JTAG). There is no `nRESET` on it.

MCU is Atmel/Microchip **SAMD11** in 24-QFN (U18), USB device. Debug signals:

- **PA30** = SWCLK (QFN pin 19)
- **PA31** = SWDIO (QFN pin 20)

## Orientation

**JP1 pin 1 is the pad closest to the `JP1` silkscreen.** That numbering is from a meter check (USB shell = GND, debugger disconnected) and overrides the earlier photo guess.

## Pinout (measured)

| JP1 | vs USB shell | Net |
|-----|----------------|-----|
| **1** (closest to `JP1` text) | **3.3 V** | VTref / VDD |
| **2** | 0 V | SWCLK or SWDIO (order unknown) |
| **3** | 0 V | the other SWD pin |
| **4** (far end) | 0 V, tied to pour | **GND** |

An earlier photo-based table had GND/3.3 V reversed. That would put debugger VTref on ground and explains `cannot read IDR` plus a ~1 V reading with the adapter attached (buffers loading a signal net).

Pins 2 and 3 both idle at 0 V, so running firmware may have remuxed PA30/PA31. **Cold-plug:** connect SWD first, then plug USB so the debugger is present at reset.

## Debugger hookup (generic)

| Debugger | JP1 |
|----------|-----|
| Vtref / 3.3 V | **1** |
| SWCLK | 2 or 3 |
| SWDIO | the other of 2/3 |
| GND | **4** |

Do **not** feed 5 V into pin 1. Swap 2 and 3 if IDR still fails. Do not swap 1 and 4.

## Olimex ARM-USB-OCD + ARM-JTAG-SWD

This combination **will work**. ARM-USB-OCD has no SWD hardware of its own; the ARM-JTAG-SWD adapter adds the bidirectional SWDIO buffer. OpenOCD 0.9+ is the supported software path (`probe-rs` does not talk to this FTDI pod).

### Physical stack

Plug the adapter **directly onto** the ARM-USB-OCD 20-pin header. Put the ribbon cable (or Dupont jumpers) on the **adapter’s target side**. Do not put a cable between the OCD and the adapter.

Power the Slicky from USB so JP1-1 is 3.3 V. The adapter’s level shifters are powered from **VTref (20-pin pin 1)**. That must go to JP1-1, not JP1-4.

### 20-pin ARM IDC (adapter target side) → JP1

Standard ARM 20-pin numbering (pin 1 is usually marked; red stripe on a ribbon is pin 1):

```
 1 VTref        2 NC / Vsupply
 3 nTRST        4 GND
 5 TDI          6 GND
 7 TMS / SWDIO  8 GND
 9 TCK / SWCLK 10 GND
11 RTCK        12 GND
13 TDO / SWO   14 GND
15 nSRST       16 GND
17 NC          18 GND
19 +5V         20 GND
```

| ARM-JTAG-SWD (20-pin) | Signal | Slicky JP1 |
|-----------------------|--------|------------|
| **1** (or 2) | VTref | **1** (3.3 V, next to `JP1` text) |
| **7** | SWDIO (TMS) | **2** or **3** |
| **9** | SWCLK (TCK) | the other of 2/3 |
| **4** (or any even GND) | GND | **4** (ground plane) |

Leave nTRST (3), nSRST (15), TDI, TDO, and **pin 19 (+5 V)** unconnected. Pin 19 is debugger 5 V — never tie it to JP1-1.

On ARM-USB-OCD-H, **pins 1 and 2 are both VREF**. VREF is mandatory: it powers the 74LVC SWDIO buffer on the ARM-JTAG-SWD. If it is open, SWCLK may still wiggle and OpenOCD still prints `cannot read IDR`.

#### How to count the 20-pin (notch)

Look **into** the 20-pin (holes on the female, or the pins on the male). The polarizing **notch is on the odd-pin long edge**. Pin 1 is at one end of that same edge (triangle on the plastic; red stripe on the Olimex ribbon). Pin 9 sits in the **middle of the notch**.

Olimex’s own drawing (connector vertical, notch on the left):

```
          notch
            |
     1  2     VTref    VTref
     3  4     nTRST    GND
     5  6     TDI      GND
     7  8     SWDIO    GND
    [9] 10    SWCLK    GND     ← notch centered on pin 9
    11 12     RTCK     GND
    13 14     TDO      GND
    15 16     nSRST    GND
    17 18     NC       GND
    19 20     +5V      GND     ← do not use pin 19
```

Same thing with the notch along the bottom (how you often hold it for Dupont wires):

```
  2   4   6   8  10  12  14  16  18  20
  1   3   5   7   9  11  13  15  17  19
  ============ notch / key ============
  ^ pin 1 (VTref)    ^7 SWDIO  ^9 SWCLK
```

Odd pins (1, 3, 5, 7, 9, …) are the **notch row**. Even pins are GND except 2 (also VTref) and 19 (+5 V).

If you counted 1–10 along one row of ten, you were on the wrong map. SWDIO is the **4th pin on the notch row**, SWCLK the **5th**.

If the adapter’s 10-pin Cortex header is easier:

| 10-pin Cortex | Signal | JP1 |
|---------------|--------|-----|
| 1 | VTref | 1 |
| 2 | SWDIO | 2 or 3 |
| 4 | SWCLK | the other of 2/3 |
| 3 | GND | 4 |

### OpenOCD

Need OpenOCD ≥ 0.9. For the original **ARM-USB-OCD** (not `-H`):

```bash
openocd \
  -f interface/ftdi/olimex-arm-usb-ocd.cfg \
  -f interface/ftdi/olimex-arm-jtag-swd.cfg \
  -c "adapter speed 500" \
  -c "transport select swd" \
  -c "reset_config none" \
  -c "set CHIPNAME at91samd11u14" \
  -f target/at91samdXX.cfg
```

If the pod is an **ARM-USB-OCD-H**, use `interface/ftdi/olimex-arm-usb-ocd-h.cfg` instead.

A good attach prints something like `SWD DPIDR 0x0bc11477` (Cortex-M0+). Then dump flash:

```
halt
dump_image slicky_flash.bin 0x00000000 0x4000
```

If it fails with `cannot read IDR`:

1. Confirm VTref (20-pin **1**) is on JP1-1 (3.3 V, next to `JP1` text) and JP1-4 is GND.
2. Swap JP1-2 / JP1-3 (SWCLK / SWDIO). Cold-plug: SWD connected before USB.
3. Slow the adapter further (`adapter speed 100`).
4. Check U18 for a security lock (SWD disabled). Try `ATSAMD11D14A` / `at91samd11d14` if the package marking is D11D not D11U.

## ST-Link V2

Yes for **attach + dump**. Not with STM32CubeProgrammer / ST-Link Utility (those only speak STM32). OpenOCD is the path.

ST-Link HLA often **reads** SAMD memory fine but can fail **flash erase/write** (NVMCTRL is a 16-bit register; HLA splits that into byte writes). For reverse-engineering the HID protocol, a read dump is enough.

Power the Slicky from USB. Do **not** connect the ST-Link **5 V** pin to JP1.

### Genuine ST-Link V2 (20-pin ARM header)

Same overlay as the Olimex 20-pin:

| ST-Link 20-pin | Signal | JP1 |
|----------------|--------|-----|
| **1** | VTref (sense, not 5 V) | **1** (3.3 V) |
| **7** | SWDIO | **2** or **3** |
| **9** | SWCLK | the other of 2/3 |
| **4** (or any GND) | GND | **4** |

Leave NRST disconnected.

### Cheap “ST-Link V2” USB stick (Dupont 10-pin)

Trust the **silkscreen on that stick** (clone pinouts vary). Typical labels:

| Stick label | JP1 |
|-------------|-----|
| GND | 4 |
| SWCLK | 2 or 3 |
| SWDIO | the other of 2/3 |
| 3.3 V | 1 (or omit if USB is powering the light) |

The clone’s 3.3 V pin is often a **power output**, not VTref. Connecting it in parallel with the Slicky’s regulator is optional and can back-feed; GND + SWDIO + SWCLK is enough if the light is on USB.

Ignore SWIM (that’s STM8). Ignore 5 V.

### OpenOCD

```bash
openocd \
  -f interface/stlink.cfg \
  -c "transport select hla_swd" \
  -c "adapter speed 500" \
  -c "reset_config none" \
  -c "set CHIPNAME at91samd11u14" \
  -f target/at91samdXX.cfg
```

Expect `SWD DPIDR 0x0bc11477`, then `halt` and `dump_image slicky_flash.bin 0x00000000 0x4000`. If flash programming later fails with a sector-erase error, that is the known HLA/16-bit issue — use the Olimex + ARM-JTAG-SWD for writes.

## Related hardware notes

- LED ring is **16** pixels (U1–U16), WS2812-style addressable packages.
- USB HID is a 64-byte vendor output report. The only known host command is global RGBW:

  `00 0A 04 00 00 W B G R`

  A 64-byte report could hold 16 RGB triples, but HID probing did not find a per-LED command. Dumping the 16 KB flash over JP1 is the way to see whether firmware ever addresses pixels separately.
