// rs485-port.cpp
//
// Serial port with the ninth (address) bit. See rs485-port.h.
//
// This file is part of the VSCP (https://www.vscp.org)
// SPDX-License-Identifier: MIT
// Copyright (C) 2000-2026 Ake Hedman, Grodans Paradis AB <info@grodansparadis.com>
//

#include "rs485-port.h"

#include <cerrno>
#include <cstring>

#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/serial.h>
#endif
#ifdef RS485_PORT_NO_CMSPAR
#undef CMSPAR // Build check of the path for systems without mark/space parity
#endif
#endif

#include <chrono>

namespace {

#ifndef _WIN32
bool
speedFor(uint32_t baud, speed_t &speed)
{
  static const struct {
    uint32_t baud;
    speed_t speed;
  } table[] = {
    { 1200, B1200 },     { 2400, B2400 },     { 4800, B4800 },     { 9600, B9600 },
    { 19200, B19200 },   { 38400, B38400 },   { 57600, B57600 },   { 115200, B115200 },
    { 230400, B230400 },
#ifdef B460800
    { 460800, B460800 },
#endif
#ifdef B500000
    { 500000, B500000 },
#endif
#ifdef B921600
    { 921600, B921600 },
#endif
#ifdef B1000000
    { 1000000, B1000000 },
#endif
  };
  for (const auto &t : table) {
    if (t.baud == baud) {
      speed = t.speed;
      return true;
    }
  }
  return false;
}

#ifndef CMSPAR
int
popcount8(uint8_t c)
{
  int n = 0;
  for (; c; c &= (uint8_t) (c - 1)) {
    n++;
  }
  return n;
}
#endif
#endif

} // namespace

CRs485Port::CRs485Port()
  : m_markRx(false)
#ifdef _WIN32
  , m_handle(INVALID_HANDLE_VALUE)
  , m_readTimeout(0xFFFFFFFF)
#else
  , m_fd(-1)
  , m_parmrkState(0)
  , m_currentParity(-1)
#endif
{
}

CRs485Port::~CRs485Port()
{
  close();
}

bool
CRs485Port::fail(const std::string &text)
{
  m_error = text;
  return false;
}

void
CRs485Port::decodeParmrk(int &state, const uint8_t *buf, size_t n, std::deque<std::pair<uint8_t, int>> &out)
{
  for (size_t i = 0; i < n; i++) {
    const uint8_t b = buf[i];
    switch (state) {
      case 0:
        if (0xFF == b) {
          state = 1;
        }
        else {
          out.emplace_back(b, 0);
        }
        break;
      case 1:
        if (0xFF == b) {
          out.emplace_back(0xFF, 0);
          state = 0;
        }
        else {
          state = 2; // 0xFF 0x00: a marked character follows
        }
        break;
      default:
        out.emplace_back(b, 1);
        state = 0;
        break;
    }
  }
}

#ifndef _WIN32

///////////////////////////////////////////////////////////////////////////////
//                                 POSIX
///////////////////////////////////////////////////////////////////////////////

bool
CRs485Port::isOpen(void) const
{
  return m_fd >= 0;
}

