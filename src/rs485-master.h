// rs485-master.h
//
// The RS-485 bus master (server) of "VSCP Level I over RS-485/RS-422":
// it polls the nodes for events, finds new nodes, notices nodes that are
// gone, and sends events from the host to the nodes.
//
// Independent of the operating system and of CANAL/VSCP Level II: the
// serial port is an IRs485Port, received events are given to a callback.
//
// This file is part of the VSCP (https://www.vscp.org)
// SPDX-License-Identifier: MIT
// Copyright (C) 2000-2026 Ake Hedman, Grodans Paradis AB <info@grodansparadis.com>
//

#ifndef RS485_MASTER_H
#define RS485_MASTER_H

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "vscp-rs485.h"

/// The serial port as the master sees it
class IRs485Port {
public:
  virtual ~IRs485Port() {}

  /*!
    Send one frame. data[0] is the address character (ninth bit set), the
    rest are sent with the ninth bit clear. Returns when the frame has been
    sent (the line is free for the answer).
  */
  virtual bool writeFrame(const uint8_t *data, size_t len) = 0;

  /*!
    Read one character.
    @param mark Set to 1/0 for a character with the ninth bit set/clear, -1
           when the port can not tell.
    @return 1 a character, 0 time-out, -1 port error
  */
  virtual int readChar(uint8_t &c, int &mark, uint32_t timeout_ms) = 0;

  /// Discard everything received so far
  virtual void flushInput(void) = 0;
};

/// Master configuration
struct Rs485MasterConfig {
  std::vector<uint8_t> addresses;  ///< Node addresses on the segment (1-254)
  uint32_t pollTimeout      = 20;  ///< ms to wait for the first character of an answer
  uint32_t charTimeout      = 5;   ///< ms to wait between characters of a frame
  uint32_t discoveryRounds  = 1;   ///< Probe one unused address after this many polling rounds, 0 = never
  uint32_t missLimit        = 3;   ///< Missed polls before a node counts as gone
  bool broadcast            = false; ///< Send host events to address 0xFF instead of to each node
  vscp_rs485_options_t codec = { VSCP_RS485_CRC_DOW, 0 };
  size_t txQueueSize        = 1000;
};

/// Bus statistics
struct Rs485MasterStats {
  uint64_t polls       = 0;
  uint64_t answers     = 0; ///< EVENT or NO_EVENTS answers to polls
  uint64_t events      = 0; ///< Events received from nodes
  uint64_t sent        = 0; ///< Frames sent for the host
  uint64_t timeouts    = 0; ///< Polls without an answer
  uint64_t crcErrors   = 0;
  uint64_t frameErrors = 0; ///< Format errors, truncated frames, resyncs
  uint64_t wrongSender = 0; ///< Answers from another address than the polled one
  uint64_t portErrors  = 0;
  uint64_t discovered  = 0; ///< Nodes that became active
  uint64_t lost        = 0; ///< Nodes that went away
};

class CRs485Master {
public:
  /// Called (from step()) for every event received from a node
  typedef std::function<void(const vscp_rs485_frame_t &frame)> EventCallback;
  /// Called when a node becomes active (true) or is lost (false)
  typedef std::function<void(uint8_t address, bool active)> NodeCallback;

  CRs485Master(IRs485Port &port, const Rs485MasterConfig &cfg, EventCallback onEvent, NodeCallback onNode = nullptr);

  /*!
    Queue an event for the bus.
    @param frame The event (op, class, type, size, data; src is set to the
           master address)
    @param target A node address, or -1 for every active node (or the
           broadcast address with the broadcast option)
    @return false when the queue is full
  */
  bool queueEvent(const vscp_rs485_frame_t &frame, int target);

  /*!
    Do one bus transaction: send a queued host event, or poll the next node
    (which may give an event), or probe an unused address. Blocks at most
    about pollTimeout + 16 * charTimeout ms.
    @return true if something was done on the bus, false when idle (no
            nodes and no discovery; the caller should sleep)
  */
  bool step(void);

  /// Addresses of the nodes that answer polls
  std::vector<uint8_t> activeNodes(void);

  Rs485MasterStats statistics(void);

  size_t txQueued(void);

  /// Parse an address list such as "1-32,40,50-52" (addresses 1-254)
  static bool parseAddresses(const std::string &str, std::vector<uint8_t> &out);

  /*!
    Which node(s) an event from the host is for: the address a Level I
    protocol event names in data byte 0 (register read/write, nickname,
    boot loader ...), or -1 for every node.
  */
  static int targetOf(uint16_t vscpClass, uint8_t vscpType, const uint8_t *data, uint8_t size);

private:
  enum class PollResult
  {
    Answer,  ///< EVENT or NO_EVENTS from the polled node
    Timeout, ///< Nothing
    Garbled  ///< Something, but not a valid answer from it
  };

  PollResult poll(uint8_t address);
  bool sendFrame(const vscp_rs485_frame_t &frame);
  int nextPollAddress(bool &discovery);
  void setActive(uint8_t address, bool active);

  IRs485Port &m_port;
  Rs485MasterConfig m_cfg;
  EventCallback m_onEvent;
  NodeCallback m_onNode;

  std::mutex m_mutex; // m_tx, m_active, m_stats (step() itself runs in one thread)
  std::deque<std::pair<vscp_rs485_frame_t, int>> m_tx;
  std::set<uint8_t> m_active;
  Rs485MasterStats m_stats;

  // Polling state (only used by step())
  std::vector<uint8_t> m_miss;      // Missed polls per address (index = address)
  size_t m_pollIndex      = 0;      // Next index in the configured address list
  size_t m_discoveryIndex = 0;      // Next index for discovery probes
  uint32_t m_rounds       = 0;      // Completed polling rounds
  bool m_lastWasTx        = false;  // Alternate host events and polls
};

#endif
