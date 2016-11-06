# Raspberry Pi FPV

Table of Contents:

1. [Overview](#overview)
2. [Operating System](#operating-system)
3. [Building](#building)
4. [Installation](#installation)
4. [Wiring](#wiring)
5. [License](#license)
6. [Links](#links)

## Overview

This software was created to use a Raspberry Pi Zero, in combination with a
Raspberry Pi Camera Module v2, as an FPV flight system for a quadcopter. This
software was designed to make use of the Raspberry Pi's composite video output
to produce a video signal compatible with the traditional analog video systems.

This software has 3 main functions:

1. Stream camera video to a video transmitter in real-time via the composite
   output.
2. Record camera video to the SD card, controllable by a switch on the
   transmitter.
3. Add an on-screen display to the video output (but not the recording).

The software was written in C, using provided Raspberry Pi APIs directly to
produce a system as light-weight as possible, in order to minimize boot time.
Using a minimal Tiny Core Linux system, the Raspberry Pi Zero can boot from no
power to live video output in approximately 15 seconds.

## Operating System

This software uses common Raspberry Pi APIs, including dispmanx, EGL, MMAL, and
OpenGL ES operating-system specific functionality (that I'm aware of). As a
result, it should be capable of running on most Raspberry Pi operating systems.

This software was created and tested using piCore, the Raspberry Pi port of the
Tiny Core Linux operating system. Tiny Core Linux is ideal for this purpose for
several reasons. First, the operating system is extremely minimal; much of the
functionality present in typical Linux operating systems is omitted, resulting
in an extremely compact system that can boot very quickly. In addition, the
operating system loads the entire filesystem into memory at boot time,
minimizing SD access and making the system more tolerant to removal of power, as
will be common in its usage.

The only package needed at run-time is the `rpi-vc` package, which contains the
shared libraries which provide the MMAL APIs.

## Building

This software has a few prerequisites. They are:

- [bcm2835](http://www.airspayce.com/mikem/bcm2835/)
- [stb_image](http://github.com/nothings/stb)

On a Tiny Core Linux system, the following official extensions will be required
for the build process, in addition to the prerequisites above:

- `compiletc` includes the build software (such as gcc, make, etc.), as well
  as a bunch of other things that aren't needed.
- `rpi-vc-dev` includes the header files for the MMAL APIs.
- `squashfs-tools` if you want to make extensions for the software or
  prerequisites.

If you don't have an Internet connection on your Raspberry Pi, see my
[TceDownload](#todo) GitHub repository for software to speed up the process of
downloading the packages and their dependencies. Use the flags
`-arch armv6 -version 8.x` to get the correct versions.

Once the prerequisites are in place, building the software should be as simple
as running the Makefile via `make`. Note, however, that configuration is done at
compile-time. Check the configuration values present in the defines at the top
of the source files in this repository, as they may not be appropriate for your
system.

Note that the serial port that is used is not configured by the software; it
should be configured via a separate start-up script. The serial port must
operate in raw mode, at 9600 baud rate. Echo is not needed. My setup used the
following command to set it up:

```
stty -F /dev/ttyAMA0 ospeed 9600 raw -echo
```

This configuration may be done by the software in the future. The APIs are
present for it.

## Installation

Once the software is built, the Makefile does not include a recipe to install
it. Installation depends on the type of system. If you're using Tiny Core Linux,
then you may want to create an extension containing the FPV executable and the
images. Otherwise, you'll probably just want to copy them into place.

You'll then want to run the software automatically on power-up. If your system
is set up for autologin, then this is probably fairly simple. In my case, I
added the following to my user's `.profile`:

```
# Start the FPV software (TTY only)
if [ ${TERMTYPE:5:3} == "tty" ]
then
	sudo fpv
fi
```

Note that due to the use of `sudo`, this will most likely not work if `sudo`
requires password entry on your system (Tiny Core Linux does not). I used this
approach because it allowed me to use Control-C to exit (if a keyboard was
connected).

## Wiring

There are only a few connections that need to be made to the Raspberry Pi in
order to connect it to the vehicle. They are:

- Power and ground - Any 5V output should do. Probably from the connection to
  the record input.
- Record input - A PWM output channel on the flight controller should be
  connected to a GPIO pin on the Raspberry Pi. **Check this voltage; mine was 3V.**
- Telemetry - The serial output on the flight controller should be connected to
  the UART receive pin on the Raspberry Pi. the FC will need to be configured to
  output telemetry. **Check this voltage; mine was 3V.**
- Video - The composite output should be connected to the video input on the
  video transmitter.

These connections can be made either by headers or by soldering wires on.

## License

This software is licensed under the MIT license. Basically, do whatever you want
with it, but don't blame me if you damage something because of a failure of the
software. By all means, tell me what happened, and I can try to fix the
problem (no promises)--but I'm not paying for the damage.

See `LICENSE` for the official wording.

## Links

- [bcm2835](http://www.airspayce.com/mikem/bcm2835/)
- [stb_image](http://github.com/nothings/stb)
- [Tiny Core Linux](http://tinycorelinux.net/)