bool
CRs485Port::open(const Rs485PortOptions &opt)
{
  close();
  m_opt = opt;

  m_fd = ::open(opt.device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (m_fd < 0) {
    return fail("Unable to open " + opt.device + ": " + std::strerror(errno));
  }
  if (!configure()) {
    ::close(m_fd);
    m_fd = -1;
    return false;
  }

  if (opt.kernelRs485) {
#if defined(__linux__) && defined(TIOCSRS485)
    struct serial_rs485 rs485;
    std::memset(&rs485, 0, sizeof(rs485));
    rs485.flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND;
    if (ioctl(m_fd, TIOCSRS485, &rs485) < 0) {
      std::string err = std::strerror(errno);
      ::close(m_fd);
      m_fd = -1;
      return fail("The kernel RS-485 mode is not supported by " + opt.device + ": " + err);
    }
#else
    ::close(m_fd);
    m_fd = -1;
    return fail("The kernel RS-485 mode is only available on Linux");
#endif
  }

  setRts(false);
  flushInput();
  return true;
}

bool
CRs485Port::configure(void)
{
  struct termios tio;
  if (tcgetattr(m_fd, &tio) < 0) {
    return fail("Not a serial port: " + m_opt.device);
  }
  speed_t speed;
  if (!speedFor(m_opt.baudrate, speed)) {
    return fail("Unsupported baud rate " + std::to_string(m_opt.baudrate));
  }

  cfmakeraw(&tio);
  tio.c_cflag |= CLOCAL | CREAD;
  tio.c_cflag &= ~(CSIZE | CSTOPB | PARENB | PARODD);
  tio.c_cflag |= CS8;
#ifdef CRTSCTS
  tio.c_cflag &= ~CRTSCTS;
#endif
  tio.c_iflag &= ~(IXON | IXOFF | IXANY | INPCK | PARMRK | IGNPAR | ISTRIP);
  tio.c_cc[VMIN]  = 0;
  tio.c_cc[VTIME] = 0;

  m_markRx        = false;
  m_currentParity = -1;
  if (m_opt.nineBit) {
#ifdef CMSPAR
    // Space parity: a received ninth bit of one is a parity error, which
    // PARMRK reports as 0xFF 0x00 <character>
    tio.c_cflag |= PARENB | CMSPAR;
    tio.c_iflag |= INPCK | PARMRK;
    m_markRx        = true;
    m_currentParity = 0;
#else
    // Even/odd chosen per character when sending; received parity not checked
    tio.c_cflag |= PARENB;
    m_currentParity = 0;
#endif
  }

  cfsetispeed(&tio, speed);
  cfsetospeed(&tio, speed);
  if (tcsetattr(m_fd, TCSANOW, &tio) < 0) {
    return fail("Unable to configure " + m_opt.device + ": " + std::strerror(errno));
  }
  return true;
}

void
CRs485Port::close(void)
{
  if (m_fd >= 0) {
    ::close(m_fd);
    m_fd = -1;
  }
  m_rx.clear();
  m_parmrkState = 0;
}

bool
CRs485Port::setBaudrate(uint32_t baudrate)
{
  if (m_fd < 0) {
    return fail("The port is not open");
  }
  const uint32_t old = m_opt.baudrate;
  m_opt.baudrate     = baudrate;
  if (!configure()) {
    m_opt.baudrate = old;
    configure();
    return false;
  }
  return true;
}

void
CRs485Port::setRts(bool on)
{
  if (!m_opt.rtsDirection || (m_fd < 0)) {
    return;
  }
  int bits = TIOCM_RTS;
  ioctl(m_fd, on ? TIOCMBIS : TIOCMBIC, &bits);
}

// Make the parity bit of the next character c equal to one (or zero)
bool
CRs485Port::setParityBit(bool one, uint8_t c)
{
  struct termios tio;
  if (tcgetattr(m_fd, &tio) < 0) {
    return false;
  }
#ifdef CMSPAR
  (void) c;
  const int want = one ? 1 : 0; // PARODD with CMSPAR = mark
#else
  // Even parity makes the parity bit equal to the number of ones (mod 2)
  const int want = ((popcount8(c) & 1) == (one ? 1 : 0)) ? 0 : 1; // 1 = odd
#endif
  if (want == m_currentParity) {
    return true;
  }
  if (want) {
    tio.c_cflag |= PARODD;
  }
  else {
    tio.c_cflag &= ~PARODD;
  }
  // TCSADRAIN: the characters already written keep their parity
  if (tcsetattr(m_fd, TCSADRAIN, &tio) < 0) {
    return false;
  }
  m_currentParity = want;
  return true;
}

bool
CRs485Port::writeFrame(const uint8_t *data, size_t len)
{
  if ((m_fd < 0) || (0 == len)) {
    return false;
  }
  setRts(true);

  auto writeAll = [this](const uint8_t *p, size_t n) {
    while (n) {
      ssize_t w = ::write(m_fd, p, n);
      if (w < 0) {
        if ((EAGAIN == errno) || (EINTR == errno)) {
          struct pollfd pfd = { m_fd, POLLOUT, 0 };
          ::poll(&pfd, 1, 100);
          continue;
        }
        return false;
      }
      p += w;
      n -= (size_t) w;
    }
    return true;
  };

  bool ok = true;
  if (!m_opt.nineBit) {
    ok = writeAll(data, len);
  }
  else {
#ifdef CMSPAR
    // Address character with mark parity, the rest with space parity
    ok = setParityBit(true, data[0]) && writeAll(data, 1) && setParityBit(false, 0) && writeAll(data + 1, len - 1);
#else
    for (size_t i = 0; ok && (i < len); i++) {
      ok = setParityBit(0 == i, data[i]) && writeAll(data + i, 1);
    }
#endif
  }

  tcdrain(m_fd); // Sent: the line can be turned around
  setRts(false);
  if (!ok) {
    m_error = std::string("Write failed: ") + std::strerror(errno);
  }
  return ok;
}

int
CRs485Port::readChar(uint8_t &c, int &mark, uint32_t timeout_ms)
{
  if (m_fd < 0) {
    return -1;
  }
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

  while (m_rx.empty()) {
    const auto now = std::chrono::steady_clock::now();
    const int left = (now >= deadline)
                       ? 0
                       : (int) std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count() + 1;
    struct pollfd pfd = { m_fd, POLLIN, 0 };
    const int pr      = ::poll(&pfd, 1, left);
    if (pr < 0) {
      if (EINTR == errno) {
        continue;
      }
      return -1;
    }
    if (0 == pr) {
      return 0;
    }
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
      return -1;
    }

    uint8_t buf[256];
    const ssize_t n = ::read(m_fd, buf, sizeof(buf));
    if (n < 0) {
      if ((EAGAIN == errno) || (EINTR == errno)) {
        continue;
      }
      return -1;
    }
    if (m_markRx) {
      decodeParmrk(m_parmrkState, buf, (size_t) n, m_rx);
    }
    else {
      for (ssize_t i = 0; i < n; i++) {
        m_rx.emplace_back(buf[i], -1);
      }
    }
  }

  c    = m_rx.front().first;
  mark = m_rx.front().second;
  m_rx.pop_front();
  return 1;
}

