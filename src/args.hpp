#pragma once
#include <optional>
#include <string>
#include <vector>

struct ConnectionSpec {
    std::string server;
    std::string room;
    std::string nick;
};

struct Args {
    ConnectionSpec from;
    ConnectionSpec to;
    int            output_width  = 640;
    int            output_height = 360;

    // TODO: split debug flags for from/to
    std::vector<std::string> debug_flags;
    // TODO: allow setting "lws-loglevel-bitmap"
};

auto parse_args(int argc, char* argv[]) -> std::optional<Args>;
