// rs485-driver.cpp
//
// The driver object of the VSCP Level I (CANAL) RS-485 driver.
// See rs485-driver.h and docs/design.md.
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

#include "rs485-driver.h"

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <vector>

///////////////////////////////////////////////////////////////////////////////
// Helpers
//

// Split "a;b;c" into its parts (empty parts kept)
static std::vector<std::string>
splitConfig(const std::string &str)
{
  std::vector<std::string> parts;
  std::string part;
  std::istringstream in(str);
  while (std::getline(in, part, ';')) {
    // Trim white space
    size_t b = part.find_first_not_of(" \t\r\n");
    size_t e = part.find_last_not_of(" \t\r\n");
    parts.push_back((std::string::npos == b) ? std::string() : part.substr(b, e - b + 1));
  }
  return parts;
}

// Parse a decimal or 0x hex number. Empty gives the default.
static bool
parseNumber(const std::string &str, uint32_t def, uint32_t max, uint32_t &value)
{
  if (str.empty()) {
    value = def;
    return true;
  }
  char *end              = nullptr;
  unsigned long long val = std::strtoull(str.c_str(), &end, 0); // 0x.. = hex
  if ((nullptr == end) || ('\0' != *end) || (val > max)) {
    return false;
  }
  value = (uint32_t) val;
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// CRs485Driver
//

CRs485Driver::CRs485Driver()
  : m_flags(0)
  , m_maxQueue(RS485_DEFAULT_QUEUE_SIZE)
  , m_bOpen(false)
  , m_bRun(false)
  , m_filter(0)
  , m_mask(0)
{
  std::memset(&m_stat, 0, sizeof(m_stat));
  std::memset(&m_status, 0, sizeof(m_status));
}

CRs485Driver::~CRs485Driver()
{
  close();
}

bool
CRs485Driver::parseAddresses(const std::string &str, std::vector<uint8_t> &out)
{
  return CRs485Master::parseAddresses(str, out);
}

bool
CRs485Driver::parseConfig(const char *config)
{
  std::vector<std::string> parts = splitConfig((nullptr == config) ? "" : config);
  parts.resize(7); // Missing parts are empty, i.e. defaults

  if (parts[0].empty()) {
    setError(CANAL_ERROR_PARAMETER, "No serial port given (part 1 of the configuration)");
    return false;
  }
  m_portOpt              = Rs485PortOptions();
  m_portOpt.device       = parts[0];
  m_portOpt.nineBit      = !(m_flags & RS485_FLAG_8N1);
  m_portOpt.kernelRs485  = (m_flags & RS485_FLAG_KERNEL_RS485);
  m_portOpt.rtsDirection = (m_flags & RS485_FLAG_RTS);

  if (!parseNumber(parts[1], RS485_DEFAULT_BAUDRATE, 4000000, m_portOpt.baudrate) || (0 == m_portOpt.baudrate)) {
    setError(CANAL_ERROR_PARAMETER, "Invalid baud rate (part 2 of the configuration)");
    return false;
  }

  m_masterCfg = Rs485MasterConfig();
  if (!parseAddresses(parts[2].empty() ? RS485_DEFAULT_ADDRESSES : parts[2], m_masterCfg.addresses)) {
    setError(CANAL_ERROR_PARAMETER, "Invalid node addresses, e.g. 1-32,40 (part 3 of the configuration)");
    return false;
  }
  if (!parseNumber(parts[3], RS485_DEFAULT_POLL_TIMEOUT, 10000, m_masterCfg.pollTimeout) ||
      (0 == m_masterCfg.pollTimeout)) {
    setError(CANAL_ERROR_PARAMETER, "Invalid poll time-out (part 4 of the configuration)");
    return false;
  }
  if (!parseNumber(parts[4], RS485_DEFAULT_DISCOVERY, 100000, m_masterCfg.discoveryRounds)) {
    setError(CANAL_ERROR_PARAMETER, "Invalid discovery interval (part 5 of the configuration)");
    return false;
  }
  if (!parseNumber(parts[5], RS485_DEFAULT_MISS_LIMIT, 255, m_masterCfg.missLimit) || (0 == m_masterCfg.missLimit)) {
    setError(CANAL_ERROR_PARAMETER, "Invalid missed poll limit (part 6 of the configuration)");
    return false;
  }
  uint32_t queue;
  if (!parseNumber(parts[6], RS485_DEFAULT_QUEUE_SIZE, 1000000, queue) || (0 == queue)) {
    setError(CANAL_ERROR_PARAMETER, "Invalid receive queue size (part 7 of the configuration)");
    return false;
  }
  m_maxQueue                   = queue;
  m_masterCfg.txQueueSize      = queue;
  m_masterCfg.broadcast        = (m_flags & RS485_FLAG_BROADCAST);
  m_masterCfg.codec.fixed      = (m_flags & RS485_FLAG_FIXED_LENGTH) ? 1 : 0;
  m_masterCfg.codec.crc        = (m_flags & RS485_FLAG_CRC_ATM) ? VSCP_RS485_CRC_ATM : VSCP_RS485_CRC_DOW;
  // Between characters of a frame: three character times, at least 2 ms
  // (11 bits per character with the ninth bit)
  const uint32_t charUs        = 11u * 1000000u / m_portOpt.baudrate;
  m_masterCfg.charTimeout      = std::max<uint32_t>(2, (3 * charUs + 999) / 1000);
  return true;
}

int
CRs485Driver::open(const char *config, unsigned long flags)
{
  if (m_bOpen) {
    return CANAL_ERROR_INIT_READY;
  }

  m_flags = flags;
  if (!parseConfig(config)) {
    return CANAL_ERROR_PARAMETER;
  }

  m_start = std::chrono::steady_clock::now();
  if (!hwOpen()) {
    setError(CANAL_ERROR_HARDWARE, "Unable to open the hardware");
    return CANAL_ERROR_HARDWARE;
  }

  m_bRun   = true;
  m_worker = std::thread(&CRs485Driver::workerThread, this);
  m_bOpen  = true;

  std::lock_guard<std::mutex> lock(m_mutex);
  m_status.channel_status = CANAL_STATUS_ACTIVE;
  return CANAL_ERROR_SUCCESS;
}

void
CRs485Driver::close(void)
{
  if (!m_bRun && !m_worker.joinable()) {
    return;
  }

  m_bRun = false;
  if (m_worker.joinable()) {
    m_worker.join();
  }
  hwClose();
  m_bOpen = false;

  // Wake blocking receivers so they can return
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status.channel_status = CANAL_STATUS_STOPPED;
  }
  m_cv.notify_all();
}

