# vscpl1drv-rs485

A VSCP Level I driver, a *CANAL driver*, for RS-485 and RS-422. It is the
**master** of [VSCP Level I over RS-485/RS-422](https://grodansparadis.github.io/vscp-doc-spec/#/./vscp_level_i_over_rs-485_rs-422):
it polls the nodes on a segment for events, finds nodes that are added,
notices nodes that disappear, and sends the host's events to the nodes. To
the host - the VSCP daemon, VSCP Works, vscp-cli or the VSCP simulator - the
segment looks like any other Level I interface.

* The whole master of the specification: polling, NO_EVENTS, discovery of new
  nodes spread over the polling rounds, events between nodes, DOW CRC.
* The ninth (address) bit made with mark/space parity on ordinary serial
  ports and USB adapters (Linux, Windows; macOS sends it), or plain 8N1.
* RS-485 direction control by the adapter, the Linux kernel RS-485 mode or
  RTS.
* The points the specification leaves open (frame length, CRC variant,
  broadcast) are configurable; see [The RS-485 protocol](protocol.md).
* Tested against golden vectors and a simulated segment, and run by its
  test program against simulated nodes on a pseudo terminal.
* Built from the [Level I driver template](https://github.com/grodansparadis/vscpl1drv-template):
  CMake, CI for Linux, Raspberry Pi, macOS and Windows, man page.

The same master is used by [vscpl2drv-rs485](https://github.com/grodansparadis/vscpl2drv-rs485),
the Level II driver for the VSCP daemon.

## Quick start

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure

vscp-dump -i canal --driver build/vscpl1drv-rs485.so --driver-config "/dev/ttyUSB0;115200;1-32"
```

## Files

| File | What it is |
| :--- | :--------- |
| `src/canal-api.cpp` | The exported CANAL functions and the handle table |
| `src/rs485-driver.h/.cpp` | The CANAL driver object |
| `src/rs485-master.h/.cpp` | The bus master |
| `src/rs485-port.h/.cpp` | The serial port with the ninth bit |
| `src/vscp-rs485.h/.c` | Frames, CRC and receiver (plain C) |
| `src/driverinfo.h` | The configuration description for VSCP Works |
| `src/version.h` | Driver version (year.month.patch) |
| `tests/` | The test programs |
| `man/` | The man page |
