
#include <atomic>
#include <new>
#include <vector>
// Adapted from https://rigtorp.se/ringbuffer/
template <typename T,
          size_t c_size = std::hardware_destructive_interference_size>
struct lf_queue {

public:
  lf_queue(size_t capacity) : m_data(capacity) {}

  auto push(T &&item) -> bool {
    const auto curr_index = m_write_pos.load(std::memory_order_relaxed);
    auto next_index = curr_index + 1;
    if (next_index == m_data.size()) {
      next_index = 0;
    }

    // check agains the cache before the atomic
    if (next_index == m_read_pos_cached) {
      m_read_pos_cached = m_read_pos.load(std::memory_order_acquire);
      if (next_index == m_read_pos_cached) {
        return false;
      }
    }

    m_data[curr_index] = std::forward<T>(item);

    m_write_pos.store(next_index, std::memory_order_release);
    return true;
  }

  auto pop(T &item) -> bool {
    const auto curr_index = m_read_pos.load(std::memory_order_relaxed);
    // check against cache first
    if (curr_index == m_write_pos_cached) {
      m_write_pos_cached = m_write_pos.load(std::memory_order_acquire);
      if (curr_index == m_write_pos_cached) {
        return false;
      }
    }
    item = m_data[curr_index];
    auto next_read_index = curr_index + 1;
    if (next_read_index == m_data.size()) {
      next_read_index = 0;
    }
    m_read_pos.store(next_read_index, std::memory_order_release);
    return true;
  }

private:
  std::vector<T> m_data;
  alignas(c_size) std::atomic<size_t> m_read_pos;
  alignas(c_size) size_t m_read_pos_cached;
  alignas(c_size) std::atomic<size_t> m_write_pos;
  alignas(c_size) size_t m_write_pos_cached;
};