void
CRs485Port::flushInput(void)
{
  if (m_fd >= 0) {
    tcflush(m_fd, TCIFLUSH);
  }
  m_rx.clear();
  m_parmrkState = 0;
}

#else

///////////////////////////////////////////////////////////////////////////////
//                                Windows
///////////////////////////////////////////////////////////////////////////////

bool
CRs485Port::isOpen(void) const
{
  return INVALID_HANDLE_VALUE != m_handle;
}

bool
CRs485Port::open(const Rs485PortOptions &opt)
{
  close();
  m_opt = opt;

  if (opt.kernelRs485) {
    return fail("The kernel RS-485 mode is only available on Linux");
  }

  // COM10 and up need the \\.\ prefix; it works for all ports
  std::string path = opt.device;
  if (0 != path.rfind("\\\\.\\", 0)) {
    path = "\\\\.\\" + path;
  }
  m_handle = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (INVALID_HANDLE_VALUE == m_handle) {
    return fail("Unable to open " + opt.device + " (error " + std::to_string(GetLastError()) + ")");
  }
  if (!configure()) {
    CloseHandle(m_handle);
    m_handle = INVALID_HANDLE_VALUE;
    return false;
  }
  SetupComm(m_handle, 4096, 4096);
  setRts(false);
  flushInput();
  return true;
}

bool
CRs485Port::configure(void)
{
  DCB dcb;
  std::memset(&dcb, 0, sizeof(dcb));
  dcb.DCBlength = sizeof(dcb);
  if (!GetCommState(m_handle, &dcb)) {
    return fail("Not a serial port: " + m_opt.device);
  }
  dcb.BaudRate        = m_opt.baudrate;
  dcb.ByteSize        = 8;
  dcb.StopBits        = ONESTOPBIT;
  dcb.fBinary         = TRUE;
  dcb.fParity         = m_opt.nineBit ? TRUE : FALSE;
  dcb.Parity          = m_opt.nineBit ? SPACEPARITY : NOPARITY;
  dcb.fOutxCtsFlow    = FALSE;
  dcb.fOutxDsrFlow    = FALSE;
  dcb.fDtrControl     = DTR_CONTROL_ENABLE;
  dcb.fRtsControl     = RTS_CONTROL_DISABLE;
  dcb.fDsrSensitivity = FALSE;
  dcb.fOutX           = FALSE;
  dcb.fInX            = FALSE;
  dcb.fErrorChar      = FALSE;
  dcb.fNull           = FALSE;
  dcb.fAbortOnError   = FALSE;
  if (!SetCommState(m_handle, &dcb)) {
    return fail("Unable to configure " + m_opt.device + " (baud rate " + std::to_string(m_opt.baudrate) + ")");
  }
  m_markRx      = false; // Windows can not tell which character had a parity error
  m_readTimeout = 0xFFFFFFFF;
  return true;
}

