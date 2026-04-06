# ll_data_feed

A low-latency market data feed simulator written in modern C++23. The project demonstrates end-to-end UDP multicast publishing and receiving with a lock-free inter-thread pipeline, designed with the same techniques used in high-frequency trading (HFT) infrastructure.

---

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Components](#components)
  - [Common Types](#common-types)
  - [Network Layer — `udp_socket`](#network-layer--udp_socket)
  - [Server — `publisher`](#server--publisher)
  - [Client — `reader`](#client--reader)
  - [Client — `lf_queue`](#client--lf_queue)
  - [Client — `consumer`](#client--consumer)
- [Data Flow](#data-flow)
- [Building](#building)
- [Usage](#usage)
- [Design Decisions](#design-decisions)
- [Platform Notes](#platform-notes)

---

## Overview

`ll_data_feed` spins up a **publisher** that generates synthetic market tick data and multicasts it over UDP at a user-specified rate, and one or more **receivers** that listen on the same multicast group, deserialize the messages, and process them with minimal latency.

The key goals are:

- **Zero dynamic allocation on the hot path** — messages are passed through a pre-allocated lock-free ring buffer.
- **Nanosecond-resolution timestamps** — every outgoing packet is stamped with a `std::chrono::system_clock` value so round-trip latency can be measured.
- **Graceful shutdown** — `SIGINT` cleanly stops all threads and prints per-receiver diagnostic counters (packets received, packets dropped).

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                        main thread                       │
│                                                          │
│  publisher::start()  ──── UDP multicast ────►  reader   │
│  (busy-loop burst)         239.255.0.1:12345   (jthread)│
│                                                          │
│                                      │ lf_queue (SPSC)  │
│                                      ▼                   │
│                                  consumer                │
│                                  (jthread)               │
└─────────────────────────────────────────────────────────┘
```

The publisher runs on the main thread. Each receiver pair (reader + consumer) runs on two background `std::jthread`s and is connected by a lock-free single-producer / single-consumer (SPSC) ring buffer.

---

## Components

### Common Types

**`src/common/types.hpp`**

Defines the wire format for a single market update:

```cpp
struct MarketUpdate {
    uint64_t seq;                // monotonically increasing sequence number
    uint64_t send_timestamp_ns;  // UNIX timestamp of send time (nanoseconds)
    double   price;              // simulated mid-price (100.50 – 101.49)
    uint32_t size;               // lot size (always 20 in the simulation)
};
```

The struct is sent as raw bytes over the network (no serialisation library), so both sides must be compiled for the same platform and use the same struct layout.

---

### Network Layer — `udp_socket`

**`src/network/udp_socket.hpp`**

A RAII-managed, move-only UDP socket abstraction templated on `SocketType`:

| Template parameter | Role |
|--------------------|------|
| `SocketType::Sender` | Opens a UDP socket for sending. Exposes `write()`. |
| `SocketType::Receiever` | Opens a UDP socket for receiving. Exposes `bind()`, `join_multicast_group()`, `read()`. |

C++20 concepts (`Sender<T>`, `Receiver<T>`) gate the methods so misuse (e.g. calling `write` on a receiver socket) is a compile-time error.

Key methods:

| Method | Description |
|--------|-------------|
| `try_create()` | Factory; returns `std::expected<udp_socket, std::error_code>`. |
| `set_socket_reuse()` | Sets `SO_REUSEADDR` (and `SO_REUSEPORT` on macOS) so multiple processes can bind the same port for multicast. |
| `set_nonblocking()` | Puts the socket in non-blocking mode via `fcntl`. |
| `bind(port)` | Binds a receiver socket to `INADDR_ANY:port`. |
| `join_multicast_group(ip)` | Issues `IP_ADD_MEMBERSHIP` to subscribe to a multicast group. |
| `write(data, addr)` | Sends a raw byte span via `sendto`. |
| `read(buffer)` | Receives bytes via `recvfrom`. |

All error paths return `std::unexpected<std::error_code>` instead of throwing.

---

### Server — `publisher`

**`src/server/publisher.hpp` / `publisher.cpp`**

The publisher owns a `udp_socket<Sender>` and a destination `sockaddr_in` (the multicast address). It is configured with:

- **`numMessagesPerSecond`** — overall throughput target.
- **`burst_size`** — how many messages to send in each burst.

`publisher::start()` runs a **busy-polling loop** on the calling thread:

1. Compute `burst_interval_ns = (1 000 000 000 × burst_size) / rate` — the nanosecond gap between consecutive bursts.
2. At each burst time, emit `burst_size` `MarketUpdate` packets back-to-back via `sendto`.
3. After each burst, advance `next_burst_time` by `burst_interval_ns`. If the loop has fallen behind (e.g., OS jitter), `next_burst_time` is reset to `now` and a "Slowing Down" warning is printed.
4. When `m_stop_token` is set (via `publisher::stop()` called from the `SIGINT` handler), the loop exits and prints total sent / failed counts.

Messages are cast directly to `uint8_t*` with `std::span` — zero-copy serialisation.

---

### Client — `reader`

**`src/client/reader.hpp`**

A state-machine struct templated on `ReaderState` (`Initialized` → `Reading`). Transitioning state is enforced at compile time via requires-clauses.

**Setup (`ReaderState::Initialized`)**

`start_reading(port)`:
1. Binds the UDP socket to the given port.
2. Joins the multicast group `239.255.0.1`.
3. Creates a `kqueue` instance and registers two events:
   - `EVFILT_READ` on the socket fd — fires when a UDP datagram arrives.
   - `EVFILT_USER` on a synthetic `WAKEUP_EVENT_ID` — used as a shutdown signal.
4. Spawns a `std::jthread` (the *reading thread*) and returns `reader<ReaderState::Reading>`.

**Reading thread loop**

```
forever:
  kevent() ── blocks until a socket read or wakeup event fires
  if WAKEUP_EVENT_ID → break (stop was requested)
  if socket ready  → recvfrom() into a fixed stack buffer
                     reinterpret_cast to MarketUpdate*
                     sequence-gap check → increment diag.num_dropped
                     lf_queue::push(market_update)
                     increment diag.num_received
```

**Shutdown**

A `std::stop_callback` is registered on the `jthread`'s stop token. When `stop_reading()` is called it triggers the user-event in `kqueue`, waking the blocked `kevent()` call without any polling or spin.

**Diagnostics**

`get_diag()` returns a `diag{num_dropped, num_received}` snapshot. Dropped packets are detected by checking whether `market_update->seq != last_seq + 1`.

---

### Client — `lf_queue`

**`src/client/lf_queue.hpp`**

A lock-free SPSC ring buffer backed by a `std::vector<T>` of fixed capacity, shared via `std::shared_ptr<lf_queue_storage>`.

**Permission model**

| `Permissions` | Can push | Can pop | Obtained via |
|---------------|----------|---------|--------------|
| `Shared`      | ✓        | ✓       | Constructor  |
| `Write`       | ✓        | ✗       | `to_writer()` |
| `Read`        | ✗        | ✓       | `to_reader()` |

The `reader` holds a `Write` view; the `consumer` holds a `Read` view. Both share the same underlying `lf_queue_storage`.

**Storage layout** (`lf_queue_storage<T, c_size>`)

```
[ data vector ]
[ read_pos  ]  ← cache-line aligned  (atomic)
[ read_pos_cached ]                   (non-atomic, writer-local copy)
[ write_pos ]  ← cache-line aligned  (atomic)
[ write_pos_cached ]                  (non-atomic, reader-local copy)
```

Each position is aligned to `std::hardware_destructive_interference_size` (typically 64 bytes) to prevent **false sharing** between producer and consumer cores.

**`push(T&&)`** (writer only)

1. Load `write_pos` (relaxed).
2. Compute `next = (write_pos + 1) % capacity`.
3. If `next == read_pos_cached`, refresh from the atomic `read_pos` (acquire). If still equal → queue full → return `false`.
4. Write item, store `write_pos = next` (release).

**`pop(T&)`** (reader only)

1. Load `read_pos` (relaxed).
2. If `read_pos == write_pos_cached`, refresh from atomic `write_pos` (acquire). If still equal → queue empty → return `false`.
3. Move item out, advance `read_pos` (release).

The cached positions avoid an expensive atomic load on every operation when the queue is not near-full/near-empty.

---

### Client — `consumer`

**`src/client/consumer.hpp`**

A templated consumer parameterised by a `DurationLike` delay (default: `std::chrono::nanoseconds(1)`).

`start_consuming()` launches a `std::jthread` that:

1. Calls `lf_queue::pop()` in a tight loop.
2. Accumulates `market_update.price` into a running `sum`.
3. Busy-waits for `delay_per_update` after each message (simulating downstream processing work).
4. Prints a live counter `\r{sum}  Queue Size: {n}` to stdout.

`stop_consuming()` signals the stop source and joins the thread.

---

## Data Flow

```
publisher (main thread)
  │  MarketUpdate (raw bytes)
  │  sendto() → UDP multicast 239.255.0.1:12345
  ▼
[ network ]
  ▼
reader (jthread — kqueue I/O)
  │  recvfrom() → stack buffer
  │  reinterpret_cast<MarketUpdate*>
  │  sequence gap check
  │  lf_queue::push()
  ▼
lf_queue<MarketUpdate, SPSC> (ring buffer, capacity 100)
  ▼
consumer (jthread)
  │  lf_queue::pop()
  │  accumulate price
  │  busy-wait delay
  └─ print running sum + queue depth
```

---

## Building

**Requirements**

- C++23-capable compiler (Clang 17+ or GCC 13+ recommended)
- CMake 3.20+
- POSIX platform with `kqueue` support (macOS or FreeBSD)

**Steps**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The resulting binary is `build/ll_data_feed`.

---

## Usage

```
./ll_data_feed -m<messages_per_second> -b<burst_size>
```

Both flags support attached or space-separated values:

```bash
# 1 000 000 messages/sec, burst of 100
./ll_data_feed -m1000000 -b100

# Equivalent with spaces
./ll_data_feed -m 1000000 -b 100
```

| Flag | Description |
|------|-------------|
| `-m` | Total messages per second to publish |
| `-b` | Number of messages sent in each burst; controls granularity of pacing |

Press **Ctrl+C** to trigger a graceful shutdown. On exit, the program prints per-receiver statistics:

```
receiver: 00 had 0 packets dropped and 1000000 packets received
Publisher stopped. Total sent: 1000000, Total failed: 0
```

**How burst sizing affects pacing**

The inter-burst interval is computed as:

```
burst_interval_ns = (1_000_000_000 × burst_size) / messages_per_second
```

A smaller burst size (e.g. `-b1`) results in one message sent every `1 / rate` nanoseconds — the smoothest possible pacing. A larger burst size groups messages together with longer pauses between groups, which is more typical of real exchange feeds that batch updates per matching-engine cycle.

---

## Design Decisions

| Decision | Rationale |
|----------|-----------|
| **UDP multicast** | Broadcast to multiple consumers without per-connection overhead; mirrors real equity/futures exchange feeds. |
| **Busy-polling publisher** | Eliminates OS scheduler jitter on the send path; nanosecond-accurate pacing. |
| **`kqueue` for I/O** | Level-triggered edge notifications without spinning; allows clean shutdown via a synthetic user event without `pipe` tricks. |
| **Lock-free SPSC queue** | Single-producer (reader thread) / single-consumer (consumer thread) ownership avoids all mutex overhead; cache-line padding prevents false sharing. |
| **`std::expected` error handling** | No exceptions on any I/O path; all errors propagate as values and are handled explicitly. |
| **`std::jthread` + stop tokens** | Cooperative cancellation without `pthread_cancel`; pairs naturally with `std::stop_callback` to wake blocked syscalls. |
| **C++23 features** | `std::print`, `std::span`, `std::ranges`, `std::from_chars`, structured bindings, and `requires`-clauses are used throughout for clarity and safety. |

---

## Platform Notes

The reader uses `kqueue` (`sys/event.h`), which is available on **macOS and FreeBSD** but not on Linux. To port to Linux, replace the `kqueue` / `kevent` calls in `reader.hpp` with `epoll` equivalents (`epoll_create1`, `epoll_ctl`, `epoll_wait`), and replace the user-event wakeup with an `eventfd`.

The `SO_REUSEPORT` socket option is set only when the macro is defined (guarded by `#ifdef SO_REUSEPORT`), so the build does not break on platforms that lack it.
