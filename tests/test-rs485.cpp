// test-rs485.cpp
//
// Tests of the RS-485 protocol (golden vectors), the receiver and the bus
// master against a simulated segment. No hardware needed.
//
// The golden vectors were computed with an implementation independent of
// src/vscp-rs485.c (a Python script, see docs/testing.md). The CRC-8/MAXIM-
// DOW check value of "123456789" is 0xA1, of CRC-8 (0x07) 0xF4.
//
// This file is part of the VSCP (https://www.vscp.org)
// SPDX-License-Identifier: MIT
// Copyright (C) 2000-2026 Ake Hedman, Grodans Paradis AB <info@grodansparadis.com>
//

#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "rs485-driver.h"
#include "rs485-master.h"
#include "rs485-port.h"
#include "vscp-rs485.h"

static int g_failed = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                                                    \
  do {                                                                                                                 \
    g_checks++;                                                                                                        \
    if (!(cond)) {                                                                                                     \
      g_failed++;                                                                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                      \
    }                                                                                                                  \
  } while (0)

typedef std::vector<uint8_t> bytes;

static bytes
hex(const char *s)
{
  bytes out;
  for (; s[0] && s[1]; s += 2) {
    unsigned v = 0;
    std::sscanf(s, "%2x", &v);
    out.push_back((uint8_t) v);
  }
  return out;
}

static const vscp_rs485_options_t DOW   = { VSCP_RS485_CRC_DOW, 0 };
static const vscp_rs485_options_t FIXED = { VSCP_RS485_CRC_DOW, 1 };
static const vscp_rs485_options_t ATM   = { VSCP_RS485_CRC_ATM, 0 };

static vscp_rs485_frame_t
mk(uint8_t dest, uint8_t src, uint8_t op, uint16_t cls = 0, uint8_t type = 0, bytes data = {})
{
  vscp_rs485_frame_t f;
  std::memset(&f, 0, sizeof(f));
  f.dest       = dest;
  f.src        = src;
  f.op         = op;
  f.vscp_class = cls;
  f.vscp_type  = type;
  f.size       = (uint8_t) data.size();
  if (!data.empty()) {
    std::memcpy(f.data, data.data(), data.size());
  }
  return f;
}

static bytes
enc(const vscp_rs485_options_t &opt, const vscp_rs485_frame_t &f)
{
  uint8_t buf[VSCP_RS485_MAX_FRAME];
  size_t n = vscp_rs485_encode(&opt, &f, buf);
  return bytes(buf, buf + n);
}

// Feed characters (first one marked) and return the result of the last
static vscp_rs485_rx_result_t
feed(vscp_rs485_rx_t &rx, const bytes &b, vscp_rs485_frame_t &out, bool marks = true)
{
  vscp_rs485_rx_result_t r = VSCP_RS485_RX_PENDING;
  for (size_t i = 0; i < b.size(); i++) {
    r = vscp_rs485_rx_char(&rx, b[i], marks ? (0 == i ? 1 : 0) : -1, &out);
  }
  return r;
}

///////////////////////////////////////////////////////////////////////////////
// Protocol
//

