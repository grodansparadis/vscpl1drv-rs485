// rs485-driver.h
//
// The driver object of the VSCP Level I (CANAL) RS-485 driver.
//
// One CRs485Driver is created for every CanalOpen. It owns the serial port,
// the RS-485 bus master (rs485-master.h) and a worker thread that runs the
// master: it polls the nodes on the segment, and events they send end up
// in the receive queue the CANAL functions take frames from. Frames sent
// by the host are queued for the master, which sends them to the nodes.
//
// CANAL frames carry VSCP Level I events in the extended (29-bit) id:
//
//   bits 26-28  priority (received events: 3, normal)
//   bit  25     hard coded nickname (set: RS-485 addresses are hard coded)
//   bits 16-24  VSCP class
//   bits  8-15  VSCP type
//   bits  0-7   nickname = the RS-485 address of the node
//
// This file is part of the VSCP (https://www.vscp.org)
//
// The MIT License (MIT)
//
// Copyright (C) 2000-2026 Ake Hedman, Grodans Paradis AB <info@grodansparadis.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#ifndef RS485_DRIVER_H
#define RS485_DRIVER_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <memory>
#include <thread>
#include <vector>

#include <canal.h>

#include "rs485-master.h"
#include "rs485-port.h"

// Flags given to CanalOpen
#define RS485_FLAG_8N1          0x00000001 // No ninth (address) bit: plain 8N1
#define RS485_FLAG_FIXED_LENGTH 0x00000002 // EVENT frames always carry eight data bytes
#define RS485_FLAG_CRC_ATM      0x00000004 // CRC-8 polynomial 0x07 instead of the DOW CRC
#define RS485_FLAG_BROADCAST    0x00000008 // Host events to address 0xFF instead of to each node
#define RS485_FLAG_KERNEL_RS485 0x00000010 // Linux: kernel RS-485 mode (RTS while sending)
#define RS485_FLAG_RTS          0x00000020 // Set RTS while sending (direction control)

// Defaults for the configuration string
#define RS485_DEFAULT_BAUDRATE      115200
#define RS485_DEFAULT_ADDRESSES     "1-127"
#define RS485_DEFAULT_POLL_TIMEOUT  20
#define RS485_DEFAULT_DISCOVERY     1
#define RS485_DEFAULT_MISS_LIMIT    3
#define RS485_DEFAULT_QUEUE_SIZE    1000

// CANAL id of a VSCP Level I event
#define RS485_PRIORITY_RECEIVED 3
#define RS485_ID_HARDCODED      (1UL << 25)

class CRs485Driver {
public:
  CRs485Driver();
  ~CRs485Driver();

  /*!
    Open the interface.
    @param config CANAL configuration string (see parseConfig)
    @param flags CanalOpen flags (RS485_FLAG_*)
    @return CANAL_ERROR_SUCCESS or a CANAL error code
  */
  int open(const char *config, unsigned long flags);

  /*!
    Close the interface. Safe to call more than once.
  */
  void close(void);

  /*!
    Send a frame: a VSCP Level I event (extended id, sizeData <= 8). It is
    queued for the bus master.
    @param msg Frame to send
    @param timeout Milliseconds to wait for room in the transmit path,
           0 = do not wait (CanalSend), CANAL_BLOCK_FOREVER is not used here
    @return CANAL_ERROR_SUCCESS or a CANAL error code
  */
  int send(const canalMsgFD &msg, uint32_t timeout = 0);

  /*!
    Get the next received frame that passes the filter.
    @param msg Receives the frame
    @param fd true when called from the FD functions. Classic calls never
           get FD frames; they are skipped.
    @param block true to wait for a frame
    @param timeout Milliseconds to wait when blocking, 0 = forever
    @return CANAL_ERROR_SUCCESS, CANAL_ERROR_FIFO_EMPTY (not blocking) or
            CANAL_ERROR_TIMEOUT
  */
  int receive(canalMsgFD &msg, bool fd, bool block, uint32_t timeout);

  /*!
    Number of frames waiting (classic frames only when fd is false)
  */
  int dataAvailable(bool fd);

  void setFilter(uint32_t filter);
  void setMask(uint32_t mask);
  int setBaudrate(uint32_t baudrate);

  void getStatus(canalStatus &status);
  void getStatistics(canalStatistics &statistics);

  bool isOpen(void) const { return m_bOpen; }
  bool isFd(void) const { return false; } // RS-485 Level I: no CAN FD

  /// Active node addresses (for tests and status)
  std::vector<uint8_t> activeNodes(void);

  /// Parse an address list such as "1-32,40,50-52"
  static bool parseAddresses(const std::string &str, std::vector<uint8_t> &out);

  /// Which node(s) a host event is for: an address, or -1 for all nodes
  static int targetOf(uint16_t vscpClass, uint8_t vscpType, const uint8_t *data, uint8_t size);

private:
  /*!
    Parse the CANAL configuration string
    "device;baudrate;addresses;poll-timeout;discovery-rounds;miss-limit;queue-size"
    (see docs/configure.md). Only the device is required.
  */
  bool parseConfig(const char *config);

  // Put a received frame in the queue (filter, statistics, overrun)
  void addReceived(const canalMsgFD &msg);

  // Set the last error reported by CanalGetStatus
  void setError(unsigned long code, const std::string &text);

  // The worker thread reads from the hardware
  void workerThread(void);

  // --- HARDWARE: replace these for real hardware ---------------------------
  bool hwOpen(void);
  void hwClose(void);
  int hwWrite(const canalMsgFD &msg);
  bool hwRead(canalMsgFD &msg, uint32_t timeout);
  int hwSetBaudrate(uint32_t baudrate);
  // -------------------------------------------------------------------------

  // Configuration
  unsigned long m_flags;
  Rs485PortOptions m_portOpt;
  Rs485MasterConfig m_masterCfg;
  size_t m_maxQueue; // Max frames in the receive queue

  std::atomic<bool> m_bOpen;
  std::atomic<bool> m_bRun;
  std::thread m_worker;

  // Receive queue
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::deque<canalMsgFD> m_queue;

  // Filter/mask: a frame passes when ((id ^ filter) & mask) == 0
  std::atomic<uint32_t> m_filter;
  std::atomic<uint32_t> m_mask;

  // Status and statistics (protected by m_mutex)
  canalStatistics m_stat;
  canalStatus m_status;

  std::chrono::steady_clock::time_point m_start; // Time stamp base

  // HARDWARE: the serial port and the bus master run by the worker thread
  CRs485Port m_port;
  std::unique_ptr<CRs485Master> m_master;
  std::deque<canalMsgFD> m_hwRx; // Events from the master, only used by the worker thread
};

#endif
