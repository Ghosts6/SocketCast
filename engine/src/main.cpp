#include "socketcast/transport.hpp"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

std::atomic<socketcast::Transport*> g_transport{nullptr};

void on_signal(int) {
    auto* t = g_transport.load();
    if (t) {
        t->stop();
    }
}

void usage() {
    std::cerr << "usage:\n"
              << "  socketcast_engine listen [--bind ADDR] [--port N]\n"
              << "  socketcast_engine send --host ADDR [--port N] [--count N] [--size N]\n";
}

bool take_arg(int& i, int argc, char** argv, std::string& out) {
    if (i + 1 >= argc) {
        return false;
    }
    out = argv[++i];
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::string mode = "listen";
    std::string bind_addr = "0.0.0.0";
    std::string host = "127.0.0.1";
    uint16_t port = 5000;
    uint32_t count = 100;
    uint16_t size = 64;

    int i = 1;
    if (i < argc && argv[i][0] != '-') {
        mode = argv[i++];
    }
    for (; i < argc; ++i) {
        const std::string a = argv[i];
        std::string val;
        if (a == "--bind" && take_arg(i, argc, argv, val)) {
            bind_addr = val;
        } else if (a == "--host" && take_arg(i, argc, argv, val)) {
            host = val;
        } else if (a == "--port" && take_arg(i, argc, argv, val)) {
            port = static_cast<uint16_t>(std::stoi(val));
        } else if (a == "--count" && take_arg(i, argc, argv, val)) {
            count = static_cast<uint32_t>(std::stoul(val));
        } else if (a == "--size" && take_arg(i, argc, argv, val)) {
            size = static_cast<uint16_t>(std::stoi(val));
        } else if (a == "--help" || a == "-h") {
            usage();
            return 0;
        } else {
            usage();
            return 2;
        }
    }

    try {
        std::cout << std::unitbuf;
        if (mode == "listen") {
            socketcast::Transport transport(bind_addr, port);
            std::cout << "SocketCast listen " << bind_addr << ":" << transport.bound_port()
                      << " (Ctrl+C to stop)\n";
            transport.set_rx_callback([](const socketcast::Packet& pkt,
                                         const socketcast::Session::Stats& stats) {
                using socketcast::PacketType;
                using socketcast::kFlagAck;
                using socketcast::kFlagSyn;
                if (pkt.header.packet_type == PacketType::Handshake &&
                    (pkt.header.flags & kFlagSyn) && !(pkt.header.flags & kFlagAck)) {
                    std::cout << "handshake syn stream_id=" << pkt.header.stream_id << "\n";
                }
                if (pkt.header.packet_type == PacketType::Data) {
                    std::cout << "received seq=" << pkt.header.sequence_number
                              << " total=" << stats.data_received << "\n";
                }
            });
            g_transport = &transport;
            std::signal(SIGINT, on_signal);
            std::signal(SIGTERM, on_signal);
            transport.run();
            if (const auto* s = transport.stats()) {
                std::cout << "received=" << s->data_received << " nacks=" << s->nacks_sent << "\n";
            } else {
                std::cout << "received=0 (no session)\n";
            }
            return 0;
        }
        if (mode == "send") {
            socketcast::Transport transport("0.0.0.0", 0);
            transport.configure_send(host, port, count, size);
            std::cout << "SocketCast send " << host << ":" << port << " count=" << count << "\n";
            transport.set_rx_callback([](const socketcast::Packet& pkt,
                                         const socketcast::Session::Stats& stats) {
                if (pkt.header.packet_type == socketcast::PacketType::Ack) {
                    std::cout << "acked seq=" << pkt.header.sequence_number
                              << " total=" << stats.acked << "\n";
                }
            });
            g_transport = &transport;
            std::signal(SIGINT, on_signal);
            std::signal(SIGTERM, on_signal);
            transport.run();
            const auto* s = transport.stats();
            if (!s) {
                return 1;
            }
            std::cout << "sent=" << s->data_sent << " acked=" << s->acked
                      << " retransmits=" << s->retransmits << "\n";
            return (transport.send_complete() && !transport.send_failed()) ? 0 : 1;
        }
        usage();
        return 2;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
