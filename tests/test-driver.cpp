// test-driver.cpp
//
// Tests the built driver the way a host uses it: the shared library is
// loaded at run time and the CANAL functions are looked up by name. On
// Linux and macOS the driver runs a real bus against simulated nodes at
// the other end of a pseudo terminal (in 8N1 mode: a pseudo terminal has
// no parity bit).
//
//   test-driver path/to/vscpl1drv-rs485.so
//
// Run by ctest. Exit status 0 when all checks pass.
//
// This file is part of the VSCP (https://www.vscp.org)
// SPDX-License-Identifier: MIT
// Copyright (C) 2000-2026 Ake Hedman, Grodans Paradis AB <info@grodansparadis.com>
//

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#define CALLCONV __stdcall
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>
#define CALLCONV
#endif

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <vector>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include <canal.h>

#include "vscp-rs485.h"

// The CANAL functions as the host sees them
typedef long(CALLCONV *fnOpen)(const char *, unsigned long);
typedef int(CALLCONV *fnClose)(long);
typedef unsigned long(CALLCONV *fnGetLevel)(long);
typedef int(CALLCONV *fnSend)(long, PCANALMSG);
typedef int(CALLCONV *fnBlockingSend)(long, PCANALMSG, unsigned long);
typedef int(CALLCONV *fnReceive)(long, PCANALMSG);
typedef int(CALLCONV *fnBlockingReceive)(long, PCANALMSG, unsigned long);
typedef int(CALLCONV *fnDataAvailable)(long);
typedef int(CALLCONV *fnGetStatus)(long, PCANALSTATUS);
typedef int(CALLCONV *fnGetStatistics)(long, PCANALSTATISTICS);
typedef int(CALLCONV *fnSetUlong)(long, unsigned long);
typedef unsigned long(CALLCONV *fnGetUlong)(void);
typedef const char *(CALLCONV *fnGetString)(void);
typedef int(CALLCONV *fnSendFD)(long, PCANALMSGFD);
typedef int(CALLCONV *fnReceiveFD)(long, PCANALMSGFD);
typedef int(CALLCONV *fnBlockingReceiveFD)(long, PCANALMSGFD, unsigned long);

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

struct Api {
  fnOpen open;
  fnClose close;
  fnGetLevel getLevel;
  fnSend send;
  fnBlockingSend blockingSend;
  fnReceive receive;
  fnBlockingReceive blockingReceive;
  fnDataAvailable dataAvailable;
  fnGetStatus getStatus;
  fnGetStatistics getStatistics;
  fnSetUlong setFilter;
  fnSetUlong setMask;
  fnSetUlong setBaudrate;
  fnGetUlong getVersion;
  fnGetUlong getDllVersion;
  fnGetString getVendor;
  fnGetString getDriverInfo;
  fnSendFD sendFD;
  fnReceiveFD receiveFD;
  fnBlockingReceiveFD blockingReceiveFD;
};

#ifdef _WIN32
static HMODULE g_lib;
static void *
sym(const char *name)
{
  return (void *) GetProcAddress(g_lib, name);
}
#else
static void *g_lib;
static void *
sym(const char *name)
{
  return dlsym(g_lib, name);
}
#endif

template<typename T>
static bool
load(T &fn, const char *name)
{
  fn = (T) sym(name);
  if (nullptr == fn) {
    std::printf("FAIL: the driver does not export %s\n", name);
    g_failed++;
    return false;
  }
  return true;
}

// A VSCP Level I event as a CANAL frame: n data bytes with value first, first+1 ...
static canalMsg
vscpFrame(unsigned cls, unsigned type, unsigned char n, unsigned char first)
{
  canalMsg m;
  std::memset(&m, 0, sizeof(m));
  m.flags    = CANAL_IDFLAG_EXTENDED;
  m.id       = (3UL << 26) | ((unsigned long) cls << 16) | ((unsigned long) type << 8);
  m.sizeData = n;
  for (unsigned char i = 0; i < n; i++) {
    m.data[i] = (unsigned char) (first + i);
  }
  return m;
}

