# How the driver works

```text
 host (vscpd, VSCP Works, vscp-cli, ...)
   │  CanalOpen / CanalSend / CanalBlockingReceive / ...        canal-api.cpp
   ▼
 handle table ──► CRs485Driver (one per CanalOpen)               rs485-driver.cpp
                    │ send() ─► hwWrite() ─► master transmit queue
                    │
                    │ worker thread: hwRead() ─► CRs485Master::step()   rs485-master.cpp
                    │      │                       │ poll / send / probe
                    │      │                       ▼
                    │      │                   CRs485Port (ninth bit,      rs485-port.cpp
                    │      │                   direction control)
                    │      │                       │ frames, CRC, receiver  vscp-rs485.c
                    │      ▼ events from the nodes as CANAL frames
                    │   filter/mask, time stamp, statistics
                    │   receive queue (max queue-size frames)
                    └ receive() ◄── blocking with time-out, wakes on close
```

The driver keeps the structure of the
[Level I driver template](https://github.com/grodansparadis/vscpl1drv-template):
the CANAL API, handle table, receive queue, filter, statistics and blocking
calls are the template's. The RS-485 parts are in four files:

| File | What it does |
| :--- | :----------- |
| `src/vscp-rs485.h/.c` | The protocol in plain C99 without operating system calls: CRC (DOW and 0x07), frame encoding and a receiver that is fed one character at a time. Shared with vscpl2drv-rs485 and usable in node firmware. |
| `src/rs485-master.h/.cpp` | The bus master: polling, discovery of new nodes, lost nodes, events from the host, statistics. It talks to an `IRs485Port`, so it is tested against a simulated bus. |
| `src/rs485-port.h/.cpp` | The serial port: the ninth bit with mark/space parity, decoding of marked characters on Linux, direction control, time-outs. POSIX and Windows. |
| `src/rs485-driver.h/.cpp` | The CANAL driver object: configuration string and flags, CANAL id ⇄ VSCP class/type/nickname, which node(s) an event from the host is for. |

## One bus transaction at a time

The worker thread calls `hwRead()` in a loop, and every call lets the master
do one transaction:

1. send one event from the host (if one is waiting and the last transaction
   was not a send), or
2. poll the next node, read its answer and give an event to the driver, or
3. probe an address without a node.

A transaction takes at most the poll time-out plus the time of one frame.
The host's `CanalSend` only queues the event, so it never waits for the bus.

When there is nothing to do (no nodes, discovery not due), the worker sleeps
10 ms.

## Events from nodes

Every EVENT a node sends in answer to a poll becomes a CANAL frame with an
extended id: priority 3, the hard coded bit, class, type and the node's
address as nickname (see [Configuration](configure.md#filter-and-mask)). It
then goes through the filter into the receive queue like in the template.

## Thread safety

Hosts call the CANAL functions from several threads. The receive queue,
status and statistics are protected by the driver's mutex; the master's
transmit queue, node list and statistics by the master's mutex. The bus
itself is only used by the worker thread.

## Closing

`CanalClose` stops the worker (after the transaction it is in, at most the
poll time-out), closes the port and wakes all waiting receivers, which
return `CANAL_ERROR_NOT_OPEN`.
