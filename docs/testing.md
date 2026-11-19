# Test and use

## The tests

```bash
ctest --test-dir build --output-on-failure
```

| Test | What it checks |
| :--- | :------------- |
| `rs485` | `tests/test-rs485.cpp`: the protocol against golden vectors (POLL, NO_EVENTS, EVENT with 0, 1 and 8 data bytes, class bit 8, fixed length frames, both CRCs and their check values), that every single bit error in a frame is found, format errors, resynchronisation on an address character, decoding of marked characters (Linux), and the bus master against a simulated segment: start-up scan, polling order, discovery, lost and returning nodes, events between nodes, host events to all/one/broadcast, full transmit queue, damaged answers (CRC, truncated, wrong address), adapters that echo, ports without the ninth bit. |
| `driver` | `tests/test-driver.cpp` loads the built library with `dlopen` (`LoadLibrary` on Windows) like a host and checks every CANAL function. On Linux and macOS it runs a real segment: simulated nodes at the other end of a pseudo terminal answer the driver's polls (in 8N1 mode, a pseudo terminal has no parity bit), send events with classes above 255, and receive the events the host sends to all nodes and to one node. Filter/mask, statistics, status, invalid frames and closing while another thread waits are checked too. |
| `update_version` | The version updater used by the *Update version* workflow. |

The golden vectors in `test-rs485.cpp` were made with a separate Python
implementation of the frame format and CRC, not with the driver's code.

The `driver` test uses the library exactly like a host, so a missing or
wrongly exported function fails it.

## Try it with vscp-cli

[vscp-cli](https://github.com/grodansparadis/vscp-cli) can use any CANAL
driver with `-i canal`:

```bash
# Everything the nodes on the segment send
vscp-dump -i canal --driver build/vscpl1drv-rs485.so --driver-config "/dev/ttyUSB0;115200;1-32"
```

## Use it in the VSCP daemon

In `/etc/vscp/vscpd.json`:

```json
"drivers": {
  "level1": [
    {
      "enable": true,
      "name": "rs485",
      "config": "/dev/serial/by-id/usb-FTDI_USB-RS485_Cable_FT1234AB-if00-port0;115200;1-32",
      "flags": 0,
      "translation": 0,
      "path": "/var/lib/vscp/drivers/level1/vscpl1drv-rs485.so",
      "guid": "FF:FF:FF:FF:FF:FF:FF:F5:00:00:00:00:00:00:00:00"
    }
  ]
}
```

The user the daemon runs as must be allowed to use the serial port (group
`dialout` on Debian and Raspberry Pi OS).

## Use it in VSCP Works

Add a new *CANAL* connection and select the driver file. VSCP Works reads the
driver information (`CanalGetDriverInfo`) and shows the configuration
string items and flags described there. Node scan, configuration and the
bootloader wizard work through it like through any Level I interface: their
register reads and writes are sent to the node they are for.

## On real hardware

1. Connect the RS-485 adapter: A to A, B to B on every node, and the signal
   ground. Terminate both ends of the cable (120 Ω) and make sure the bus has
   fail-safe biasing (most adapters have it).
2. Give every node its own address and the same baud rate.
3. Start with a short address list that includes the nodes, for example
   `/dev/ttyUSB0;115200;1-8`, and look at the events with `vscp-dump`.
4. No events? Check the statistics (`CanalGetStatistics`): many bus warnings
   mean damaged frames (wrong baud rate, A/B swapped, missing termination,
   two nodes with the same address, or a different CRC or frame length in
   the nodes: flags bits 1 and 2). If nodes do not answer at all, try a
   longer poll time-out, or flag bit 0 for nodes without the ninth bit.