static void
testProtocol(void)
{
  const uint8_t check[] = "123456789";
  CHECK(0xA1 == vscp_rs485_crc(VSCP_RS485_CRC_DOW, 0, check, 9));
  CHECK(0xF4 == vscp_rs485_crc(VSCP_RS485_CRC_ATM, 0, check, 9));

  // Golden vectors
  CHECK(enc(DOW, mk(5, 0, VSCP_RS485_OP_POLL)) == hex("05000289"));
  CHECK(enc(DOW, mk(0, 5, VSCP_RS485_OP_NO_EVENTS)) == hex("0005031d"));
  CHECK(enc(DOW, mk(0, 5, VSCP_RS485_OP_EVENT, 10, 6)) == hex("000501000a0667"));
  CHECK(enc(DOW, mk(0, 5, VSCP_RS485_OP_EVENT, 10, 6, { 0x42 })) == hex("000501010a064293"));
  CHECK(enc(DOW, mk(3, 7, VSCP_RS485_OP_EVENT, 0x1FF, 0xFF, { 1, 2, 3, 4, 5, 6, 7, 8 })) ==
        hex("03070118ffff0102030405060708a1"));
  CHECK(enc(FIXED, mk(0, 5, VSCP_RS485_OP_EVENT, 20, 3, { 1, 2 })) == hex("000501021403010200000000000041"));
  CHECK(enc(ATM, mk(5, 0, VSCP_RS485_OP_POLL)) == hex("050002ce"));

  // Invalid frames are not encoded
  uint8_t buf[VSCP_RS485_MAX_FRAME];
  vscp_rs485_frame_t bad = mk(0, 5, VSCP_RS485_OP_EVENT, 512, 0);
  CHECK(0 == vscp_rs485_encode(&DOW, &bad, buf));
  bad      = mk(0, 5, VSCP_RS485_OP_EVENT, 10, 6);
  bad.size = 9;
  CHECK(0 == vscp_rs485_encode(&DOW, &bad, buf));
  bad = mk(0, 5, 7);
  CHECK(0 == vscp_rs485_encode(&DOW, &bad, buf));

  // Decoding the vectors
  vscp_rs485_rx_t rx;
  vscp_rs485_frame_t f;
  vscp_rs485_rx_init(&rx, &DOW);
  CHECK(VSCP_RS485_RX_FRAME == feed(rx, hex("03070118ffff0102030405060708a1"), f));
  CHECK((3 == f.dest) && (7 == f.src) && (VSCP_RS485_OP_EVENT == f.op) && (0x1FF == f.vscp_class) &&
        (0xFF == f.vscp_type) && (8 == f.size) && (8 == f.data[7]));
  CHECK(VSCP_RS485_RX_FRAME == feed(rx, hex("0005031d"), f));
  CHECK((VSCP_RS485_OP_NO_EVENTS == f.op) && (5 == f.src));
  CHECK(VSCP_RS485_RX_FRAME == feed(rx, hex("000501000a0667"), f));
  CHECK((0 == f.size) && (10 == f.vscp_class));

  vscp_rs485_rx_init(&rx, &FIXED);
  CHECK(VSCP_RS485_RX_FRAME == feed(rx, hex("000501021403010200000000000041"), f));
  CHECK((2 == f.size) && (20 == f.vscp_class) && (2 == f.data[1]));

  // Without marks (8N1, Windows): frames are found from their length
  vscp_rs485_rx_init(&rx, &DOW);
  bytes two = hex("0005031d");
  bytes ev  = hex("000501010a064293");
  two.insert(two.end(), ev.begin(), ev.end());
  int frames = 0;
  for (uint8_t c : two) {
    frames += (VSCP_RS485_RX_FRAME == vscp_rs485_rx_char(&rx, c, -1, &f));
  }
  CHECK((2 == frames) && (0x42 == f.data[0]));

  // Every single bit error in a frame is found
  const bytes good = hex("03070118ffff0102030405060708a1");
  int found        = 0, total = 0;
  for (size_t i = 0; i < good.size(); i++) {
    for (int bit = 0; bit < 8; bit++) {
      bytes b = good;
      b[i] ^= (uint8_t) (1 << bit);
      vscp_rs485_rx_init(&rx, &DOW);
      vscp_rs485_rx_result_t r = VSCP_RS485_RX_PENDING;
      for (size_t k = 0; (k < b.size()) && (VSCP_RS485_RX_PENDING == r); k++) {
        r = vscp_rs485_rx_char(&rx, b[k], 0 == k ? 1 : 0, &f);
      }
      total++;
      found += (VSCP_RS485_RX_FRAME != r);
    }
  }
  CHECK(found == total);

  // Format errors
  vscp_rs485_rx_init(&rx, &DOW);
  CHECK(VSCP_RS485_RX_ERR_FORMAT == feed(rx, hex("000509"), f));           // Unknown operation
  CHECK(VSCP_RS485_RX_ERR_FORMAT == feed(rx, hex("00050120"), f));         // Reserved flag bit
  CHECK(VSCP_RS485_RX_ERR_FORMAT == feed(rx, hex("00050109"), f));         // Nine data bytes
  CHECK(VSCP_RS485_RX_ERR_CRC == feed(rx, hex("0005031e"), f));            // CRC

  // An address character inside a frame starts a new one
  vscp_rs485_rx_init(&rx, &DOW);
  CHECK(VSCP_RS485_RX_PENDING == vscp_rs485_rx_char(&rx, 0x00, 1, &f));
  CHECK(VSCP_RS485_RX_PENDING == vscp_rs485_rx_char(&rx, 0x05, 0, &f));
  CHECK(VSCP_RS485_RX_ERR_RESYNC == vscp_rs485_rx_char(&rx, 0x00, 1, &f));
  vscp_rs485_rx_init(&rx, &DOW);
  vscp_rs485_rx_char(&rx, 0x00, 1, &f);
  vscp_rs485_rx_char(&rx, 0x05, 0, &f);
  vscp_rs485_rx_char(&rx, 0x00, 1, &f); // Resync: new frame starts with 0x00
  vscp_rs485_rx_char(&rx, 0x05, 0, &f);
  vscp_rs485_rx_char(&rx, 0x03, 0, &f);
  CHECK(VSCP_RS485_RX_FRAME == vscp_rs485_rx_char(&rx, 0x1d, 0, &f));
  // Data characters outside a frame are skipped
  CHECK(VSCP_RS485_RX_SKIPPED == vscp_rs485_rx_char(&rx, 0x55, 0, &f));
}

