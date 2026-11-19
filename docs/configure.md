# Configuration and flags

The host gives the driver a configuration string and flags when it opens it
(`CanalOpen`). In the VSCP daemon they are the `config` and `flags` of the
driver entry; in VSCP Works the dialog of the CANAL connection shows them
(from `CanalGetDriverInfo`).

## Configuration string

```text
device;baudrate;addresses;poll-timeout;discovery-rounds;miss-limit;queue-size
```

Only the device is required; empty or missing parts get the default.
Numbers are decimal or hex (`0x...`).

| Part | Default | Meaning |
| :--- | :------ | :------ |
| device | — | Serial port of the RS-485 adapter: `/dev/ttyUSB0`, `/dev/serial/by-id/usb-FTDI_...` (stays the same after a reboot), `/dev/ttyAMA0`, `COM3` |
| baudrate | 115200 | Baud rate of the segment. 1200-230400 everywhere; 460800, 500000, 921600 and 1000000 where the system has them |
| addresses | 1-127 | The node addresses on the segment, a list of numbers and ranges: `1-32,40,50-52`. Only these are polled; fewer addresses make discovery faster |
| poll-timeout | 20 | Milliseconds to wait for the answer to a poll |
| discovery-rounds | 1 | Probe one address without a node after this many polling rounds. `0` = no discovery: every configured address is polled in every round |
| miss-limit | 3 | Missed polls in a row before a node counts as gone |
| queue-size | 1000 | Size of the receive queue and of the transmit queue, in frames |

Examples:

```text
/dev/ttyUSB0
/dev/ttyUSB0;115200;1-16
COM4;57600;1-8,20;30;2;3;500
```

## Flags

| Bit | Value | Meaning |
| :-: | :---: | :------ |
| 0 | 0x01 | No ninth (address) bit: plain 8N1, for adapters and nodes without it |
| 1 | 0x02 | EVENT frames always carry eight data bytes (15 characters) |
| 2 | 0x04 | CRC-8 with polynomial 0x07 (as vscp `crc8.c`) instead of the DOW CRC |
| 3 | 0x08 | Send events from the host to address 0xFF instead of to each node |
| 4 | 0x10 | Linux: kernel RS-485 mode (`TIOCSRS485`, RTS set while sending) |
| 5 | 0x20 | Set RTS while sending a frame |

See [The RS-485 protocol](protocol.md) for what bits 0-3 change on the
wire.

## The ninth bit and the adapter

PC serial ports have no ninth data bit; the driver makes it with the parity
bit:

| System | Sending | Receiving |
| :----- | :------ | :-------- |
| Linux | Mark parity for the address character, space parity for the rest | Parity errors mark the characters with the ninth bit set, so frames are found from the bus |
| macOS | Even or odd parity per character, chosen so the parity bit is right | Ninth bit not known: frames are found from the poll timing |
| Windows | Mark/space parity | Ninth bit not known: frames are found from the poll timing |

The adapter must support parity: most USB adapters (FTDI, CP210x, CH340)
and the UARTs of the Raspberry Pi do. With flag bit 0 the driver uses plain
8N1, for nodes that do not use the ninth bit.

Changing the parity between the address character and the rest takes a
moment on USB adapters; that is a short pause between the first and the
second character of a frame from the master, which nodes accept (the address
marker tells them where a frame starts).

## Direction control

A half duplex RS-485 transceiver must be switched to sending while the
master sends and back to receiving before the node answers.

* **Automatic** (no flag): adapters that switch by themselves. Nearly all
  USB RS-485 adapters do. Use this when you can.
* **Kernel RS-485 mode** (flag bit 4, Linux): the serial driver sets RTS
  while it sends. Supported by many UARTs on embedded boards (also the
  Raspberry Pi with a suitable overlay) and by some USB adapters.
* **RTS from the driver** (flag bit 5): the driver sets RTS, sends, waits
  until the frame has left the computer and clears RTS. With USB adapters
  this can be late by a USB frame (about 1 ms); prefer one of the above.

Adapters that receive their own transmission (no receiver disable while
sending) work: the master recognises its echoed poll.

## Filter and mask

`CanalSetFilter`/`CanalSetMask` work on the CANAL id of received events: a
frame passes when `((id ^ filter) & mask) == 0`. The id of a VSCP Level I
event is

| Bits | Content |
| :--- | :------ |
| 26-28 | Priority (received events: 3) |
| 25 | Hard coded nickname (always set: RS-485 addresses are set in the node) |
| 16-24 | VSCP class |
| 8-15 | VSCP type |
| 0-7 | Nickname = RS-485 address of the node |

For example filter `0x000A0000`, mask `0x01FF0000` receives only
CLASS1.MEASUREMENT (10).

## Baud rate

`CanalSetBaudrate` changes the baud rate of the port (in bit/s). All nodes
on the segment must use the same rate.
