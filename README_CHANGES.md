# Changes in vscpl1drv-rs485

Every change that goes into a release is listed here, newest release first.
Add each change under **Unreleased** when it is made. When a release is
tagged, rename **Unreleased** to the version and date, and start a new
**Unreleased** section above it.

## Unreleased

- Rewritten as the full master of "VSCP Level I over RS-485/RS-422" (VSCP specification), on the structure of vscpl1drv-template: polling of the nodes, NO_EVENTS, start-up scan and discovery of new nodes spread over the polling rounds, lost nodes after missed polls, events from nodes (also those for other nodes) to the host, events from the host to every active node or to the node a Level I protocol event names, optional broadcast address
- Protocol core in plain C (`src/vscp-rs485.c`): frames, CRC-8/MAXIM-DOW (or 0x07), a character receiver with resynchronisation on the address marker; shared with vscpl2drv-rs485
- Ninth (address) bit with mark/space parity (Linux, with decoding of received marks; Windows; macOS per character parity), or plain 8N1; direction control by the adapter, the Linux kernel RS-485 mode or RTS
- The points the specification leaves open (EVENT frame length, CRC variant, broadcast) are configurable and documented in docs/protocol.md
- Configuration string `device;baudrate;addresses;poll-timeout;discovery-rounds;miss-limit;queue-size`, six flags, described to VSCP Works by `CanalGetDriverInfo`
- CMake build (VSCP fetched with FetchContent), CI for Linux, Raspberry Pi, macOS and Windows, update-version workflow, man page, docsify documentation
- Tests: golden vectors made with an independent implementation, every single bit error detected, the master against a simulated segment, and the built driver against simulated nodes on a pseudo terminal
- Removed the old Windows prototype (`common/`, `windows/`) and the vscp submodule
- The vscp submodule (vscp) points to the latest vscp master (d75d4f5c6)
- Updated copyright (6ee7bbf)
- Added submodule (25d3ad5)
- Moved from main repo. (8f9d277)
- Initial commit (299344b)

No release has been tagged yet.
