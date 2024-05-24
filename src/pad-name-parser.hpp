#pragma once
#include <optional>
#include <string_view>

struct ParsedJitsbinPad {
    std::string_view participant_id;
    std::string_view codec;
    uint32_t         ssrc;
};

auto parse_jitsibin_pad_name(const std::string_view name) -> std::optional<ParsedJitsbinPad>;
