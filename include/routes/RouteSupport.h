#pragma once

#include <nlohmann/json.hpp>
#include <string>

struct User;

namespace wfrest {
    class HttpReq;
    class HttpResp;
}

namespace route {
    const std::string &database_url();

    const std::string &jwt_secret();

    const std::string &auth_host();

    unsigned short auth_port();

    void respond_success(wfrest::HttpResp *resp, const char *msg, const nlohmann::json &data);

    void respond_error(wfrest::HttpResp *resp, const char *msg);

    bool check_token(const wfrest::HttpReq *req, User &user);
} // namespace route
