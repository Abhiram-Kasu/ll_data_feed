import argparse
import socket
import struct
import sys


def main():
    parser = argparse.ArgumentParser(description="Multicast UDP Subscriber for testing")
    parser.add_argument(
        "--ip", type=str, default="239.255.0.1", help="Multicast IP address"
    )
    parser.add_argument("--port", type=int, default=12345, help="Port to listen on")
    parser.add_argument(
        "--count",
        type=int,
        default=10,
        help="Number of messages to read before exiting",
    )
    args = parser.parse_args()

    # Create UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)

    # Allow multiple processes to bind to the same port
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    # Bind to all interfaces on the specified port
    try:
        sock.bind(("", args.port))
    except Exception as e:
        print(f"Failed to bind to port {args.port}: {e}")
        sys.exit(1)

    # Join the multicast group
    try:
        mreq = struct.pack("4sl", socket.inet_aton(args.ip), socket.INADDR_ANY)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    except Exception as e:
        print(f"Failed to join multicast group {args.ip}: {e}")
        sys.exit(1)

    print(f"Listening on {args.ip}:{args.port}...")
    print(f"Expected format: [seq (8b)] [timestamp_ns (8b)] [price (8b)] [size (4b)]\n")

    # The C++ struct MarketUpdate has:
    # uint64_t seq; (8 bytes)
    # uint64_t send_timestamp_ns; (8 bytes)
    # double price; (8 bytes)
    # uint32_t size; (4 bytes)
    # Total: 28 bytes
    STRUCT_FORMAT = "<QQdI"  # < means little-endian, Q=uint64, d=double, I=uint32

    for i in range(args.count):
        try:
            data, addr = sock.recvfrom(1024)

            if len(data) == 28:
                unpacked = struct.unpack(STRUCT_FORMAT, data)
                seq = unpacked[0]
                ts_ns = unpacked[1]
                price = unpacked[2]
                size = unpacked[3]

                print(
                    f"[{i + 1}/{args.count}] Received from {addr}: "
                    f"seq={seq}, ts_ns={ts_ns}, price={price:.2f}, size={size}"
                )
            else:
                print(
                    f"[{i + 1}/{args.count}] Received {len(data)} bytes (expected 28 bytes). "
                    f"Raw hex: {data.hex()}"
                )
        except KeyboardInterrupt:
            print("\nExiting...")
            break


if __name__ == "__main__":
    main()
