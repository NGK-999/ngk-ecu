#pragma once
#include "command_line.hpp"
#include <cstddef>
#include <cstring>

namespace ecu {
class SerialIngress {
public:
    static constexpr std::size_t byteBudget = 256;
    static constexpr std::size_t shortReplyReserve = 64;
    static constexpr std::size_t controlReplyReserve = 1024;

    // Single owner, just like CommandLine/SerialTxBuffer. A terminal line stays
    // in the parser until its entire reply can fit; no effect is retried and
    // later input cannot overtake a deferred command. Dispatch must emit at most
    // controlReplyReserve bytes (SIM1 success is silent; rejection fits 64).
    template<class Input, class Tx, class Dispatch, class Reject>
    std::size_t poll(Input& input, const Tx& tx, Dispatch dispatch, Reject reject) {
        std::size_t consumed = 0;
        for (;;) {
            if (pending_ != CommandLine::Pending) {
                // Capture switches to synchronous binary UART output. Keep the
                // preceding text queued ahead of it, even if another reply fits.
                if (pending_ == CommandLine::Ready && !std::strcmp(parser_.value(), "screen-capture") && tx.pending())
                    return consumed;
                const auto reserve = pending_ == CommandLine::Rejected ||
                    !std::strncmp(parser_.value(), "SIM1 ", 5) ? shortReplyReserve : controlReplyReserve;
                if (tx.free() < reserve) return consumed;
                if (pending_ == CommandLine::Ready) dispatch(parser_.value());
                else reject();
                pending_ = CommandLine::Pending;
            }
            if (consumed == byteBudget || !input.available()) return consumed;
            const auto value = input.read();
            if (value < 0) return consumed;
            ++consumed;
            pending_ = parser_.push(char(value));
        }
    }
private:
    CommandLine parser_;
    CommandLine::Result pending_ = CommandLine::Pending;
};
}
