#pragma once

#include <ia32.hpp>

#include "spin-lock.h"

namespace hv {

struct logger_msg {
  static constexpr uint32_t max_msg_length = 128;

  // ID of the current message
  uint64_t id;

  // timestamp counter of the current message
  uint64_t tsc;

  // process ID of the VCPU that sent the message
  uint32_t aux;

  // null-terminated ascii string
  char data[max_msg_length];
};

struct logger {
  static constexpr uint32_t max_msg_count = 512;

  // signature to find logs in memory easier
  // "hvloggerhvlogger"
  char signature[16];

  spin_lock lock;

  uint32_t msg_start;
  uint32_t msg_count;

  // the total messages sent
  uint64_t total_msg_count;

  // an array of messages
  logger_msg msgs[max_msg_count];
};

// initialize the logger
void logger_init();

// flush log messages to the provided buffer
void logger_flush(uint32_t& count, logger_msg* buffer);

// write a printf-style string to the logger using
// a limited subset of printf specifiers:
//   %s, %i, %d, %u, %x, %X, %p
void logger_write(char const* format, ...);

} // namespace hv