///////////////////////////////////////////////////////////////////////////////
// Simulated segment
//

class SimBus : public IRs485Port {
public:
  struct Node {
    bool present = true;
    std::deque<vscp_rs485_frame_t> events;
    enum { Normal, BadCrc, Truncated, WrongSource, Silent } mode = Normal;
    int polls = 0;
  };

  vscp_rs485_options_t opt = DOW;
  bool echo                = false; // Half duplex adapter that reads back what it sends
  bool marks               = true;  // The port tells the ninth bit
  std::map<uint8_t, Node> nodes;
  std::vector<vscp_rs485_frame_t> sent; // Events the master sent
  std::vector<uint8_t> pollOrder;

  bool writeFrame(const uint8_t *data, size_t len) override
  {
    if (echo) {
      for (size_t i = 0; i < len; i++) {
        m_rx.push_back({ data[i], marks ? (0 == i ? 1 : 0) : -1 });
      }
    }
    vscp_rs485_rx_t rx;
    vscp_rs485_rx_init(&rx, &opt);
    vscp_rs485_frame_t f;
    vscp_rs485_rx_result_t r = VSCP_RS485_RX_PENDING;
    for (size_t i = 0; i < len; i++) {
      r = vscp_rs485_rx_char(&rx, data[i], 0 == i ? 1 : 0, &f);
    }
    if (VSCP_RS485_RX_FRAME != r) {
      return false;
    }
    if (VSCP_RS485_OP_EVENT == f.op) {
      sent.push_back(f);
      return true;
    }
    if (VSCP_RS485_OP_POLL != f.op) {
      return true;
    }
    pollOrder.push_back(f.dest);
    auto it = nodes.find(f.dest);
    if ((it == nodes.end()) || !it->second.present) {
      return true; // Nobody answers
    }
    Node &n = it->second;
    n.polls++;
    if (Node::Silent == n.mode) {
      return true;
    }
    vscp_rs485_frame_t answer = mk(0, f.dest, VSCP_RS485_OP_NO_EVENTS);
    if (!n.events.empty()) {
      answer = n.events.front();
      n.events.pop_front();
    }
    if (Node::WrongSource == n.mode) {
      answer.src = (uint8_t) (f.dest + 1);
    }
    bytes b = enc(opt, answer);
    if (Node::BadCrc == n.mode) {
      b.back() ^= 0x01;
    }
    if (Node::Truncated == n.mode) {
      b.pop_back();
    }
    for (size_t i = 0; i < b.size(); i++) {
      m_rx.push_back({ b[i], marks ? (0 == i ? 1 : 0) : -1 });
    }
    return true;
  }