#ifndef _WIN32
// The nodes of an RS-485 segment, at the other end of a pseudo terminal.
// Nodes 5 and 9 are there; they answer polls with a queued event or with
// NO_EVENTS and record the events the master sends.
class SimSegment {
public:
  bool start(void)
  {
    m_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if ((m_fd < 0) || grantpt(m_fd) || unlockpt(m_fd)) {
      return false;
    }
    m_slave = ptsname(m_fd);
    m_run   = true;
    m_thread = std::thread(&SimSegment::run, this);
    return true;
  }
  void stop(void)
  {
    m_run = false;
    if (m_thread.joinable()) {
      m_thread.join();
    }
    if (m_fd >= 0) {
      ::close(m_fd);
    }
  }
  std::string slavePath(void) const { return m_slave; }
  void queueEvent(uint8_t node, uint16_t cls, uint8_t type, std::vector<uint8_t> data)
  {
    vscp_rs485_frame_t f;
    std::memset(&f, 0, sizeof(f));
    f.dest       = 0;
    f.src        = node;
    f.op         = VSCP_RS485_OP_EVENT;
    f.vscp_class = cls;
    f.vscp_type  = type;
    f.size       = (uint8_t) data.size();
    if (!data.empty()) {
      std::memcpy(f.data, data.data(), data.size());
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_events[node].push_back(f);
  }
  int pollsOf(uint8_t node)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_polls[node];
  }
  std::vector<vscp_rs485_frame_t> received(void)
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_received;
  }

private:
  void run(void)
  {
    const vscp_rs485_options_t opt = { VSCP_RS485_CRC_DOW, 0 };
    vscp_rs485_rx_t rx;
    vscp_rs485_rx_init(&rx, &opt);
    while (m_run) {
      struct pollfd pfd = { m_fd, POLLIN, 0 };
      if (::poll(&pfd, 1, 20) <= 0) {
        vscp_rs485_rx_reset(&rx); // Gap: the next character starts a frame
        continue;
      }
      uint8_t buf[64];
      const ssize_t n = ::read(m_fd, buf, sizeof(buf));
      for (ssize_t i = 0; i < n; i++) {
        vscp_rs485_frame_t f;
        if (VSCP_RS485_RX_FRAME != vscp_rs485_rx_char(&rx, buf[i], -1, &f)) {
          continue;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        if (VSCP_RS485_OP_EVENT == f.op) {
          m_received.push_back(f);
          continue;
        }
        if ((VSCP_RS485_OP_POLL != f.op) || ((5 != f.dest) && (9 != f.dest))) {
          continue; // Nobody there
        }
        m_polls[f.dest]++;
        vscp_rs485_frame_t answer;
        std::memset(&answer, 0, sizeof(answer));
        if (!m_events[f.dest].empty()) {
          answer = m_events[f.dest].front();
          m_events[f.dest].erase(m_events[f.dest].begin());
        }
        else {
          answer.dest = 0;
          answer.src  = f.dest;
          answer.op   = VSCP_RS485_OP_NO_EVENTS;
        }
        uint8_t out[VSCP_RS485_MAX_FRAME];
        const size_t len = vscp_rs485_encode(&opt, &answer, out);
        if (::write(m_fd, out, len) != (ssize_t) len) {
          continue;
        }
      }
    }
  }

  int m_fd = -1;
  std::string m_slave;
  std::atomic<bool> m_run{ false };
  std::thread m_thread;
  std::mutex m_mutex;
  std::map<uint8_t, std::vector<vscp_rs485_frame_t>> m_events;
  std::map<uint8_t, int> m_polls;
  std::vector<vscp_rs485_frame_t> m_received;
};
#endif

