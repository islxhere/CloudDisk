#include "Config.h"

#include <cstdlib>
#include <stdexcept>

std::optional<std::string> Config::optional(std::string_view name) {
    const std::string key(name);
    const char *value = std::getenv(key.c_str());
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return value;
}

std::string Config::required(std::string_view name) {
    std::optional<std::string> value = optional(name);
    if (!value) {
        throw std::runtime_error("缺少必填环境变量: " + std::string(name));
    }
    return *value;
}
