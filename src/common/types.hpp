#pragma once

#include <cstdint>
struct MarketUpdate {
  uint64_t seq;
  uint64_t send_timestamp_ns;
  double price;
  uint32_t size;
};
