#include "logger.h"
#include "hv.h"

#include <ntstrsafe.h>

namespace hv {

// initialize the logger
void logger_init() {
  auto& l = ghv.logger;

  memcpy(l.signature, "hvloggerhvlogger", 16);

  l.msg_start = 0;
  l.msg_count = 0;
  l.total_msg_count = 0;
}

// flush log messages to the provided buffer
void logger_flush(uint32_t& count, logger_msg* const buffer) {
  auto& l = ghv.logger;

  // acquire the logger lock
  while (1 == InterlockedCompareExchange(&l.lock, 1, 0))
    _mm_pause();

  count = min(count, l.msg_count);

  for (uint32_t i = 0; i < count; ++i) {
    // copy the message to the buffer
    buffer[i] = l.msgs[l.msg_start];

    // increment msg_start
    l.msg_start = (l.msg_start + 1) % l.max_msg_count;
  }

  l.msg_count -= count;

  // release the logger lock
  l.lock = 0;
}

// write a printf-style string to the logger
void logger_write(char const* const format, ...) {
  char str[logger_msg::max_msg_length];

  // format the string
  // NOTE: this is VERY unsafe to do in root-mode... but it works
  va_list args;
  va_start(args, format);
  RtlStringCbVPrintfA(str, logger_msg::max_msg_length, format, args);
  va_end(args);

  auto& l = ghv.logger;

  // acquire the logger lock
  while (1 == InterlockedCompareExchange(&l.lock, 1, 0))
    _mm_pause();

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

  // release the logger lock
  l.lock = 0;
}

} // namespace hv

