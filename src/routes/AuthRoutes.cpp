#include "CloudDiskServer.h"
#include "RouteSupport.h"

#include <nlohmann/json.hpp>
#include <workflow/HttpUtil.h>

using namespace wfrest;
using json = nlohmann::json;
using route::respond_error;
using route::respond_success;

void CloudDiskServer::register_auth_module() {
    server_.POST("/api/v1/auth/register", [this](const HttpReq *req, HttpResp *resp, SeriesWork *series) {
        std::string username;
        std::string password;
        std::string confirm;
        if (req->content_type() != APPLICATION_JSON) {
            resp->set_status(HttpStatusBadRequest);
            respond_error(resp, "请求格式有误");
            return;
        }

        try {
            json reqJson = json::parse(req->body());
            username = reqJson.at("username").get<std::string>();
            password = reqJson.at("password").get<std::string>();
            confirm = reqJson.at("confirm").get<std::string>();
        } catch (json::exception &) {
            resp->set_status(HttpStatusBadRequest);
            respond_error(resp, "请求格式有误");
            return;
        }

        srpc::SRPCClientTask *task = srpc_client_.create_Register_task(
            [resp](RegisterResponse *srpc_resp, srpc::RPCContext *ctx) {
                if (!ctx->success()) {
                    resp->set_status(HttpStatusInternalServerError);
                    respond_error(resp, "内部服务器错误");
                    return;
                }

                switch (srpc_resp->result()) {
                    case AUTH_RESULT_SUCCESS: {
                        json data;
                        data["userId"] = srpc_resp->userid();
                        data["username"] = srpc_resp->username();
                        resp->set_status(HttpStatusCreated);
                        respond_success(resp, "注册成功", data);
                        break;
                    }
                    case AUTH_RESULT_PASSWORD_MISMATCH:
                        resp->set_status(HttpStatusBadRequest);
                        respond_error(resp, "两次输入的密码不一致");
                        break;
                    case AUTH_RESULT_EMPTY_CREDENTIALS:
                        resp->set_status(HttpStatusBadRequest);
                        respond_error(resp, "用户名和密码不能为空");
                        break;
                    case AUTH_RESULT_USERNAME_EXISTS:
                        resp->set_status(HttpStatusConflict);
                        respond_error(resp, "用户名已存在");
                        break;
                    case AUTH_RESULT_INTERNAL_ERROR:
                    case AUTH_RESULT_UNSPECIFIED:
                    default:
                        resp->set_status(HttpStatusInternalServerError);
                        respond_error(resp, "内部服务器错误");
                        break;
                }
            });

        RegisterRequest register_req;
        register_req.set_username(username);
        register_req.set_password(password);
        register_req.set_confirm(confirm);

        task->serialize_input(&register_req);
        series->push_back(task);
    });

    server_.POST("/api/v1/auth/login", [this](const HttpReq *req, HttpResp *resp, SeriesWork *series) {
        std::string username;
        std::string password;
        if (req->content_type() != APPLICATION_JSON) {
            resp->set_status(HttpStatusBadRequest);
            respond_error(resp, "请求格式有误");
            return;
        }

        try {
            json reqJson = json::parse(req->body());
            username = reqJson.at("username").get<std::string>();
            password = reqJson.at("password").get<std::string>();
        } catch (json::exception &) {
            resp->set_status(HttpStatusBadRequest);
            respond_error(resp, "请求格式有误");
            return;
        }

        srpc::SRPCClientTask *task = srpc_client_.create_Login_task(
            [resp](LoginResponse *srpc_resp, srpc::RPCContext *ctx) {
                if (!ctx->success()) {
                    resp->set_status(HttpStatusInternalServerError);
                    respond_error(resp, "内部服务器错误");
                    return;
                }

                switch (srpc_resp->result()) {
                    case AUTH_RESULT_SUCCESS: {
                        json data;
                        data["accessToken"] = srpc_resp->accesstoken();
                        data["tokenType"] = srpc_resp->tokentype();
                        data["user"]["userId"] = srpc_resp->userid();
                        data["user"]["username"] = srpc_resp->username();
                        resp->set_status(HttpStatusOK);
                        respond_success(resp, "登录成功", data);
                        break;
                    }
                    case AUTH_RESULT_INVALID_CREDENTIALS:
                        resp->set_status(HttpStatusUnauthorized);
                        respond_error(resp, "用户名或密码错误");
                        break;
                    case AUTH_RESULT_INTERNAL_ERROR:
                    case AUTH_RESULT_UNSPECIFIED:
                    default:
                        resp->set_status(HttpStatusInternalServerError);
                        respond_error(resp, "内部服务器错误");
                        break;
                }
            });

        LoginRequest login_req;
        login_req.set_username(username);
        login_req.set_password(password);

        task->serialize_input(&login_req);
        series->push_back(task);
    });
}