  int readChar(uint8_t &c, int &mark, uint32_t) override
  {
    if (m_rx.empty()) {
      return 0; // Time-out (no real waiting in the simulation)
    }
    c    = m_rx.front().first;
    mark = m_rx.front().second;
    m_rx.pop_front();
    return 1;
  }

  void flushInput(void) override { m_rx.clear(); }

private:
  std::deque<std::pair<uint8_t, int>> m_rx;
};

static Rs485MasterConfig
config(std::vector<uint8_t> addresses)
{
  Rs485MasterConfig cfg;
  cfg.addresses       = addresses;
  cfg.discoveryRounds = 1;
  cfg.missLimit       = 3;
  return cfg;
}

static std::vector<uint8_t>
range(uint8_t from, uint8_t to)
{
  std::vector<uint8_t> v;
  for (unsigned a = from; a <= to; a++) {
    v.push_back((uint8_t) a);
  }
  return v;
}

static void
run(CRs485Master &m, int steps)
{
  for (int i = 0; i < steps; i++) {
    m.step();
  }
}

static void
testMaster(void)
{
  // --- Start-up scan and polling ----------------------------------------------
  {
    SimBus bus;
    bus.nodes[3];
    bus.nodes[7];
    std::vector<vscp_rs485_frame_t> got;
    std::vector<std::pair<uint8_t, bool>> nodeChanges;
    CRs485Master m(
      bus, config(range(1, 10)), [&](const vscp_rs485_frame_t &f) { got.push_back(f); },
      [&](uint8_t a, bool on) { nodeChanges.push_back({ a, on }); });

    run(m, 10); // Start-up scan: every address once
    CHECK(bus.pollOrder == range(1, 10));
    CHECK(m.activeNodes() == std::vector<uint8_t>({ 3, 7 }));
    CHECK((2 == nodeChanges.size()) && nodeChanges[0].second);

    // Then the active nodes in turn and one probe per round
    bus.pollOrder.clear();
    run(m, 9);
    CHECK(bus.pollOrder.size() == 9);
    int probes = 0;
    for (uint8_t a : bus.pollOrder) {
      probes += (3 != a) && (7 != a);
    }
    CHECK(3 == probes); // One per round of two polls

    // Events from nodes reach the host, also those for another node
    bus.nodes[7].events.push_back(mk(0, 7, VSCP_RS485_OP_EVENT, 10, 6, { 1, 2 }));
    bus.nodes[3].events.push_back(mk(7, 3, VSCP_RS485_OP_EVENT, 20, 3, { 9 }));
    run(m, 6);
    CHECK(2 == got.size());
    bool fromSeven = false, fromThreeToSeven = false;
    for (const auto &f : got) {
      fromSeven |= (7 == f.src) && (10 == f.vscp_class) && (2 == f.size);
      fromThreeToSeven |= (3 == f.src) && (7 == f.dest) && (20 == f.vscp_class);
    }
    CHECK(fromSeven && fromThreeToSeven);

    // A node that goes away is lost after three missed polls, and found
    // again by the discovery probes when it comes back
    bus.nodes[7].present = false;
    run(m, 12);
    CHECK(m.activeNodes() == std::vector<uint8_t>({ 3 }));
    CHECK((3 == nodeChanges.size()) && (7 == nodeChanges[2].first) && !nodeChanges[2].second);
    bus.nodes[7].present = true;
    run(m, 30);
    CHECK(m.activeNodes() == std::vector<uint8_t>({ 3, 7 }));

    // A node that appears later is found
    bus.nodes[9];
    run(m, 30);
    CHECK(m.activeNodes() == std::vector<uint8_t>({ 3, 7, 9 }));

    const Rs485MasterStats st = m.statistics();
    CHECK(st.events == 2);
    CHECK(st.discovered == 4);
    CHECK(st.lost == 1);
    CHECK(st.crcErrors == 0);
  }

  // --- Host events --------------------------------------------------------------
  {
    SimBus bus;
    bus.nodes[2];
    bus.nodes[4];
    CRs485Master m(bus, config(range(1, 5)), nullptr);
    run(m, 5);
    CHECK(m.activeNodes() == std::vector<uint8_t>({ 2, 4 }));

    vscp_rs485_frame_t ev = mk(0, 99, VSCP_RS485_OP_EVENT, 20, 9, { 1, 2, 3 });
    CHECK(m.queueEvent(ev, -1)); // All nodes
    CHECK(m.queueEvent(ev, 4));  // Node 4
    run(m, 4);                   // Host events and polls take turns
    CHECK(3 == bus.sent.size());
    CHECK((2 == bus.sent[0].dest) && (4 == bus.sent[1].dest) && (4 == bus.sent[2].dest));
    CHECK(0 == bus.sent[0].src); // From the master
    CHECK((3 == bus.sent[2].size) && (3 == bus.sent[2].data[2]));
    CHECK(0 == m.txQueued());

    // Broadcast address instead
    Rs485MasterConfig cfg = config(range(1, 5));
    cfg.broadcast         = true;
    SimBus bus2;
    bus2.nodes[2];
    CRs485Master m2(bus2, cfg, nullptr);
    run(m2, 5);
    m2.queueEvent(ev, -1);
    run(m2, 2);
    CHECK((1 == bus2.sent.size()) && (0xFF == bus2.sent[0].dest));

    // A full transmit queue is reported
    cfg.txQueueSize = 2;
    SimBus bus3;
    CRs485Master m3(bus3, cfg, nullptr);
    CHECK(m3.queueEvent(ev, -1) && m3.queueEvent(ev, -1));
    CHECK(!m3.queueEvent(ev, -1));
  }

  // --- Damaged answers ----------------------------------------------------------
  {
    SimBus bus;
    bus.nodes[1].mode = SimBus::Node::BadCrc;
    bus.nodes[2].mode = SimBus::Node::Truncated;
    bus.nodes[3].mode = SimBus::Node::WrongSource;
    bus.nodes[4];
    bus.nodes[1].events.push_back(mk(0, 1, VSCP_RS485_OP_EVENT, 10, 6, { 1 }));
    std::vector<vscp_rs485_frame_t> got;
    CRs485Master m(bus, config(range(1, 4)), [&](const vscp_rs485_frame_t &f) { got.push_back(f); });
    run(m, 4);
    CHECK(m.activeNodes() == std::vector<uint8_t>({ 4 })); // The others never answered correctly
    CHECK(got.empty());
    const Rs485MasterStats st = m.statistics();
    CHECK(st.crcErrors == 1);
    CHECK(st.frameErrors == 1);
    CHECK(st.wrongSender == 1);
  }

  // --- Echo of the own transmission, and ports without ninth bit ---------------
  for (int marks = 0; marks < 2; marks++) {
    SimBus bus;
    bus.echo  = true;
    bus.marks = (1 == marks);
    bus.nodes[6].events.push_back(mk(0, 6, VSCP_RS485_OP_EVENT, 10, 6, { 5 }));
    std::vector<vscp_rs485_frame_t> got;
    CRs485Master m(bus, config(range(5, 7)), [&](const vscp_rs485_frame_t &f) { got.push_back(f); });
    run(m, 3);
    CHECK(m.activeNodes() == std::vector<uint8_t>({ 6 }));
    CHECK((1 == got.size()) && (5 == got[0].data[0]));
    CHECK(0 == m.statistics().wrongSender);
  }

  // --- No discovery: every configured address in every round --------------------
  {
    SimBus bus;
    bus.nodes[2];
    Rs485MasterConfig cfg = config(range(1, 3));
    cfg.discoveryRounds   = 0;
    CRs485Master m(bus, cfg, nullptr);
    run(m, 6);
    CHECK(bus.pollOrder == std::vector<uint8_t>({ 1, 2, 3, 1, 2, 3 }));
  }

  // --- Nothing to poll ------------------------------------------------------------
  {
    SimBus bus;
    CRs485Master m(bus, config({ 0, 255 }), nullptr); // Invalid addresses dropped
    CHECK(!m.step());
  }
}

