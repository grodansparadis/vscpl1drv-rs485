// rs485-master.cpp
//
// The RS-485 bus master. See rs485-master.h and docs/protocol.md.
//
// This file is part of the VSCP (https://www.vscp.org)
// SPDX-License-Identifier: MIT
// Copyright (C) 2000-2026 Ake Hedman, Grodans Paradis AB <info@grodansparadis.com>
//

#include "rs485-master.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace {

// Parse a decimal or 0x hex number up to max
bool
parseNumber(const std::string &str, uint32_t max, uint32_t &value)
{
  if (str.empty()) {
    return false;
  }
  char *end              = nullptr;
  unsigned long long val = std::strtoull(str.c_str(), &end, 0);
  if ((nullptr == end) || ('\0' != *end) || (val > max)) {
    return false;
  }
  value = (uint32_t) val;
  return true;
}

} // namespace


CRs485Master::CRs485Master(IRs485Port &port, const Rs485MasterConfig &cfg, EventCallback onEvent, NodeCallback onNode)
  : m_port(port)
  , m_cfg(cfg)
  , m_onEvent(onEvent)
  , m_onNode(onNode)
  , m_miss(256, 0)
{
  // Valid node addresses only, each once, in order
  std::vector<uint8_t> list;
  for (uint8_t a : m_cfg.addresses) {
    if ((a >= VSCP_RS485_ADDR_FIRST) && (a <= VSCP_RS485_ADDR_LAST) &&
        (std::find(list.begin(), list.end(), a) == list.end())) {
      list.push_back(a);
    }
  }
  m_cfg.addresses = list;
}

bool
CRs485Master::queueEvent(const vscp_rs485_frame_t &frame, int target)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_tx.size() >= m_cfg.txQueueSize) {
    return false;
  }
  vscp_rs485_frame_t f = frame;
  f.src                = VSCP_RS485_ADDR_MASTER;
  f.op                 = VSCP_RS485_OP_EVENT;
  m_tx.emplace_back(f, target);
  return true;
}

size_t
CRs485Master::txQueued(void)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_tx.size();
}

std::vector<uint8_t>
CRs485Master::activeNodes(void)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return std::vector<uint8_t>(m_active.begin(), m_active.end());
}

Rs485MasterStats
CRs485Master::statistics(void)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_stats;
}

void
CRs485Master::setActive(uint8_t address, bool active)
{
  bool changed = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (active && !m_active.count(address)) {
      m_active.insert(address);
      m_stats.discovered++;
      changed = true;
    }
    else if (!active && m_active.count(address)) {
      m_active.erase(address);
      m_stats.lost++;
      changed = true;
    }
  }
  if (changed && m_onNode) {
    m_onNode(address, active);
  }
}

bool
CRs485Master::sendFrame(const vscp_rs485_frame_t &frame)
{
  uint8_t buf[VSCP_RS485_MAX_FRAME];
  const size_t len = vscp_rs485_encode(&m_cfg.codec, &frame, buf);
  if (0 == len) {
    return false;
  }
  if (!m_port.writeFrame(buf, len)) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.portErrors++;
    return false;
  }
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// nextPollAddress
//
// First all configured addresses are probed once (start-up scan). Then the
// active nodes are polled in turn, and after every discoveryRounds rounds
// one address without a node is probed, so that new nodes are found
// without slowing down the polling of the others (as the specification
// suggests). With discoveryRounds = 0 every configured address is polled
// in every round.
//

