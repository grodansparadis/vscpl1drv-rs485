// rs485-port.h
//
// A serial port for the RS-485 master, with the ninth (address) bit.
//
// UARTs of PCs have no ninth data bit. It is made with the parity bit:
//
//   Linux    mark/space parity (CMSPAR). The address character is sent
//            with mark parity, the rest with space parity. Received
//            characters are checked against space parity and parity
//            errors are marked (PARMRK), so the master knows which
//            received character had the ninth bit set.
//   macOS    no mark/space parity: even or odd parity is chosen for every
//            character so that the parity bit has the wanted value.
//            Received ninth bits are not known.
//   Windows  MARKPARITY/SPACEPARITY. Received ninth bits are not known.
//
// When the ninth bit of received characters is not known the master finds
// the frames from the poll timing, which is how the bus works anyway (only
// the polled node answers).
//
// RS-485 direction: adapters with automatic direction control (most USB
// adapters) need nothing. Otherwise the Linux kernel RS-485 mode
// (TIOCSRS485, RTS during sending) or RTS set by the driver around every
// frame can be used.
//
// This file is part of the VSCP (https://www.vscp.org)
// SPDX-License-Identifier: MIT
// Copyright (C) 2000-2026 Ake Hedman, Grodans Paradis AB <info@grodansparadis.com>
//

#ifndef RS485_PORT_H
#define RS485_PORT_H

#include <cstdint>
#include <deque>
#include <string>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "rs485-master.h"

struct Rs485PortOptions {
  std::string device;           ///< /dev/ttyUSB0, COM3 ...
  uint32_t baudrate  = 115200;
  bool nineBit       = true;    ///< Address character with the ninth bit set (else plain 8N1)
  bool kernelRs485   = false;   ///< Linux: enable the kernel RS-485 mode (RTS on send)
  bool rtsDirection  = false;   ///< Set RTS while sending a frame
};

class CRs485Port : public IRs485Port {
public:
  CRs485Port();
  ~CRs485Port() override;

  /// Open the port. On failure error() tells why.
  bool open(const Rs485PortOptions &opt);
  void close(void);
  bool isOpen(void) const;

  /// Change the baud rate of an open port
  bool setBaudrate(uint32_t baudrate);

  /// Last error text
  const std::string &error(void) const { return m_error; }

  // IRs485Port
  bool writeFrame(const uint8_t *data, size_t len) override;
  int readChar(uint8_t &c, int &mark, uint32_t timeout_ms) override;
  void flushInput(void) override;

  /// True if received ninth bits are known on this platform/mode
  bool knowsReceivedMarks(void) const { return m_markRx; }

  /*!
    Decode characters read with PARMRK and space parity (Linux): 0xFF 0xFF
    is 0xFF, 0xFF 0x00 X is X with a parity error, i.e. the ninth bit set.
    @param state Decoder state kept between reads (start with 0)
  */
  static void decodeParmrk(int &state, const uint8_t *buf, size_t n, std::deque<std::pair<uint8_t, int>> &out);

private:
  bool fail(const std::string &text);
  bool configure(void);
  void setRts(bool on);

  Rs485PortOptions m_opt;
  std::string m_error;
  bool m_markRx;

  // Decoded received characters (character, mark)
  std::deque<std::pair<uint8_t, int>> m_rx;

#ifdef _WIN32
  HANDLE m_handle;
  DWORD m_readTimeout;
  bool setParity(BYTE parity);
#else
  int m_fd;
  int m_parmrkState; // PARMRK decoding: 0 normal, 1 after 0xFF, 2 after 0xFF 0x00
  bool setParityBit(bool one, uint8_t c);
  int m_currentParity; // -1 unknown, 0 space/even, 1 mark/odd (PARODD set)
#endif
};

#endif