///////////////////////////////////////////////////////////////////////////////
// Driver helpers
//

static void
testDriver(void)
{
  std::vector<uint8_t> a;
  CHECK(CRs485Driver::parseAddresses("1-3,7, 9-10", a));
  CHECK(a == std::vector<uint8_t>({ 1, 2, 3, 7, 9, 10 }));
  CHECK(CRs485Driver::parseAddresses("0x10", a) && (a == std::vector<uint8_t>({ 16 })));
  CHECK(!CRs485Driver::parseAddresses("0-3", a));
  CHECK(!CRs485Driver::parseAddresses("5-3", a));
  CHECK(!CRs485Driver::parseAddresses("1-255", a));
  CHECK(!CRs485Driver::parseAddresses("1,,2", a));
  CHECK(!CRs485Driver::parseAddresses("x", a));

  const uint8_t node9[] = { 9, 0xD0 };
  const uint8_t all[]   = { 0xFF };
  CHECK(9 == CRs485Driver::targetOf(0, 9, node9, 2));   // READ_REGISTER
  CHECK(9 == CRs485Driver::targetOf(0, 11, node9, 2));  // WRITE_REGISTER
  CHECK(9 == CRs485Driver::targetOf(0, 37, node9, 2));  // EXTENDED_PAGE_READ
  CHECK(-1 == CRs485Driver::targetOf(0, 31, all, 1));   // WHO_IS_THERE all
  CHECK(-1 == CRs485Driver::targetOf(0, 2, node9, 2));  // NEW_NODE_ONLINE: everybody
  CHECK(-1 == CRs485Driver::targetOf(20, 9, node9, 2)); // Not CLASS1.PROTOCOL
  CHECK(-1 == CRs485Driver::targetOf(0, 9, node9, 0));  // No data
}

