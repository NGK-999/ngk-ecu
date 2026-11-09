#include "command_line.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}
using Command = ecu::CommandLine;
Command::Result feed(Command& command, std::string_view bytes) {
    auto result = Command::Pending;
    for (char byte : bytes) result = command.push(byte);
    return result;
}
}

int main() {
    Command command;
    check(feed(command, "NGK STATUS\n") == Command::Ready,
          "complete printable command becomes ready");
    check(std::strcmp(command.value(), "NGK STATUS") == 0,
          "ready command is preserved without its newline");
    check(std::strcmp(command.value(), "NGK STATUS") == 0,
          "reading value does not consume a ready command");
    check(command.push('N') == Command::Pending && std::strcmp(command.value(), "N") == 0,
          "the next push starts a clean command");
    check(feed(command, "GK IDENTIFY\r\n") == Command::Ready &&
              std::strcmp(command.value(), "NGK IDENTIFY") == 0,
          "one trailing CR is accepted only with LF");
    check(command.push('\n') == Command::Pending,
          "empty LF cannot execute a command");
    check(feed(command, "\r\n") == Command::Pending,
          "empty CRLF cannot execute a command");

    check(feed(command, std::string_view("NGK STATUS\0 ERASE\n", 18)) == Command::Rejected,
          "NUL cannot hide a suffix after a valid command prefix");
    check(command.value()[0] == '\0', "rejected input exposes no valid prefix");
    check(command.push('\n') == Command::Pending,
          "one invalid line produces exactly one rejection");
    check(feed(command, "NGK STA\rTUS\n") == Command::Rejected,
          "internal CR cannot join separate command fragments");
    check(feed(command, "NGK STATUS\r\r\n") == Command::Rejected,
          "repeated CR is not an optional line ending");
    check(feed(command, "NGK STATUS\t\n") == Command::Rejected,
          "tab cannot be interpreted as printable command data");
    check(feed(command, std::string_view("NGK STATUS\x7F\n", 12)) == Command::Rejected,
          "DEL is rejected");
    check(feed(command, std::string_view("NGK STATUS\xFF\n", 12)) == Command::Rejected,
          "non-ASCII input is rejected even with signed char");

    const std::string maximum(95, 'X');
    check(feed(command, maximum + "\r\n") == Command::Ready &&
              std::strlen(command.value()) == 95,
          "all 95 printable bytes fit with a terminating null");
    check(feed(command, maximum + "X\n") == Command::Rejected,
          "a 96th byte rejects the whole line instead of executing a truncated prefix");
    check(command.value()[0] == '\0', "overflow does not expose a truncated command");
    check(feed(command, std::string(200, 'X')) == Command::Pending,
          "overflow is discarded while waiting for its newline");
    check(feed(command, "NGK STATUS\n") == Command::Rejected,
          "a suffix after overflow cannot become a separate valid command");
    check(feed(command, "NGK STATUS\n") == Command::Ready &&
              std::strcmp(command.value(), "NGK STATUS") == 0,
          "a fresh line recovers after rejected input");
    check(feed(command, "NGK STATUS\r") == Command::Pending,
          "a final CR alone does not execute a command");
    check(command.push('\n') == Command::Ready,
          "LF completes a previously received optional final CR");

    std::string printable;
    for (unsigned byte = 32; byte <= 126; ++byte) printable += char(byte);
    check(feed(command, printable + "\n") == Command::Ready &&
              std::strcmp(command.value(), printable.c_str()) == 0,
          "the complete printable ASCII range is accepted without trimming");
    for (unsigned byte = 0; byte <= 255; ++byte) {
        if ((byte >= 32 && byte <= 126) || byte == '\r' || byte == '\n') continue;
        Command invalid;
        feed(invalid, "NGK STATUS");
        check(invalid.push(char(byte)) == Command::Pending,
              "invalid bytes wait for the line delimiter");
        check(invalid.push('\n') == Command::Rejected,
              "every nonprintable or non-ASCII byte rejects the complete line");
        check(feed(invalid, "NGK STATUS\n") == Command::Ready,
              "a new valid line recovers after each invalid byte");
    }

    if (failures) return EXIT_FAILURE;
    std::puts("command line: printable framing, NUL/CR rejection and overflow recovery PASS");
    return EXIT_SUCCESS;
}
