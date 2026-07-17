#include "CloudDiskServer.h"
#include "CryptoUtil.h"
#include "RouteSupport.h"

#include <nlohmann/json.hpp>

using namespace wfrest;
using json = nlohmann::json;
using clouddisk::route::check_token;
using clouddisk::route::respond_error;
using clouddisk::route::respond_success;

void CloudDiskServer::register_user_module() {
    server_.GET("/api/v1/user/me", [](const HttpReq *req, HttpResp *resp) {
        User user;
        if (!check_token(req, user)) {
            resp->set_status(HttpStatusUnauthorized);
            respond_error(resp, "无效的访问令牌");
            return;
        }
        json data;
        data["userId"] = user.id;
        data["username"] = user.username;
        data["createdAt"] = user.createAt;
        resp->set_status(HttpStatusOK);
        respond_success(resp, "获取个人信息成功", data);
    });
}
