#include "CloudDiskServer.h"
#include "Config.h"
#include "CryptoUtil.h"
#include "RouteSupport.h"
#include <nlohmann/json.hpp>

using namespace wfrest;
using json = nlohmann::json;

namespace route {
    const std::string &database_url() {
        static const std::string value = Config::required("CLOUDDISK_DB_URL");
        return value;
    }

    const std::string &jwt_secret() {
        static const std::string value = Config::required("CLOUDDISK_JWT_SECRET");
        return value;
    }

    void respond_success(HttpResp *resp, const char *msg, const json &data) {
        resp->set_header_pair("Content-Type", "application/json");
        json respJson;
        respJson["status"] = "success";
        respJson["message"] = msg;
        respJson["data"] = data;
        resp->Json(respJson.dump(2));
    }

    void respond_error(HttpResp *resp, const char *msg) {
        resp->set_header_pair("Content-Type", "application/json");
        json respJson;
        respJson["status"] = "error";
        respJson["message"] = msg;
        resp->Json(respJson.dump(2));
    }

    bool check_token(const HttpReq *req, User &user) {
        const std::string &token = req->header("Authorization");
        if (token.empty() || token.find("Bearer ") != 0) { return false; }
        const std::string authToken = token.substr(7);
        return CryptoUtil::verify_token(authToken, jwt_secret(), user);
    }
} // namespace route

void CloudDiskServer::register_routes() {
    register_www_module();
    register_auth_module();
    register_user_module();
    register_file_module();
}

void CloudDiskServer::register_www_module() {
    server_.Static("/", "./www/index.html");
    server_.Static("/static", "./www/static");
}