///////////////////////////////////////////////////////////////////////////////
// PARMRK decoding (Linux 9-bit receive)
//

static void
testParmrk(void)
{
  // POLL answer NO_EVENTS from node 5: address character 0x00 with the
  // ninth bit (parity error, 0xFF 0x00 0x00), then 05 03 1d; and a data
  // character 0xFF (doubled)
  const bytes raw = hex("ff000005031dff");
  std::deque<std::pair<uint8_t, int>> out;
  int state = 0;
  // Split across two reads in the middle of a mark sequence
  CRs485Port::decodeParmrk(state, raw.data(), 2, out);
  CRs485Port::decodeParmrk(state, raw.data() + 2, raw.size() - 2, out);
  CHECK(1 == state); // The last 0xFF waits for its pair
  const uint8_t ff = 0xFF;
  CRs485Port::decodeParmrk(state, &ff, 1, out);
  CHECK(0 == state);
  CHECK(5 == out.size());
  CHECK((0x00 == out[0].first) && (1 == out[0].second));
  CHECK((0x05 == out[1].first) && (0 == out[1].second));
  CHECK((0x1d == out[3].first) && (0 == out[3].second));
  CHECK((0xFF == out[4].first) && (0 == out[4].second));

  // The decoded characters make a frame
  vscp_rs485_rx_t rx;
  vscp_rs485_rx_init(&rx, &DOW);
  vscp_rs485_frame_t f;
  vscp_rs485_rx_result_t r = VSCP_RS485_RX_PENDING;
  for (int i = 0; i < 4; i++) {
    r = vscp_rs485_rx_char(&rx, out[i].first, out[i].second, &f);
  }
  CHECK((VSCP_RS485_RX_FRAME == r) && (VSCP_RS485_OP_NO_EVENTS == f.op) && (5 == f.src));
}

int
main(void)
{
  testParmrk();
  testProtocol();
  testMaster();
  testDriver();
  std::printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
