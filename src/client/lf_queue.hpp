#pragma once
#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <utility>
#include <vector>

enum class Permissions { Read, Write, Shared };

template <Permissions P>
concept IsReader = P == Permissions::Read;

template <Permissions P>
concept IsWriter = P == Permissions::Write;

template <Permissions P>
concept IsShared = P == Permissions::Shared;

// shared
template <typename T, size_t c_size> struct lf_queue_storage {
  explicit lf_queue_storage(size_t capacity)
      : data(capacity), read_pos(0), read_pos_cached(0), write_pos(0),
        write_pos_cached(0) {}

  std::vector<T> data;

  alignas(c_size) std::atomic<size_t> read_pos;
  alignas(c_size) size_t read_pos_cached;

  alignas(c_size) std::atomic<size_t> write_pos;
  alignas(c_size) size_t write_pos_cached;
};

template <typename T, Permissions P = Permissions::Shared,
          size_t c_size = std::hardware_destructive_interference_size>
class lf_queue {
public:
  using storage_t = lf_queue_storage<T, c_size>;

  // Shared constructor (owns storage)
  explicit lf_queue(size_t capacity)
    requires IsShared<P>
      : m_storage(std::make_shared<storage_t>(capacity)) {}

  // Internal constructor for views
  explicit lf_queue(std::shared_ptr<storage_t> storage)
      : m_storage(std::move(storage)) {}

  // Move ctor (important for performance-sensitive code)
  lf_queue(lf_queue &&other) noexcept = default;
  lf_queue &operator=(lf_queue &&other) noexcept = default;

  lf_queue(const lf_queue &) = default;
  lf_queue &operator=(const lf_queue &) = default;

  auto push(T &&item) -> bool
    requires(not IsReader<P>)
  {
    auto &s = *m_storage;

    const auto curr = s.write_pos.load(std::memory_order_relaxed);
    auto next = curr + 1;
    if (next == s.data.size())
      next = 0;

    if (next == s.read_pos_cached) {
      s.read_pos_cached = s.read_pos.load(std::memory_order_acquire);
      if (next == s.read_pos_cached)
        return false;
    }

    s.data[curr] = std::forward<T>(item);
    s.write_pos.store(next, std::memory_order_release);
    return true;
  }

  auto pop(T &item) -> bool
    requires(not IsWriter<P>)
  {
    auto &s = *m_storage;

    const auto curr = s.read_pos.load(std::memory_order_relaxed);

    if (curr == s.write_pos_cached) {
      s.write_pos_cached = s.write_pos.load(std::memory_order_acquire);
      if (curr == s.write_pos_cached)
        return false;
    }

    item = std::move(s.data[curr]);

    auto next = curr + 1;
    if (next == s.data.size())
      next = 0;

    s.read_pos.store(next, std::memory_order_release);
    return true;
  }

  auto to_reader() const noexcept
    requires IsShared<P>
  {
    return lf_queue<T, Permissions::Read, c_size>{m_storage};
  }

  auto to_writer() const noexcept
    requires IsShared<P>
  {
    return lf_queue<T, Permissions::Write, c_size>{m_storage};
  }

  auto size() const noexcept -> size_t {
    auto &s = *m_storage;

    const auto w = s.write_pos.load(std::memory_order_acquire);
    const auto r = s.read_pos.load(std::memory_order_acquire);

    if (w >= r)
      return w - r;
    return s.data.size() - (r - w);
  }

  
  constexpr auto getCapacity() -> size_t {
    
  }
private:
  std::shared_ptr<storage_t> m_storage;
};
