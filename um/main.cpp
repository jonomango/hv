#include <iostream>
#include <Windows.h>

#include "hv.h"

inline constexpr uint32_t max_length = 32;

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
      if (buffer_idx >= max_length - 1)
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
        if (buffer_idx >= max_length - 1) {
          buffer[max_length - 1] = '\0';
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
        if (buffer_idx >= max_length - 1) {
          buffer[max_length - 1] = '\0';
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

static void logger_write(char const* const format, ...) {
  char str[max_length];

  // format the string
  // NOTE: this is VERY unsafe to do in root-mode... but it works
  va_list args;
  va_start(args, format);
  logger_format(str, format, args);
  va_end(args);

  printf("[%s]\n", str);
}

int main() {
  // check to see if the hypervisor is loaded...
  __try {
    if (hv::ping() != hv::hypervisor_signature) {
      printf("Failed to ping the hypervisor... :(\n");
      getchar();
      return 0;
    }
  } __except (1) {
    printf("Failed to ping the hypervisor... :(\n");
    getchar();
    return 0;
  }

  int info[4];
  __cpuidex(info, 0, 0);
  printf("Pinged the hypervisor! Flushing logs...\n");

  while (true) {
    uint32_t count = 128;
    hv::logger_msg msgs[128];
    hv::flush_logs(count, msgs);

    for (uint32_t i = 0; i < count; ++i)
      printf("[%u] %s\n", msgs[i].id, msgs[i].data);

    Sleep(10);
  }

  getchar();
}