void
CRs485Port::close(void)
{
  if (INVALID_HANDLE_VALUE != m_handle) {
    CloseHandle(m_handle);
    m_handle = INVALID_HANDLE_VALUE;
  }
  m_rx.clear();
}

bool
CRs485Port::setBaudrate(uint32_t baudrate)
{
  if (INVALID_HANDLE_VALUE == m_handle) {
    return fail("The port is not open");
  }
  const uint32_t old = m_opt.baudrate;
  m_opt.baudrate     = baudrate;
  if (!configure()) {
    m_opt.baudrate = old;
    configure();
    return false;
  }
  return true;
}

void
CRs485Port::setRts(bool on)
{
  if (m_opt.rtsDirection && (INVALID_HANDLE_VALUE != m_handle)) {
    EscapeCommFunction(m_handle, on ? SETRTS : CLRRTS);
  }
}

bool
CRs485Port::setParity(BYTE parity)
{
  DCB dcb;
  std::memset(&dcb, 0, sizeof(dcb));
  dcb.DCBlength = sizeof(dcb);
  if (!GetCommState(m_handle, &dcb)) {
    return false;
  }
  if (dcb.Parity == parity) {
    return true;
  }
  dcb.Parity = parity;
  return SetCommState(m_handle, &dcb) ? true : false;
}

bool
CRs485Port::writeFrame(const uint8_t *data, size_t len)
{
  if ((INVALID_HANDLE_VALUE == m_handle) || (0 == len)) {
    return false;
  }
  setRts(true);

  auto writeAll = [this](const uint8_t *p, size_t n) {
    DWORD written = 0;
    if (!WriteFile(m_handle, p, (DWORD) n, &written, nullptr) || (written != n)) {
      return false;
    }
    return FlushFileBuffers(m_handle) ? true : false; // Wait until sent
  };

  bool ok;
  if (m_opt.nineBit) {
    ok = setParity(MARKPARITY) && writeAll(data, 1) && setParity(SPACEPARITY) && ((len < 2) || writeAll(data + 1, len - 1));
  }
  else {
    ok = writeAll(data, len);
  }
  setRts(false);
  if (!ok) {
    m_error = "Write failed (error " + std::to_string(GetLastError()) + ")";
  }
  return ok;
}

int
CRs485Port::readChar(uint8_t &c, int &mark, uint32_t timeout_ms)
{
  if (INVALID_HANDLE_VALUE == m_handle) {
    return -1;
  }
  if (m_rx.empty()) {
    if (m_readTimeout != timeout_ms) {
      // Return as soon as something is there, or after the time-out
      COMMTIMEOUTS to;
      std::memset(&to, 0, sizeof(to));
      to.ReadIntervalTimeout        = MAXDWORD;
      to.ReadTotalTimeoutMultiplier = MAXDWORD;
      to.ReadTotalTimeoutConstant   = timeout_ms ? timeout_ms : 1;
      if (!SetCommTimeouts(m_handle, &to)) {
        return -1;
      }
      m_readTimeout = timeout_ms;
    }
    uint8_t buf[256];
    DWORD n = 0;
    if (!ReadFile(m_handle, buf, sizeof(buf), &n, nullptr)) {
      DWORD errors = 0;
      ClearCommError(m_handle, &errors, nullptr);
      return -1;
    }
    // Parity errors (received address characters) are expected
    DWORD errors = 0;
    ClearCommError(m_handle, &errors, nullptr);
    if (0 == n) {
      return 0;
    }
    for (DWORD i = 0; i < n; i++) {
      m_rx.emplace_back(buf[i], -1);
    }
  }
  c    = m_rx.front().first;
  mark = m_rx.front().second;
  m_rx.pop_front();
  return 1;
}

void
CRs485Port::flushInput(void)
{
  if (INVALID_HANDLE_VALUE != m_handle) {
    PurgeComm(m_handle, PURGE_RXCLEAR);
  }
  m_rx.clear();
}

#endif
