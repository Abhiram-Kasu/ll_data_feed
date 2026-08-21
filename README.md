# ll_data_feed

Low-latency C++23 multicast market data feed demo with a publisher, UDP reader, and lock-free queue consumer pipeline.

## What it does

- Publishes `MarketUpdate` messages over UDP multicast
- Reads multicast packets on one or more listeners
- Pushes updates through a lock-free single-producer/single-consumer style queue
- Consumes updates on a worker thread and reports queue/stream activity

## Message format

`MarketUpdate` contains:
- `seq` (sequence number)
- `send_timestamp_ns` (send time in nanoseconds)
- `price`
- `size`

## Build

Requirements:
- CMake 3.20+
- C++23 compiler
- BSD/macOS-compatible `kqueue` support (reader uses `sys/event.h`)

Build commands:

```bash
cmake -S . -B build
cmake --build build
```

## Run

The executable expects message rate and burst size:

```bash
./build/ll_data_feed -m100000 -b100
```

- `-m`: messages per second
- `-b`: burst size per send cycle

Press `Ctrl+C` to stop gracefully and print receiver diagnostics.

## Project layout

- `src/server`: publisher logic
- `src/client`: reader, consumer, and lock-free queue
- `src/network`: UDP socket abstraction
- `src/common`: shared types
