// driverinfo.h
//
// What CanalGetDriverInfo returns: an XML description of the configuration
// string and the flags, so that a host such as VSCP Works can show a dialog
// for them. Format: https://grodansparadis.github.io/vscp-doc-canal/#/canalgetdriverinfo
//
// Update it when the configuration string or the flags change.
//
// This file is part of the VSCP (https://www.vscp.org)
// SPDX-License-Identifier: MIT
// Copyright (C) 2000-2026 Ake Hedman, Grodans Paradis AB <info@grodansparadis.com>
//

#ifndef DRIVERINFO_H
#define DRIVERINFO_H

// clang-format off
#define DRIVER_INFO_XML \
"<?xml version = \"1.0\" encoding = \"UTF-8\" ?>\n" \
"<config>\n" \
"  <description format=\"text\">VSCP Level I driver for RS-485/RS-422: the bus master of VSCP Level I over RS-485. It polls the nodes on the segment for events, finds new nodes and sends events from the host to the nodes.</description>\n" \
"  <level>1</level>\n" \
"  <blocking>yes</blocking>\n" \
"  <infourl>https://github.com/grodansparadis/vscpl1drv-rs485</infourl>\n" \
"  <items>\n" \
"    <item optional=\"false\" type=\"string\" description=\"Serial port of the RS-485 adapter: /dev/ttyUSB0, /dev/serial/by-id/..., COM3.\" infourl=\"https://github.com/grodansparadis/vscpl1drv-rs485/blob/master/docs/configure.md\" />\n" \
"    <item optional=\"true\" type=\"uint32\" min=\"1200\" max=\"4000000\" description=\"Baud rate of the segment (default 115200).\" infourl=\"https://github.com/grodansparadis/vscpl1drv-rs485/blob/master/docs/configure.md\" />\n" \
"    <item optional=\"true\" type=\"string\" description=\"Node addresses on the segment, e.g. 1-32,40 (default 1-127). Only these are polled.\" infourl=\"https://github.com/grodansparadis/vscpl1drv-rs485/blob/master/docs/configure.md\" />\n" \
"    <item optional=\"true\" type=\"uint32\" min=\"1\" max=\"10000\" description=\"Milliseconds to wait for the answer to a poll (default 20).\" infourl=\"https://github.com/grodansparadis/vscpl1drv-rs485/blob/master/docs/configure.md\" />\n" \
"    <item optional=\"true\" type=\"uint32\" min=\"0\" max=\"100000\" description=\"Probe one address without a node after this many polling rounds (default 1). 0 = poll every address every round.\" infourl=\"https://github.com/grodansparadis/vscpl1drv-rs485/blob/master/docs/configure.md\" />\n" \
"    <item optional=\"true\" type=\"uint32\" min=\"1\" max=\"255\" description=\"Missed polls before a node counts as gone (default 3).\" infourl=\"https://github.com/grodansparadis/vscpl1drv-rs485/blob/master/docs/configure.md\" />\n" \
"    <item optional=\"true\" type=\"uint32\" min=\"1\" max=\"1000000\" description=\"Size of the receive and transmit queues in frames (default 1000).\" infourl=\"https://github.com/grodansparadis/vscpl1drv-rs485/blob/master/docs/configure.md\" />\n" \
"  </items>\n" \
"  <flags>\n" \
"    <bit pos=\"0\" width=\"1\" type=\"bool\" description=\"No ninth (address) bit: plain 8N1 for adapters and nodes without it.\" infourl=\"https://github.com/grodansparadis/vscpl1drv-rs485/blob/master/docs/configure.md\" />\n" \
"    <bit pos=\"1\" width=\"1\" type=\"bool\" description=\"Fixed length EVENT frames: always eight data bytes (15 characters).\" infourl=\"https://github.com/grodansparadis/vscpl1drv-rs485/blob/master/docs/configure.md\" />\n" \
"    <bit pos=\"2\" width=\"1\" type=\"bool\" description=\"CRC-8 with polynomial 0x07 (vscp crc8.c) instead of the DOW CRC.\" infourl=\"https://github.com/grodansparadis/vscpl1drv-rs485/blob/master/docs/configure.md\" />\n" \
"    <bit pos=\"3\" width=\"1\" type=\"bool\" description=\"Send events from the host to the broadcast address 0xFF instead of to each node.\" infourl=\"https://github.com/grodansparadis/vscpl1drv-rs485/blob/master/docs/configure.md\" />\n" \
"    <bit pos=\"4\" width=\"1\" type=\"bool\" description=\"Linux: use the kernel RS-485 mode (RTS set while sending).\" infourl=\"https://github.com/grodansparadis/vscpl1drv-rs485/blob/master/docs/configure.md\" />\n" \
"    <bit pos=\"5\" width=\"1\" type=\"bool\" description=\"Set RTS while sending a frame (direction control without the kernel mode).\" infourl=\"https://github.com/grodansparadis/vscpl1drv-rs485/blob/master/docs/configure.md\" />\n" \
"  </flags>\n" \
"</config>\n"
// clang-format on

#endif