int
main(int argc, char **argv)
{
  if (argc < 2) {
    std::printf("Usage: %s path-to-driver\n", argv[0]);
    return 2;
  }

#ifdef _WIN32
  g_lib = LoadLibraryA(argv[1]);
#else
  g_lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
#endif
  if (!g_lib) {
    std::printf("FAIL: unable to load %s\n", argv[1]);
    return 1;
  }

  Api a;
  bool ok = load(a.open, "CanalOpen") & load(a.close, "CanalClose") & load(a.getLevel, "CanalGetLevel") &
            load(a.send, "CanalSend") & load(a.blockingSend, "CanalBlockingSend") &
            load(a.receive, "CanalReceive") & load(a.blockingReceive, "CanalBlockingReceive") &
            load(a.dataAvailable, "CanalDataAvailable") & load(a.getStatus, "CanalGetStatus") &
            load(a.getStatistics, "CanalGetStatistics") & load(a.setFilter, "CanalSetFilter") &
            load(a.setMask, "CanalSetMask") & load(a.setBaudrate, "CanalSetBaudrate") &
            load(a.getVersion, "CanalGetVersion") & load(a.getDllVersion, "CanalGetDllVersion") &
            load(a.getVendor, "CanalGetVendorString") & load(a.getDriverInfo, "CanalGetDriverInfo") &
            load(a.sendFD, "CanalSendFD") & load(a.receiveFD, "CanalReceiveFD") &
            load(a.blockingReceiveFD, "CanalBlockingReceiveFD");
  if (!ok) {
    return 1;
  }

  // --- Information ---------------------------------------------------------
  CHECK((a.getVersion() >> 24) == CANAL_MAIN_VERSION);
  CHECK(((a.getVersion() >> 16) & 0xff) == CANAL_MINOR_VERSION);
  CHECK(a.getDllVersion() != 0);
  CHECK(std::strlen(a.getVendor()) > 0);
  CHECK(std::strstr(a.getDriverInfo(), "<config>") != nullptr);
  CHECK(std::strstr(a.getDriverInfo(), "<level>1</level>") != nullptr);

  // --- Invalid configuration and handles -----------------------------------
  CHECK(a.open("", 0) <= 0);                                  // No serial port
  CHECK(a.open("/no/such/port", 1) <= 0);                     // Can not open
  canalMsg m = vscpFrame(20, 9, 0, 0);
  CHECK(a.send(9999, &m) == CANAL_ERROR_NOT_OPEN);
  CHECK(a.close(9999) == CANAL_ERROR_NOT_OPEN);
  CHECK(a.getLevel(9999) == 0);

#ifndef _WIN32
  // --- A simulated RS-485 segment on a pseudo terminal ---------------------
  SimSegment seg;
  CHECK(seg.start());
  const std::string port = seg.slavePath();
  CHECK(a.open((port + ";115200;0-3").c_str(), 1) <= 0);    // Address 0 is the master
  CHECK(a.open((port + ";0").c_str(), 1) <= 0);             // Baud rate

  // 8N1 (flag 0x01): a pseudo terminal has no parity bit
  long h = a.open((port + ";115200;1-10;30;1;3;100").c_str(), 1);
  CHECK(h > 0);
  CHECK(a.getLevel(h) == CANAL_LEVEL_STANDARD);

  // Node 5 sends CLASS1.MEASUREMENT temperature, node 9 has no events
  seg.queueEvent(5, 10, 6, { 0x89, 0x02, 0x1A });
  canalMsg rx;
  CHECK(a.blockingReceive(h, &rx, 3000) == CANAL_ERROR_SUCCESS);
  CHECK(rx.flags & CANAL_IDFLAG_EXTENDED);
  CHECK(rx.id == ((3UL << 26) | (1UL << 25) | (10UL << 16) | (6UL << 8) | 5));
  CHECK((rx.sizeData == 3) && (rx.data[0] == 0x89) && (rx.data[2] == 0x1A));

  // A class above 255 (bit 8 in the flags) to the master
  seg.queueEvent(9, 0x105, 1, {});
  CHECK(a.blockingReceive(h, &rx, 3000) == CANAL_ERROR_SUCCESS);
  CHECK(rx.id == ((3UL << 26) | (1UL << 25) | (0x105UL << 16) | (1UL << 8) | 9));
  CHECK(rx.sizeData == 0);

  // Wait until the master knows both nodes, then send to them
  for (int i = 0; (i < 100) && (seg.pollsOf(5) < 3 || seg.pollsOf(9) < 3); i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  CHECK(seg.pollsOf(5) >= 3);
  CHECK(seg.pollsOf(9) >= 3);

  // An event for everybody goes to every active node
  m = vscpFrame(20, 9, 3, 0x11);
  CHECK(a.send(h, &m) == CANAL_ERROR_SUCCESS);
  // READ_REGISTER (CLASS1.PROTOCOL type 9) for node 9 goes to node 9 only
  m          = vscpFrame(0, 9, 2, 0);
  m.data[0]  = 9;
  m.data[1]  = 0xD0;
  CHECK(a.send(h, &m) == CANAL_ERROR_SUCCESS);
  for (int i = 0; (i < 100) && (seg.received().size() < 3); i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  auto got = seg.received();
  CHECK(got.size() == 3);
  int toNode5 = 0, toNode9 = 0, readReg = 0;
  for (const auto &f : got) {
    CHECK(f.src == 0); // From the master
    toNode5 += (5 == f.dest);
    toNode9 += (9 == f.dest);
    readReg += (0 == f.vscp_class) && (9 == f.vscp_type) && (9 == f.dest) && (0xD0 == f.data[1]);
  }
  CHECK((1 == toNode5) && (2 == toNode9) && (1 == readReg));

  // Invalid frames
  m = vscpFrame(20, 9, 1, 0);
  m.flags = CANAL_IDFLAG_STANDARD; // VSCP needs extended ids
  CHECK(a.send(h, &m) == CANAL_ERROR_CAN_ID);
  m          = vscpFrame(20, 9, 1, 0);
  m.sizeData = 9;
  CHECK(a.send(h, &m) == CANAL_ERROR_MTU);
  canalMsgFD fd;
  std::memset(&fd, 0, sizeof(fd));
  fd.flags    = CANAL_IDFLAG_FD | CANAL_IDFLAG_EXTENDED;
  fd.sizeData = 12;
  CHECK(a.sendFD(h, &fd) == CANAL_ERROR_FD_SUPPORT);

  // Filter: only class 10
  CHECK(a.setFilter(h, 10UL << 16) == CANAL_ERROR_SUCCESS);
  CHECK(a.setMask(h, 0x1FFUL << 16) == CANAL_ERROR_SUCCESS);
  seg.queueEvent(5, 20, 3, { 1, 2, 3 }); // Filtered out
  seg.queueEvent(5, 10, 6, { 7 });
  CHECK(a.blockingReceive(h, &rx, 3000) == CANAL_ERROR_SUCCESS);
  CHECK(((rx.id >> 16) & 0x1FF) == 10);
  CHECK(a.blockingReceive(h, &rx, 300) == CANAL_ERROR_TIMEOUT);
  a.setMask(h, 0);

  // Statistics and status
  canalStatistics st;
  CHECK(a.getStatistics(h, &st) == CANAL_ERROR_SUCCESS);
  CHECK(st.cntReceiveFrames == 3);
  CHECK(st.cntTransmitFrames == 2);
  canalStatus status;
  CHECK(a.getStatus(h, &status) == CANAL_ERROR_SUCCESS);
  CHECK(status.channel_status & CANAL_STATUS_ACTIVE);

  // Close while another thread is blocked in a receive
  int blockedRv = -1;
  std::thread t([&]() {
    canalMsg msg;
    blockedRv = a.blockingReceive(h, &msg, 0); // Forever
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  CHECK(a.close(h) == CANAL_ERROR_SUCCESS);
  t.join();
  CHECK(blockedRv == CANAL_ERROR_NOT_OPEN);
  CHECK(a.close(h) == CANAL_ERROR_NOT_OPEN);
  seg.stop();
#endif

  std::printf("%d checks, %d failed\n", g_checks, g_failed);
  return g_failed ? 1 : 0;
}
