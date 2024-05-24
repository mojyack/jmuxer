#include "args.hpp"
#include "macros/unwrap.hpp"
#include "util/assert.hpp"
#include "util/charconv.hpp"
#include "util/misc.hpp"

auto usage = R"(jmuxer - Jitsi Meet multiplexer
Usage:
jmuxer [FLAGS] [OPTIONS]
FLAGS:
    -h --help                               Print this message
OPTIONS:
    -i --from <{server}/{room}/{nick}>      Where jmuxer takes streams from (Required)
    -o --to <{server}/{room}/{nick}>        Where jmuxer streams to (Required)
    -s --output-size <{width}x{height}>     Output resolution (Default: 640x360)
    --enable-debug <{flag1},{flag2},...>    Array of active debug flags of jitsibin
)";

auto parse_connection_spec(const std::string_view str) -> std::optional<ConnectionSpec> {
    const auto elms = split(str, "/");
    assert_o(elms.size() == 3);
    return ConnectionSpec{
        .server = std::string(elms[0]),
        .room   = std::string(elms[1]),
        .nick   = std::string(elms[2]),
    };
}

auto parse_output_size(const std::string_view str) -> std::optional<std::pair<int, int>> {
    const auto elms = split(str, "x");
    assert_o(elms.size() == 2);
    unwrap_oo(width, from_chars<int>(elms[0]));
    unwrap_oo(height, from_chars<int>(elms[1]));
    return std::make_pair(width, height);
}

auto parse_args(const int argc, char* argv[]) -> std::optional<Args> {
    auto args  = Args{};
    auto index = 1;

    auto next_arg = [argc, argv, &index]() -> std::optional<std::string_view> {
        assert_o(index < argc);
        const auto ret = argv[index];
        index += 1;
        return ret;
    };
    auto found_from = false;
    auto found_to   = false;

    while(index < argc) {
        unwrap_oo(arg, next_arg());
        if(arg == "-h" || arg == "--help") {
            print(usage);
            std::exit(0);
        } else if(arg == "-i" || arg == "--from") {
            unwrap_oo(str, next_arg());
            unwrap_oo(spec, parse_connection_spec(str));
            args.from  = spec;
            found_from = true;
        } else if(arg == "-o" || arg == "--to") {
            unwrap_oo(str, next_arg());
            unwrap_oo(spec, parse_connection_spec(str));
            args.to  = spec;
            found_to = true;
        } else if(arg == "-s" || arg == "--output-size") {
            unwrap_oo(str, next_arg());
            unwrap_oo(size, parse_output_size(str));
            args.output_width  = size.first;
            args.output_height = size.second;
        } else if(arg == "--enable-debug") {
            unwrap_oo(str, next_arg());
            for(const auto flag : split(str, ",")) {
                args.debug_flags.emplace_back(flag);
            }
        } else {
            print("invalid option");
            print(usage);
            std::exit(1);
        }
    }

    assert_o(found_from, "--from option required");
    assert_o(found_to, "--to option required");

    return args;
}