int
CRs485Driver::send(const canalMsgFD &msg, uint32_t /*timeout*/)
{
  if (!m_bOpen) {
    return CANAL_ERROR_NOT_OPEN;
  }

  if (msg.flags & CANAL_IDFLAG_FD) {
    return CANAL_ERROR_FD_SUPPORT; // VSCP Level I: eight data bytes at most
  }
  if (msg.sizeData > 8) {
    return CANAL_ERROR_MTU;
  }
  // VSCP Level I events use extended ids
  if (!(msg.flags & CANAL_IDFLAG_EXTENDED) || (msg.id > CAN_MAX_EXTENDED_ID)) {
    return CANAL_ERROR_CAN_ID;
  }

  int rv = hwWrite(msg);
  if (CANAL_ERROR_SUCCESS == rv) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stat.cntTransmitFrames++;
    m_stat.cntTransmitData += msg.sizeData;
  }
  return rv;
}

void
CRs485Driver::addReceived(const canalMsgFD &msgIn)
{
  canalMsgFD msg = msgIn;

  // The filter is applied to the id. A mask bit set to one means that
  // bit must be equal to the filter bit. Mask 0 lets everything through.
  if ((msg.id ^ m_filter) & m_mask) {
    return;
  }

  // Time stamp in microseconds (wraps after ~71 minutes, as in CANAL)
  msg.timestamp = (uint32_t) std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - m_start)
                    .count();

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_queue.size() >= m_maxQueue) {
      m_stat.cntOverruns++;
      m_status.channel_status |= CANAL_STATUS_RECIVE_BUFFER_FULL;
      return;
    }
    m_status.channel_status &= ~CANAL_STATUS_RECIVE_BUFFER_FULL;
    m_queue.push_back(msg);
    m_stat.cntReceiveFrames++;
    m_stat.cntReceiveData += msg.sizeData;
  }
  m_cv.notify_one();
}

int
CRs485Driver::receive(canalMsgFD &msg, bool fd, bool block, uint32_t timeout)
{
  std::unique_lock<std::mutex> lock(m_mutex);

  // Classic callers do not get FD frames: drop them from the head
  auto dropFd = [&]() {
    if (!fd) {
      while (!m_queue.empty() && (m_queue.front().flags & CANAL_IDFLAG_FD)) {
        m_queue.pop_front();
      }
    }
  };

  dropFd();
  if (m_queue.empty()) {
    if (!block) {
      return m_bOpen ? CANAL_ERROR_FIFO_EMPTY : CANAL_ERROR_NOT_OPEN;
    }
    auto ready = [&]() {
      dropFd();
      return !m_queue.empty() || !m_bOpen;
    };
    if (CANAL_BLOCK_FOREVER == timeout) {
      m_cv.wait(lock, ready);
    }
    else if (!m_cv.wait_for(lock, std::chrono::milliseconds(timeout), ready)) {
      return CANAL_ERROR_TIMEOUT;
    }
    if (m_queue.empty()) {
      return CANAL_ERROR_NOT_OPEN; // Closed while waiting
    }
  }

  msg = m_queue.front();
  m_queue.pop_front();
  return CANAL_ERROR_SUCCESS;
}

int
CRs485Driver::dataAvailable(bool fd)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (fd) {
    return (int) m_queue.size();
  }
  int count = 0;
  for (const auto &m : m_queue) {
    if (!(m.flags & CANAL_IDFLAG_FD)) {
      count++;
    }
  }
  return count;
}

