#include "logger.h"
#include "hv.h"

#include <ntstrsafe.h>

namespace hv {

// initialize the logger
void logger_init() {
  auto& l = ghv.logger;

  memcpy(l.signature, "hvloggerhvlogger", 16);

  l.lock.initialize();
  l.msg_start = 0;
  l.msg_count = 0;
  l.total_msg_count = 0;

  logger_write("Logger initialized.");
}

// flush log messages to the provided buffer
void logger_flush(uint32_t& count, logger_msg* const buffer) {
  auto& l = ghv.logger;

  scoped_spin_lock lock(l.lock);

  count = min(count, l.msg_count);

  for (uint32_t i = 0; i < count; ++i) {
    // copy the message to the buffer
    buffer[i] = l.msgs[l.msg_start];

    // increment msg_start
    l.msg_start = (l.msg_start + 1) % l.max_msg_count;
  }

  l.msg_count -= count;
}

/**
 * C++ version 0.4 char* style "itoa":
 * Written by Lukás Chmela
 * Released under GPLv3.
 * https://stackoverflow.com/a/23840699
 */
static char* lukas_itoa(int value, char* result, int base) {
  // check that the base if valid
  if (base < 2 || base > 36) {
    *result = '\0';
    return result;
  }

  char* ptr = result, *ptr1 = result, tmp_char;
  int tmp_value;

  do {
    tmp_value = value;
    value /= base;
    *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"
      [35 + (tmp_value - value * base)];
  } while ( value );

  // Apply negative sign
  if (tmp_value < 0)
    *ptr++ = '-';

  *ptr-- = '\0';
  while(ptr1 < ptr) {
    tmp_char = *ptr;
    *ptr--= *ptr1;
    *ptr1++ = tmp_char;
  }

  return result;
}

// format a string into a logger buffer, using
// a limited subset of printf specifiers:
//   %s, %i, %d
static void logger_format(char* const buffer, char const* const format, va_list& args) {
  uint32_t buffer_idx = 0;
  uint32_t format_idx = 0;

  // true if the last character was a '%'
  bool specifying = false;

  while (true) {
    auto const c = format[format_idx++];

    // format end has been reached
    if (c == '\0')
      break;

    if (c == '%') {
      specifying = true;
      continue;
    }

    // just copy the character directly
    if (!specifying) {
      buffer[buffer_idx++] = c;

      // buffer end has been reached
      if (buffer_idx >= logger_msg::max_msg_length - 1)
        break;

      specifying = false;
      continue;
    }

    // format the string according to the specifier
    switch (c) {
    case 's': {
      auto const arg = va_arg(args, char const*);

      for (uint32_t i = 0; arg[i]; ++i) {
        buffer[buffer_idx++] = arg[i];

        // buffer end has been reached
        if (buffer_idx >= logger_msg::max_msg_length - 1) {
          buffer[logger_msg::max_msg_length - 1] = '\0';
          return;
        }
      }

      break;
    }
    case 'd':
    case 'i': {
      char int_buffer[128];

      // convert the int to a string
      auto const int_buffer_ptr = lukas_itoa(va_arg(args, int), int_buffer, 10);

      for (uint32_t i = 0; int_buffer_ptr[i]; ++i) {
        buffer[buffer_idx++] = int_buffer_ptr[i];

        // buffer end has been reached
        if (buffer_idx >= logger_msg::max_msg_length - 1) {
          buffer[logger_msg::max_msg_length - 1] = '\0';
          return;
        }
      }

      break;
    }
    case 'u': {
      break;
    }
    case 'X': {
      break;
    }
    case 'p': {
      break;
    }
    }

    specifying = false;
  }

  buffer[buffer_idx] = '\0';
}

// write a printf-style string to the logger
void logger_write(char const* const format, ...) {
  char str[logger_msg::max_msg_length];

  // format the string
  va_list args;
  va_start(args, format);
  logger_format(str, format, args);
  va_end(args);

  auto& l = ghv.logger;

  scoped_spin_lock lock(l.lock);

  auto& msg = l.msgs[(l.msg_start + l.msg_count) % l.max_msg_count];
  
  if (l.msg_count < l.max_msg_count)
    l.msg_count += 1;
  else
    l.msg_start = (l.msg_start + 1) % l.max_msg_count;

  // copy the string
  memset(msg.data, 0, msg.max_msg_length);
  for (size_t i = 0; (i < msg.max_msg_length - 1) && str[i]; ++i)
    msg.data[i] = str[i];

  // set the ID of the msg
  l.total_msg_count += 1;
  msg.id = l.total_msg_count;
}

} // namespace hv

