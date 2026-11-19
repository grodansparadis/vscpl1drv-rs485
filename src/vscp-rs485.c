// vscp-rs485.c
//
// VSCP Level I over RS-485/RS-422: frames, CRC and receiver. See
// vscp-rs485.h.
//
// This file is part of the VSCP (https://www.vscp.org)
// SPDX-License-Identifier: MIT
// Copyright (C) 2000-2026 Ake Hedman, Grodans Paradis AB <info@grodansparadis.com>
//

#include <string.h>

#include "vscp-rs485.h"

uint8_t
vscp_rs485_crc(vscp_rs485_crc_t algo, uint8_t crc, const uint8_t *data, size_t len)
{
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++) {
      if (VSCP_RS485_CRC_DOW == algo) {
        // Reflected polynomial x^8 + x^5 + x^4 + 1 (0x31 -> 0x8C)
        crc = (uint8_t) ((crc & 0x01) ? ((crc >> 1) ^ 0x8C) : (crc >> 1));
      }
      else {
        // Polynomial x^8 + x^2 + x + 1 (0x07), not reflected
        crc = (uint8_t) ((crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1));
      }
    }
  }
  return crc;
}

size_t
vscp_rs485_encode(const vscp_rs485_options_t *opt, const vscp_rs485_frame_t *f, uint8_t out[15])
{
  size_t n = 0;
  out[n++] = f->dest;
  out[n++] = f->src;
  out[n++] = f->op;

  switch (f->op) {
    case VSCP_RS485_OP_EVENT: {
      if ((f->size > VSCP_RS485_MAX_DATA) || (f->vscp_class > 511)) {
        return 0;
      }
      out[n++] = (uint8_t) ((f->size & VSCP_RS485_FLAG_SIZE) | ((f->vscp_class & 0x100) ? VSCP_RS485_FLAG_CLASS8 : 0));
      out[n++] = (uint8_t) (f->vscp_class & 0xFF);
      out[n++] = f->vscp_type;
      const size_t count = opt->fixed ? VSCP_RS485_MAX_DATA : f->size;
      for (size_t i = 0; i < count; i++) {
        out[n++] = (i < f->size) ? f->data[i] : 0;
      }
    } break;

    case VSCP_RS485_OP_NOOP:
    case VSCP_RS485_OP_POLL:
    case VSCP_RS485_OP_NO_EVENTS:
      break;

    default:
      return 0;
  }

  out[n] = vscp_rs485_crc(opt->crc, 0, out, n);
  return n + 1;
}

void
vscp_rs485_rx_init(vscp_rs485_rx_t *rx, const vscp_rs485_options_t *opt)
{
  memset(rx, 0, sizeof(*rx));
  rx->opt = *opt;
}

void
vscp_rs485_rx_reset(vscp_rs485_rx_t *rx)
{
  rx->pos  = 0;
  rx->need = 0;
}

vscp_rs485_rx_result_t
vscp_rs485_rx_char(vscp_rs485_rx_t *rx, uint8_t c, int mark, vscp_rs485_frame_t *frame)
{
  vscp_rs485_rx_result_t result = VSCP_RS485_RX_PENDING;

  if (1 == mark) {
    // An address character always starts a new frame
    if (rx->pos) {
      result = VSCP_RS485_RX_ERR_RESYNC;
    }
    rx->pos  = 0;
    rx->need = 0;
  }
  else if ((0 == rx->pos) && (0 == mark)) {
    return VSCP_RS485_RX_SKIPPED; // Data character without a frame start
  }

  rx->buf[rx->pos++] = c;

  // The operation (third character) tells the length; for EVENT the flags
  // (fourth character) are needed too
  if ((3 == rx->pos) && (0 == rx->need)) {
    switch (rx->buf[2]) {
      case VSCP_RS485_OP_NOOP:
      case VSCP_RS485_OP_POLL:
      case VSCP_RS485_OP_NO_EVENTS:
        rx->need = VSCP_RS485_SHORT_SIZE;
        break;
      case VSCP_RS485_OP_EVENT:
        break;
      default:
        vscp_rs485_rx_reset(rx);
        return VSCP_RS485_RX_ERR_FORMAT;
    }
  }
  else if ((4 == rx->pos) && (0 == rx->need)) {
    const uint8_t flags = rx->buf[3];
    const size_t size   = flags & VSCP_RS485_FLAG_SIZE;
    if ((flags & VSCP_RS485_FLAG_RSVD) || (size > VSCP_RS485_MAX_DATA)) {
      vscp_rs485_rx_reset(rx);
      return VSCP_RS485_RX_ERR_FORMAT;
    }
    rx->need = VSCP_RS485_EVENT_BASE + (rx->opt.fixed ? VSCP_RS485_MAX_DATA : size);
  }

  if ((0 == rx->need) || (rx->pos < rx->need)) {
    return result;
  }

  // Complete
  const size_t len = rx->pos;
  rx->pos          = 0;
  rx->need         = 0;
  if (vscp_rs485_crc(rx->opt.crc, 0, rx->buf, len - 1) != rx->buf[len - 1]) {
    return VSCP_RS485_RX_ERR_CRC;
  }

  memset(frame, 0, sizeof(*frame));
  frame->dest = rx->buf[0];
  frame->src  = rx->buf[1];
  frame->op   = rx->buf[2];
  if (VSCP_RS485_OP_EVENT == frame->op) {
    frame->size       = rx->buf[3] & VSCP_RS485_FLAG_SIZE;
    frame->vscp_class = (uint16_t) (rx->buf[4] | ((rx->buf[3] & VSCP_RS485_FLAG_CLASS8) ? 0x100 : 0));
    frame->vscp_type  = rx->buf[5];
    memcpy(frame->data, rx->buf + 6, frame->size);
  }
  return VSCP_RS485_RX_FRAME;
}