int
CRs485Master::nextPollAddress(bool &discovery)
{
  discovery              = false;
  const size_t n         = m_cfg.addresses.size();
  if (0 == n) {
    return -1;
  }

  if (0 == m_cfg.discoveryRounds) {
    const uint8_t a = m_cfg.addresses[m_pollIndex % n];
    m_pollIndex     = (m_pollIndex + 1) % n;
    return a;
  }

  if (0 == m_rounds) {
    // Start-up scan
    if (m_pollIndex < n) {
      discovery = true;
      return m_cfg.addresses[m_pollIndex++];
    }
    m_pollIndex = 0;
    m_rounds    = 1;
  }

  std::set<uint8_t> active;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    active = m_active;
  }

  for (int pass = 0; pass < 2; pass++) {
    while (m_pollIndex < n) {
      const uint8_t a = m_cfg.addresses[m_pollIndex++];
      if (active.count(a)) {
        return a;
      }
    }
    // End of a polling round
    m_pollIndex = 0;
    m_rounds++;
    if (0 == (m_rounds % m_cfg.discoveryRounds)) {
      for (size_t k = 0; k < n; k++) {
        const size_t idx = (m_discoveryIndex + k) % n;
        if (!active.count(m_cfg.addresses[idx])) {
          m_discoveryIndex = (idx + 1) % n;
          discovery        = true;
          return m_cfg.addresses[idx];
        }
      }
    }
    if (active.empty()) {
      return -1; // Nothing to poll in this round
    }
  }
  return -1;
}

///////////////////////////////////////////////////////////////////////////////
// poll
//

CRs485Master::PollResult
CRs485Master::poll(uint8_t address)
{
  vscp_rs485_frame_t pollFrame;
  std::memset(&pollFrame, 0, sizeof(pollFrame));
  pollFrame.dest = address;
  pollFrame.src  = VSCP_RS485_ADDR_MASTER;
  pollFrame.op   = VSCP_RS485_OP_POLL;

  // Old characters (echo of earlier frames, noise) are not an answer
  m_port.flushInput();
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.polls++;
  }
  if (!sendFrame(pollFrame)) {
    return PollResult::Garbled;
  }

  vscp_rs485_rx_t rx;
  vscp_rs485_rx_init(&rx, &m_cfg.codec);
  vscp_rs485_frame_t frame;
  bool gotChar     = false;
  uint32_t timeout = m_cfg.pollTimeout;

  for (;;) {
    uint8_t c = 0;
    int mark  = -1;
    const int r = m_port.readChar(c, mark, timeout);
    if (r < 0) {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_stats.portErrors++;
      return PollResult::Garbled;
    }
    if (0 == r) {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (!gotChar) {
        m_stats.timeouts++;
        return PollResult::Timeout;
      }
      m_stats.frameErrors++; // Truncated answer
      return PollResult::Garbled;
    }
    gotChar = true;
    timeout = m_cfg.charTimeout;

    const vscp_rs485_rx_result_t res = vscp_rs485_rx_char(&rx, c, mark, &frame);
    switch (res) {
      case VSCP_RS485_RX_PENDING:
      case VSCP_RS485_RX_SKIPPED:
        continue;

      case VSCP_RS485_RX_ERR_RESYNC: {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stats.frameErrors++;
      }
        continue;

      case VSCP_RS485_RX_ERR_CRC: {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stats.crcErrors++;
      }
        return PollResult::Garbled;

      case VSCP_RS485_RX_ERR_FORMAT: {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stats.frameErrors++;
      }
        return PollResult::Garbled;

      case VSCP_RS485_RX_FRAME:
        break;
    }

    // Our own poll read back (half duplex adapters that echo): keep reading
    if ((VSCP_RS485_ADDR_MASTER == frame.src) && (VSCP_RS485_OP_POLL == frame.op) && (address == frame.dest)) {
      gotChar = false;
      timeout = m_cfg.pollTimeout;
      continue;
    }

    if (((VSCP_RS485_OP_EVENT != frame.op) && (VSCP_RS485_OP_NO_EVENTS != frame.op)) || (frame.src != address)) {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_stats.wrongSender++;
      return PollResult::Garbled;
    }

    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_stats.answers++;
      if (VSCP_RS485_OP_EVENT == frame.op) {
        m_stats.events++;
      }
    }
    if ((VSCP_RS485_OP_EVENT == frame.op) && m_onEvent) {
      // The event may be addressed to another node on the segment: that
      // node receives it directly (it sees its address). The host gets
      // every event.
      m_onEvent(frame);
    }
    return PollResult::Answer;
  }
}

///////////////////////////////////////////////////////////////////////////////
// step
//

bool
CRs485Master::step(void)
{
  // Host events and polls take turns so that neither waits for the other
  std::pair<vscp_rs485_frame_t, int> item;
  bool haveTx = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_lastWasTx && !m_tx.empty()) {
      item   = m_tx.front();
      haveTx = true;
      m_tx.pop_front();
    }
  }

  if (haveTx) {
    m_lastWasTx          = true;
    vscp_rs485_frame_t f = item.first;
    std::vector<uint8_t> targets;
    if (item.second >= 0) {
      targets.push_back((uint8_t) item.second);
    }
    else if (m_cfg.broadcast) {
      targets.push_back(VSCP_RS485_ADDR_BROADCAST);
    }
    else {
      targets = activeNodes();
    }
    for (uint8_t t : targets) {
      f.dest = t;
      if (sendFrame(f)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stats.sent++;
      }
    }
    return true;
  }
  m_lastWasTx = false;

  bool discovery  = false;
  const int a     = nextPollAddress(discovery);
  if (a < 0) {
    // Nothing to poll now; send waiting host events if any
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_tx.empty();
  }

  const uint8_t address = (uint8_t) a;
  switch (poll(address)) {
    case PollResult::Answer:
      m_miss[address] = 0;
      setActive(address, true);
      break;

    case PollResult::Timeout:
    case PollResult::Garbled:
      if (m_miss[address] < 255) {
        m_miss[address]++;
      }
      if (m_miss[address] >= m_cfg.missLimit) {
        setActive(address, false);
      }
      break;
  }
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// parseAddresses
//

bool
CRs485Master::parseAddresses(const std::string &str, std::vector<uint8_t> &out)
{
  out.clear();
  std::string part;
  std::istringstream in(str);
  while (std::getline(in, part, ',')) {
    size_t b = part.find_first_not_of(" \t");
    size_t e = part.find_last_not_of(" \t");
    if (std::string::npos == b) {
      return false;
    }
    part           = part.substr(b, e - b + 1);
    const size_t d = part.find('-');
    uint32_t from = 0, to = 0;
    if (std::string::npos == d) {
      if (!parseNumber(part, 254, from)) {
        return false;
      }
      to = from;
    }
    else if (!parseNumber(part.substr(0, d), 254, from) || !parseNumber(part.substr(d + 1), 254, to)) {
      return false;
    }
    if ((from < 1) || (to < from)) {
      return false;
    }
    for (uint32_t a = from; a <= to; a++) {
      out.push_back((uint8_t) a);
    }
  }
  return !out.empty();
}

///////////////////////////////////////////////////////////////////////////////
// targetOf
//
// VSCP events are for everybody. On RS-485 a frame has one destination, so
// an event from the host is sent to every active node - except the Level I
// protocol events that name the node they are for in data byte 0 (register
// read/write, nickname, boot loader ...), which go to that node only.
//

int
CRs485Master::targetOf(uint16_t vscpClass, uint8_t vscpType, const uint8_t *data, uint8_t size)
{
  if ((0 != vscpClass) || (0 == size)) {
    return -1;
  }
  switch (vscpType) {
    case 6:  // SET_NICKNAME (old nickname)
    case 8:  // DROP_NICKNAME
    case 9:  // READ_REGISTER
    case 11: // WRITE_REGISTER
    case 12: // ENTER_BOOT_LOADER
    case 24: // PAGE_READ
    case 25: // PAGE_WRITE
    case 29: // INCREMENT_REGISTER
    case 30: // DECREMENT_REGISTER
    case 31: // WHO_IS_THERE (0xFF = all nodes)
    case 33: // GET_MATRIX_INFO
    case 35: // GET_EMBEDDED_MDF
    case 37: // EXTENDED_PAGE_READ
    case 38: // EXTENDED_PAGE_WRITE
    case 40: // GET_EVENT_INTEREST
      if ((data[0] >= VSCP_RS485_ADDR_FIRST) && (data[0] <= VSCP_RS485_ADDR_LAST)) {
        return data[0];
      }
      return -1;
    default:
      return -1;
  }
}
