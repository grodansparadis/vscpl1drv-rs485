% VSCPL1DRV-RS485(1) VSCP Level I driver
% Åke Hedman, Grodans Paradis AB
% November 2026

# NAME

vscpl1drv-rs485 - VSCP Level I (CANAL) driver for RS-485/RS-422, the bus master

# SYNOPSIS

**vscpl1drv-rs485.so** (Linux), **vscpl1drv-rs485.dylib** (macOS),
**vscpl1drv-rs485.dll** (Windows)

A CANAL driver. It is loaded by the VSCP daemon (**vscpd**(8)), VSCP Works,
**vscp-cli**, the VSCP simulator or any other program that uses the CANAL
API. The host passes a configuration string and flags to **CanalOpen**.

# DESCRIPTION

**vscpl1drv-rs485** is the master of "VSCP Level I over RS-485/RS-422" in the
VSCP specification. It polls the nodes on an RS-485 segment for VSCP Level I
events, finds nodes that are added and notices nodes that disappear, and
sends the events of the host to the nodes.

At start every configured address is polled once. Then the nodes that
answered are polled in turn, and after every polling round (see
*discovery-rounds*) one configured address without a node is probed. A node
that misses *miss-limit* polls in a row counts as gone.

An event from the host is sent to every active node; the Level I protocol
events that name a node in their first data byte (register read and write,
nickname, boot loader, who is there, ...) are sent to that node only.

Frames start with an address character that has the ninth bit set, made
with mark/space parity. The CRC is CRC-8/MAXIM-DOW.

# CONFIGURATION STRING

A semicolon separated list. Only *device* is required; empty parts get the
default. Numbers are decimal or hex (0x...).

    device;baudrate;addresses;poll-timeout;discovery-rounds;miss-limit;queue-size

*device*
:   Serial port of the RS-485 adapter, for example /dev/ttyUSB0,
    /dev/serial/by-id/..., /dev/ttyAMA0 or COM3.

*baudrate*
:   Baud rate of the segment, default 115200.

*addresses*
:   The node addresses on the segment (1-254), a list of numbers and ranges
    such as 1-32,40. Default 1-127.

*poll-timeout*
:   Milliseconds to wait for the answer to a poll, default 20.

*discovery-rounds*
:   Probe one address without a node after this many polling rounds,
    default 1. 0 polls every configured address in every round.

*miss-limit*
:   Missed polls in a row before a node counts as gone, default 3.

*queue-size*
:   Size of the receive and transmit queues in frames, default 1000. Frames
    that arrive when the receive queue is full are counted as overruns.

An invalid configuration string makes **CanalOpen** fail.

# FLAGS

Bit 0 (0x01)
:   No ninth (address) bit: plain 8N1.

Bit 1 (0x02)
:   EVENT frames always carry eight data bytes (15 characters).

Bit 2 (0x04)
:   CRC-8 with polynomial 0x07 instead of the DOW CRC.

Bit 3 (0x08)
:   Send events from the host once, to address 0xFF, instead of to each node.

Bit 4 (0x10)
:   Linux: use the kernel RS-485 mode (RTS set while sending).

Bit 5 (0x20)
:   Set RTS while sending a frame.

# CANAL ID

Events are VSCP Level I events with extended (29-bit) ids: priority in bits
26-28 (3 for received events), the hard coded nickname bit 25 (set), the
VSCP class in bits 16-24, the type in bits 8-15 and the RS-485 address of
the node in bits 0-7. **CanalSetFilter** and **CanalSetMask** select
received events: an event is received when ((id XOR filter) AND mask) is
zero.

# STATISTICS

**CanalGetStatistics** counts the events and data bytes sent and received,
overruns, and as bus warnings the damaged frames: CRC and format errors,
answers from another address than the polled one, and port errors.

# EXAMPLES

Configuration of the driver in the VSCP daemon (*/etc/vscp/vscpd.json*):

    "drivers": {
      "level1": [
        {
          "enable": true,
          "name": "rs485",
          "config": "/dev/ttyUSB0;115200;1-32",
          "flags": 0,
          "translation": 0,
          "path": "/var/lib/vscp/drivers/level1/vscpl1drv-rs485.so",
          "guid": "FF:FF:FF:FF:FF:FF:FF:F5:00:00:00:00:00:00:00:00"
        }
      ]
    }

Watch the events of the segment with **vscp-dump**(1):

    vscp-dump -i canal --driver /var/lib/vscp/drivers/level1/vscpl1drv-rs485.so \
              --driver-config "/dev/ttyUSB0;115200;1-32"

# FILES

*/var/lib/vscp/drivers/level1/vscpl1drv-rs485.so*
:   The driver (Linux).

# SEE ALSO

**vscpd**(8), **vscp-dump**(1), **vscp-send**(1), **vscpsim**(1)

VSCP Level I over RS-485/RS-422:
<https://grodansparadis.github.io/vscp-doc-spec/#/./vscp_level_i_over_rs-485_rs-422>

The CANAL API: <https://grodansparadis.github.io/vscp-doc-canal>

The source code and the documentation:
<https://github.com/grodansparadis/vscpl1drv-rs485>

# COPYRIGHT

Copyright 2000-2026 Åke Hedman, Grodans Paradis AB. MIT license.
