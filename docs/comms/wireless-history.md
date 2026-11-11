# Wi-Fi on the MessagePad: what the community built, and how it bears on the reconstruction

Background for the comms area. Every fact here comes from the
NewtonTalk mailing list (newtontalk.net, public archive). Each one gives
the thread's subject and date so it can be checked. Nothing here was
established from the ROM.

## On the original hardware

* **Hiroshi's WaveLAN driver.** The usual Wi-Fi solution was a 16-bit
  802.11b PC Card driven by a third-party WaveLAN driver package written by
  Hiroshi, under Apple's NIE 2.0 (Ethernet module). It started as shareware;
  later a free release with the protection removed was published. Its
  encryption is WEP only, and only WEP 128-bit with an ASCII key is known to
  work: 40/64-bit keys and hexadecimal keys fail
  ("Hiroshi / WaveLan Driver: WEP 40/64 & 128 Bit Key Bug ?", 2021-04-25).
  *MoreWifi* (Sylvain Pilet) extends the list of cards the driver accepts
  (same thread).
* **The work-arounds owners use** are an open guest network limited to
  dial-up speed, or an old AirPort base station restricted by MAC address and
  bridged to the main router ("Current state of Newton wireless solutions",
  2023-10-17).
* **PicoPCMCIA** (yyzkevin.com/picopcmcia) is a Pi Pico W based PC Card. It
  presents an **NE2000 Ethernet adapter** to the host and does the Wi-Fi
  itself, so WPA2 is the card's business. The Newton still needs NIE and an
  Ethernet driver (the DDK ships an NE2000 driver as sample source). It was
  tested working on a Newton ("Newton compatible PCMCIA WiFi Card (WPA2) in
  the making", 2024-05-09; "PicoPCMCIA", 2026-01-09/11).
* **ESP32 Wi-Fi boards.** Jake Borden built an ESP32 board, and an internal
  modem-port Wi-Fi card based on it was proposed. Wi-Fi-to-serial dongles
  are also used ("RFC: project ideas for 2026", 2026-03/04;
  "WiFi ESP32 to serial dongle", 2025-08-31).

The common thread is that the Newton cannot do modern encryption itself.
Every working solution moves the radio and its security into a separate
processor that the Newton sees as wired Ethernet or a serial line.

## How the reconstruction differs

The reconstruction moves the same boundary one layer higher. The NIE's
TCP/IP stack is not run at all: its service and tool are host classes over
the host's sockets (`docs/comms/README.md`). So the host OS does the Wi-Fi,
WPA2/WPA3, DHCP and DNS, and no card driver or WEP is involved. The ESP32
host manages its own Wi-Fi (`host/HostWifi.h`).

## Porting the ROM to newer CPUs

A list discussion ("Can the Newton's ARM processor ever be upgraded?",
2025-11-29 to 12-03, Matthias Melcher and David Arnold) lists what stops the
ROM's binary from running on newer ARM chips:
* The kernel's core is hand-written assembly, specific to the SA-110 (ARMv4):
  MMU, interrupts, task switching.
* There is no FPU; floating point is emulated through undefined-instruction
  traps.
* It is big-endian. Byte-swapping the image would need to know how every
  byte is accessed: instructions, 32- and 16-bit integers, UTF-16 and ASCII
  strings, and four-character codes all need different handling.
* 64-bit CPUs rule out running it as it is.

The list noted that Apple has been asked for the ROM source more than once
and that legal issues stand in the way. It also noted that Einstein's ROM
extension (REx) is a working example of a hardware abstraction layer. The
reconstruction avoids all of the binary-porting problems because it is
source (`src/`), with the CPU-specific parts behind `hal/` and the host task
runtime (`docs/host-runtime.md`).
