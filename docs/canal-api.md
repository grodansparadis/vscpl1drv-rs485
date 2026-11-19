# The CANAL API

How the driver implements each CANAL function. The full specification is
at <https://grodansparadis.github.io/vscp-doc-canal>.

| Function | vscpl1drv-rs485 |
| :------- | :------- |
| `long CanalOpen(const char *pDevice, unsigned long flags)` | Parses the [configuration string](configure.md) and flags, opens the serial port and starts polling the segment. Returns a handle > 0, or 0 on failure (the reason is in the log of the host; `CanalGetStatus` of a later open tells it too). |
| `int CanalClose(long handle)` | Closes the interface. `CANAL_ERROR_NOT_OPEN` for an unknown handle. |
| `unsigned long CanalGetLevel(long handle)` | `CANAL_LEVEL_STANDARD` (1), 0 for an unknown handle. |
| `int CanalSend(long handle, PCANALMSG msg)` | Queues a VSCP Level I event (extended id) for the bus; the master sends it between polls to every active node, or to the node a protocol event names. `CANAL_ERROR_CAN_ID` for a standard id, `CANAL_ERROR_MTU` for more than 8 data bytes, `CANAL_ERROR_TRM_FULL` when the transmit queue is full. |
| `int CanalBlockingSend(long handle, PCANALMSG msg, unsigned long timeout)` | As `CanalSend` (queuing does not wait). |
| `int CanalReceive(long handle, PCANALMSG msg)` | Next event from a node, or `CANAL_ERROR_FIFO_EMPTY`. |
| `int CanalBlockingReceive(long handle, PCANALMSG msg, unsigned long timeout)` | Waits for a frame. `CANAL_ERROR_TIMEOUT` after `timeout` ms; 0 waits forever. `CANAL_ERROR_NOT_OPEN` when the interface is closed while waiting. |
| `int CanalDataAvailable(long handle)` | Number of frames in the queue (classic frames). |
| `int CanalGetStatus(long handle, PCANALSTATUS status)` | Channel state (`CANAL_STATUS_ACTIVE`, `..._RECIVE_BUFFER_FULL`, `..._STOPPED`) and the last error. |
| `int CanalGetStatistics(long handle, PCANALSTATISTICS stat)` | Events and bytes sent and received, overruns, and as `cntBusWarnings` the damaged frames on the bus (CRC and format errors, answers from the wrong address, port errors). |
| `int CanalSetFilter(long handle, unsigned long filter)` | See [filter and mask](configure.md#filter-and-mask). |
| `int CanalSetMask(long handle, unsigned long mask)` | See [filter and mask](configure.md#filter-and-mask). |
| `int CanalSetBaudrate(long handle, unsigned long baudrate)` | Sets the baud rate of the serial port in bit/s. `CANAL_ERROR_PARAMETER` for a rate the port does not support. |
| `unsigned long CanalGetVersion(void)` | The CANAL version of `canal.h`: major in the most significant byte, then minor and sub version. |
| `unsigned long CanalGetDllVersion(void)` | The driver version from `src/version.h`: year, month, patch (high byte), patch (low byte). |
| `const char *CanalGetVendorString(void)` | `DRIVER_VENDOR` in `canal-api.cpp`. |
| `const char *CanalGetDriverInfo(void)` | The XML in `src/driverinfo.h`. |

## CAN FD functions (CANAL 1.1)

VSCP Level I events have at most eight data bytes. The FD functions are
there for hosts that use them: `CanalSendFD` sends classic frames and gives
`CANAL_ERROR_FD_SUPPORT` for FD frames, the receive functions give classic
frames.

## Error codes

The functions return the `CANAL_ERROR_...` codes from `canal.h`. The ones
the driver uses:

| Code | When |
| :--- | :--- |
| `CANAL_ERROR_SUCCESS` (0) | OK |
| `CANAL_ERROR_NOT_OPEN` (33) | Unknown handle, or closed while waiting |
| `CANAL_ERROR_PARAMETER` (34) | Null pointer, FD flag in a classic call, invalid configuration |
| `CANAL_ERROR_FIFO_EMPTY` (8) | No frame (non-blocking receive) |
| `CANAL_ERROR_TIMEOUT` (32) | No frame within the time-out |
| `CANAL_ERROR_CAN_ID` (5) | Not an extended id (VSCP events use extended ids) |
| `CANAL_ERROR_MTU` (39) | More than eight data bytes |
| `CANAL_ERROR_FD_SUPPORT` (40) | CAN FD frame |
| `CANAL_ERROR_TRM_FULL` (21) | The transmit queue is full |
| `CANAL_ERROR_HARDWARE` (13) | The serial port could not be opened or configured |
