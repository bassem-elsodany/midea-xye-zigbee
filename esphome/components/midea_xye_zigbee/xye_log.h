#pragma once


#include <cstdio>
#include <cstdint>
#include <cstddef>
#include "esphome/core/log.h"

namespace esphome {
namespace midea {
namespace xye {

/// Compact one-line hex dump for bus frames (grep for ">>>" / "<<<" in logs).
inline void log_frame_hex(const char *tag, const char *arrow, const uint8_t *data, size_t len,
                          int level = ESPHOME_LOG_LEVEL_DEBUG) {
  char buf[128];
  size_t pos = 0;
  for (size_t i = 0; i < len && pos + 4 < sizeof(buf); i++) {
    if (i > 0)
      buf[pos++] = ':';
    pos += snprintf(buf + pos, sizeof(buf) - pos, "%02X", data[i]);
  }
  buf[pos] = '\0';
  ::esphome::esp_log_printf_(level, tag, __LINE__, ESPHOME_LOG_FORMAT("%s %s"), arrow, buf);
}

}  // namespace xye
}  // namespace midea
}  // namespace esphome