void
CRs485Driver::setFilter(uint32_t filter)
{
  m_filter = filter;
}

void
CRs485Driver::setMask(uint32_t mask)
{
  m_mask = mask;
}

int
CRs485Driver::setBaudrate(uint32_t baudrate)
{
  if (!m_bOpen) {
    return CANAL_ERROR_NOT_OPEN;
  }
  return hwSetBaudrate(baudrate);
}

void
CRs485Driver::getStatus(canalStatus &status)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  status = m_status;
}

void
CRs485Driver::getStatistics(canalStatistics &statistics)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  statistics = m_stat;
}

void
CRs485Driver::setError(unsigned long code, const std::string &text)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_status.lasterrorcode    = code;
  m_status.lasterrorsubcode = 0;
  std::strncpy(m_status.lasterrorstr, text.c_str(), sizeof(m_status.lasterrorstr) - 1);
  m_status.lasterrorstr[sizeof(m_status.lasterrorstr) - 1] = '\0';
}

void
CRs485Driver::workerThread(void)
{
  canalMsgFD msg;
  while (m_bRun) {
    std::memset(&msg, 0, sizeof(msg));
    if (hwRead(msg, 100)) {
      addReceived(msg);
    }
  }
}

std::vector<uint8_t>
CRs485Driver::activeNodes(void)
{
  return m_master ? m_master->activeNodes() : std::vector<uint8_t>();
}

int
CRs485Driver::targetOf(uint16_t vscpClass, uint8_t vscpType, const uint8_t *data, uint8_t size)
{
  return CRs485Master::targetOf(vscpClass, vscpType, data, size);
}

///////////////////////////////////////////////////////////////////////////////
//                                HARDWARE
//
// The serial port and the RS-485 bus master. The worker thread calls
// hwRead in a loop; every call lets the master do one bus transaction
// (send a host event, poll a node, or probe for a new node).
///////////////////////////////////////////////////////////////////////////////

bool
CRs485Driver::hwOpen(void)
{
  if (!m_port.open(m_portOpt)) {
    setError(CANAL_ERROR_HARDWARE, m_port.error());
    return false;
  }
  m_hwRx.clear();
  m_master.reset(new CRs485Master(m_port, m_masterCfg, [this](const vscp_rs485_frame_t &f) {
    // A VSCP Level I event from a node becomes a CANAL frame
    canalMsgFD msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.flags    = CANAL_IDFLAG_EXTENDED;
    msg.id       = ((uint32_t) RS485_PRIORITY_RECEIVED << 26) | RS485_ID_HARDCODED |
                   ((uint32_t) (f.vscp_class & 0x1FF) << 16) | ((uint32_t) f.vscp_type << 8) | f.src;
    msg.sizeData = f.size;
    std::memcpy(msg.data, f.data, f.size);
    m_hwRx.push_back(msg);
  }));
  return true;
}

void
CRs485Driver::hwClose(void)
{
  m_master.reset();
  m_port.close();
  m_hwRx.clear();
}

int
CRs485Driver::hwWrite(const canalMsgFD &msg)
{
  vscp_rs485_frame_t f;
  std::memset(&f, 0, sizeof(f));
  f.op         = VSCP_RS485_OP_EVENT;
  f.vscp_class = (uint16_t) ((msg.id >> 16) & 0x1FF);
  f.vscp_type  = (uint8_t) ((msg.id >> 8) & 0xFF);
  f.size       = msg.sizeData;
  std::memcpy(f.data, msg.data, msg.sizeData);
  if (!m_master || !m_master->queueEvent(f, targetOf(f.vscp_class, f.vscp_type, f.data, f.size))) {
    return CANAL_ERROR_TRM_FULL;
  }
  return CANAL_ERROR_SUCCESS;
}

bool
CRs485Driver::hwRead(canalMsgFD &msg, uint32_t timeout)
{
  if (m_hwRx.empty() && m_master) {
    if (!m_master->step()) {
      // Nothing to do on the bus right now
      std::this_thread::sleep_for(std::chrono::milliseconds(std::min<uint32_t>(timeout, 10)));
    }
    // Damaged frames on the bus count as bus warnings (missed polls do
    // not: probing for new nodes gives them all the time)
    const Rs485MasterStats st = m_master->statistics();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stat.cntBusWarnings = (unsigned long) (st.crcErrors + st.frameErrors + st.wrongSender + st.portErrors);
  }
  if (m_hwRx.empty()) {
    return false;
  }
  msg = m_hwRx.front();
  m_hwRx.pop_front();
  return true;
}

int
CRs485Driver::hwSetBaudrate(uint32_t baudrate)
{
  // The baud rate of the RS-485 segment (all nodes must use it too)
  if (!m_port.setBaudrate(baudrate)) {
    setError(CANAL_ERROR_PARAMETER, m_port.error());
    return CANAL_ERROR_PARAMETER;
  }
  return CANAL_ERROR_SUCCESS;
}
