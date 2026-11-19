# vscpl1drv-rs485

![License](https://img.shields.io/badge/license-MIT-blue.svg)
[![Linux](https://github.com/grodansparadis/vscpl1drv-rs485/actions/workflows/build-linux.yml/badge.svg?branch=master)](https://github.com/grodansparadis/vscpl1drv-rs485/actions/workflows/build-linux.yml?query=branch%3Amaster+is%3Asuccess)
[![Raspberry Pi arm64](https://github.com/grodansparadis/vscpl1drv-rs485/actions/workflows/build-raspberry-pi-arm64.yml/badge.svg?branch=master)](https://github.com/grodansparadis/vscpl1drv-rs485/actions/workflows/build-raspberry-pi-arm64.yml?query=branch%3Amaster+is%3Asuccess)
[![Raspberry Pi armhf](https://github.com/grodansparadis/vscpl1drv-rs485/actions/workflows/build-raspberry-pi-armhf.yml/badge.svg?branch=master)](https://github.com/grodansparadis/vscpl1drv-rs485/actions/workflows/build-raspberry-pi-armhf.yml?query=branch%3Amaster+is%3Asuccess)
[![macOS](https://github.com/grodansparadis/vscpl1drv-rs485/actions/workflows/build-macos.yml/badge.svg?branch=master)](https://github.com/grodansparadis/vscpl1drv-rs485/actions/workflows/build-macos.yml?query=branch%3Amaster+is%3Asuccess)
[![Windows](https://github.com/grodansparadis/vscpl1drv-rs485/actions/workflows/build-windows.yml/badge.svg?branch=master)](https://github.com/grodansparadis/vscpl1drv-rs485/actions/workflows/build-windows.yml?query=branch%3Amaster+is%3Asuccess)
[![Project Status: Active](https://www.repostatus.org/badges/latest/active.svg)](https://www.repostatus.org/#active)

A VSCP Level I (CANAL) driver for RS-485 and RS-422: the **master** of
[VSCP Level I over RS-485/RS-422](https://grodansparadis.github.io/vscp-doc-spec/#/./vscp_level_i_over_rs-485_rs-422).
It polls the nodes on a segment for events, finds new nodes, notices nodes
that are gone and sends the host's events to the nodes. Hosts such as the
VSCP daemon, VSCP Works, vscp-cli and the VSCP simulator see the segment as
any other Level I interface.

* Polling, NO_EVENTS, discovery of new nodes spread over the polling rounds
  (as the specification suggests), lost nodes, events between nodes.
* The ninth (address) bit with mark/space parity on ordinary serial ports
  and USB adapters, or plain 8N1.
* CRC-8/MAXIM-DOW (the specification's "DOW checksum").
* Direction control by the adapter, the Linux kernel RS-485 mode or RTS.
* The points the specification leaves open are configurable and documented.
* Tested against golden vectors, a simulated segment and simulated nodes on
  a pseudo terminal.

The Level II twin for the VSCP daemon is
[vscpl2drv-rs485](https://github.com/grodansparadis/vscpl2drv-rs485).

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
sudo cmake --install build      # Linux: /var/lib/vscp/drivers/level1
```

Windows: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64` and
`cmake --build build --config Release`. See [Build](docs/build.md).

## Configuration

```
device;baudrate;addresses;poll-timeout;discovery-rounds;miss-limit;queue-size
```

Only the device is required: `/dev/ttyUSB0`, `/dev/ttyUSB0;115200;1-32`,
`COM3;57600;1-8`. Defaults: 115200 baud, addresses 1-127, 20 ms poll
time-out, one discovery probe per round, a node is gone after 3 missed
polls, queues of 1000 frames. Flags: bit 0 no ninth bit (8N1), bit 1 fixed
length frames, bit 2 CRC 0x07, bit 3 broadcast address, bit 4 Linux kernel
RS-485 mode, bit 5 RTS direction control. See
[Configuration and flags](docs/configure.md).

```bash
vscp-dump -i canal --driver build/vscpl1drv-rs485.so --driver-config "/dev/ttyUSB0;115200;1-32"
```

## Download

Click a badge above to see the successful runs and download a build from a
run page: `vscpl1drv-rs485-linux-x86_64`, `-linux-arm64`, `-linux-armhf`,
`-macos-arm64`, `-macos-x86_64`, `-windows-x64` and `-windows-x86`. Each has a
`SHA256SUMS-<artifact>` file.

## Documentation

The [docs](docs/README.md) folder (a docsify site): overview,
[the RS-485 protocol](docs/protocol.md) and how the driver fills the gaps of
the specification, configuration, how the driver works, the CANAL API,
building, testing and connecting real hardware. `man vscpl1drv-rs485` after
installing.
