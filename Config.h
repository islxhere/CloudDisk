#pragma once

#include <optional>
#include <string>
#include <string_view>

class Config {
public:
    static std::string required(std::string_view name);

    static std::optional<std::string> optional(std::string_view name);
};
