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
template <typename T>
static char* lukas_itoa(T value, char* result, int base, bool upper = false) {
  // check that the base if valid
  if (base < 2 || base > 36) {
    *result = '\0';
    return result;
  }

  char* ptr = result, *ptr1 = result, tmp_char;
  T tmp_value;

  if (upper) {
    do {
      tmp_value = value;
      value /= base;
      *ptr++ = "ZYXWVUTSRQPONMLKJIHGFEDCBA9876543210123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        [35 + (tmp_value - value * base)];
    } while ( value );
  } else {
    do {
      tmp_value = value;
      value /= base;
      *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"
        [35 + (tmp_value - value * base)];
    } while ( value );
  }

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

// copy the src string to the logger format buffer
static bool logger_format_copy_str(char* const buffer, char const* const src, uint32_t& idx) {
  for (uint32_t i = 0; src[i]; ++i) {
    buffer[idx++] = src[i];

    // buffer end has been reached
    if (idx >= logger_msg::max_msg_length - 1) {
      buffer[logger_msg::max_msg_length - 1] = '\0';
      return true;
    }
  }

  return false;
}

// format a string into a logger buffer, using
// a limited subset of printf specifiers:
//   %s, %i, %d, %u, %x, %X, %p
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

    char fmt_buffer[128];

    // format the string according to the specifier
    switch (c) {
    case 's': {
      if (logger_format_copy_str(buffer, va_arg(args, char const*), buffer_idx))
        return;
      break;
    }
    case 'd':
    case 'i': {
      if (logger_format_copy_str(buffer,
          lukas_itoa(va_arg(args, int), fmt_buffer, 10), buffer_idx))
        return;
      break;
    }
    case 'u': {
      if (logger_format_copy_str(buffer,
          lukas_itoa(va_arg(args, unsigned int), fmt_buffer, 10), buffer_idx))
        return;
      break;
    }
    case 'x': {
      if (logger_format_copy_str(buffer,
          lukas_itoa(va_arg(args, unsigned int), fmt_buffer, 16), buffer_idx))
        return;
      break;
    }
    case 'X': {
      if (logger_format_copy_str(buffer,
          lukas_itoa(va_arg(args, unsigned int), fmt_buffer, 16, true), buffer_idx))
        return;
      break;
    }
    case 'p': {
      if (logger_format_copy_str(buffer,
          lukas_itoa(va_arg(args, uint64_t), fmt_buffer, 16, true), buffer_idx))
        return;
      break;
    }
    }

    specifying = false;
  }

  buffer[buffer_idx] = '\0';
}

// write a printf-style string to the logger using
// a limited subset of printf specifiers:
//   %s, %i, %d, %u, %x, %X, %p
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

  l.total_msg_count += 1;

  // set the metadata info about this message
  msg.id  = l.total_msg_count;
  msg.tsc = __rdtscp(&msg.aux);
}

} // namespace hv

