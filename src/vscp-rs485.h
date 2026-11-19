// vscp-rs485.h
//
// VSCP Level I over RS-485/RS-422: frames, CRC and a byte-by-byte
// receiver. Plain C99, no operating system calls; shared by
// vscpl1drv-rs485 and vscpl2drv-rs485.
//
// The wire format is the one of the VSCP specification, chapter "VSCP
// Level I over RS-485/RS-422"
// (https://grodansparadis.github.io/vscp-doc-spec/#/./vscp_level_i_over_rs-485_rs-422).
// Where the chapter is not explicit, the provisional interpretation of the
// specification's RS-485 V1 interoperability proposal is used (see
// docs/protocol.md):
//
//   EVENT      dest src 1 flags class type data[N] crc   7 + N characters
//   POLL       dest src 2 crc                            4 characters
//   NO_EVENTS  dest src 3 crc                            4 characters
//   NOOP       dest src 0 crc                            4 characters
//
//   dest     The first character. On the wire its ninth bit is set (the
//            address marker); the low eight bits are the destination
//            address. 0 = the master, 1-254 nodes.
//   src      Originating address, 0 = the master.
//   flags    Bits 0-3 number of data bytes (0-8), bit 4 bit 8 of the VSCP
//            class, bits 5-7 zero.
//   crc      CRC over all characters before it (the low eight bits of the
//            address character).
//
// With "fixed length" frames an EVENT always has eight data bytes (zero
// padded, 15 characters), as drawn in the specification's frame table.
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

#ifndef VSCP_RS485_H
#define VSCP_RS485_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VSCP_RS485_ADDR_MASTER    0x00 ///< The master (server)
#define VSCP_RS485_ADDR_FIRST     0x01 ///< First node address
#define VSCP_RS485_ADDR_LAST      0xFE ///< Last node address
#define VSCP_RS485_ADDR_BROADCAST 0xFF ///< Optional broadcast (not in the specification)

#define VSCP_RS485_OP_NOOP      0 ///< No operation
#define VSCP_RS485_OP_EVENT     1 ///< Level I event to/from a node
#define VSCP_RS485_OP_POLL      2 ///< Poll for an event
#define VSCP_RS485_OP_NO_EVENTS 3 ///< No events to send

#define VSCP_RS485_MAX_DATA    8  ///< Level I: at most eight data bytes
#define VSCP_RS485_SHORT_SIZE  4  ///< POLL, NO_EVENTS, NOOP
#define VSCP_RS485_EVENT_BASE  7  ///< EVENT without data
#define VSCP_RS485_MAX_FRAME   15 ///< EVENT with eight data bytes
#define VSCP_RS485_FLAG_CLASS8 0x10
#define VSCP_RS485_FLAG_SIZE   0x0F
#define VSCP_RS485_FLAG_RSVD   0xE0

/// CRC algorithm
typedef enum {
  VSCP_RS485_CRC_DOW = 0, ///< CRC-8/MAXIM-DOW (Dallas/Maxim 1-Wire), the specification's "DOW checksum"
  VSCP_RS485_CRC_ATM = 1  ///< CRC-8 polynomial 0x07 (vscp crc8.c), for nodes that use it
} vscp_rs485_crc_t;

/// One decoded frame
typedef struct {
  uint8_t dest;        ///< Destination address
  uint8_t src;         ///< Originating address
  uint8_t op;          ///< VSCP_RS485_OP_*
  uint16_t vscp_class; ///< 0-511 (EVENT only)
  uint8_t vscp_type;   ///< EVENT only
  uint8_t size;        ///< Number of data bytes 0-8 (EVENT only)
  uint8_t data[8];
} vscp_rs485_frame_t;

/// Frame codec options
typedef struct {
  vscp_rs485_crc_t crc; ///< CRC algorithm
  int fixed;            ///< EVENT frames always carry eight data bytes
} vscp_rs485_options_t;

/// CRC of len bytes, starting from crc (0 for a new frame)
uint8_t vscp_rs485_crc(vscp_rs485_crc_t algo, uint8_t crc, const uint8_t *data, size_t len);

/*!
  Encode a frame. out[0] is the address character (sent with the ninth
  bit set), the rest are sent with it cleared.
  @return Number of characters (4 to 15), 0 if the frame is invalid
          (class > 511, size > 8, unknown operation).
*/
size_t vscp_rs485_encode(const vscp_rs485_options_t *opt, const vscp_rs485_frame_t *frame, uint8_t out[15]);

/// Receiver results
typedef enum {
  VSCP_RS485_RX_PENDING = 0, ///< More characters needed
  VSCP_RS485_RX_FRAME   = 1, ///< A complete, valid frame
  VSCP_RS485_RX_SKIPPED = 2, ///< Character outside a frame (no address marker) ignored
  VSCP_RS485_RX_ERR_CRC = -1,     ///< Frame complete but the CRC is wrong
  VSCP_RS485_RX_ERR_FORMAT = -2,  ///< Unknown operation, reserved flag bits, too many data bytes
  VSCP_RS485_RX_ERR_RESYNC = -3   ///< An address marker arrived inside a frame: the partial frame is dropped
} vscp_rs485_rx_result_t;

/// Receiver state
typedef struct {
  vscp_rs485_options_t opt;
  uint8_t buf[VSCP_RS485_MAX_FRAME];
  size_t pos;  ///< Characters received of the current frame
  size_t need; ///< Characters the current frame has (0 = not known yet)
} vscp_rs485_rx_t;

void vscp_rs485_rx_init(vscp_rs485_rx_t *rx, const vscp_rs485_options_t *opt);

/// Forget a partial frame (after a time-out)
void vscp_rs485_rx_reset(vscp_rs485_rx_t *rx);

/*!
  Feed one character.
  @param mark 1 = the ninth bit was set (address character), 0 = it was
         clear, -1 = not known (8N1 ports, Windows, macOS): then the first
         character after a reset starts a frame.
  @param frame Filled in when VSCP_RS485_RX_FRAME is returned.
  On an error the receiver is reset; a RESYNC starts the new frame with
  this character.
*/
vscp_rs485_rx_result_t vscp_rs485_rx_char(vscp_rs485_rx_t *rx, uint8_t c, int mark, vscp_rs485_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif
