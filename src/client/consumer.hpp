#include "common/types.hpp"
#include "lf_queue.hpp"
#include <memory>

struct consumer {
  using shared_lf_queue_read =
      std::shared_ptr<lf_queue<MarketUpdate, Permissions::Read>>;

public:
  consumer(shared_lf_queue_read queue, uint64_t delay_per_update)
      : m_queue(queue), m_delay_per_update(delay_per_update) {}

private:
  shared_lf_queue_read m_queue;
  uint64_t m_delay_per_update;
};
