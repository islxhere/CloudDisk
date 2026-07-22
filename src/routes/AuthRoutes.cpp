#include "CloudDiskServer.h"
#include "Config.h"
#include "RouteSupport.h"

#include <nlohmann/json.hpp>
#include <workflow/HttpUtil.h>

#include <memory>

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

        const std::string consulUrl = Config::required("CLOUDDISK_CONSUL_URL") +
                                      "/v1/health/service/auth-service?passing=true";
        WFHttpTask *consulTask = WFTaskFactory::create_http_task(
            consulUrl, 0, 0,
            [resp, username, password, confirm](WFHttpTask *task) {
                if (task->get_state() != WFT_STATE_SUCCESS) {
                    resp->set_status(HttpStatusServiceUnavailable);
                    respond_error(resp, "认证服务暂不可用");
                    return;
                }

                std::string statusCode;
                if (!task->get_resp()->get_status_code(statusCode) || statusCode != "200") {
                    resp->set_status(HttpStatusServiceUnavailable);
                    respond_error(resp, "认证服务暂不可用");
                    return;
                }

                const void *body;
                size_t size;
                if (!task->get_resp()->get_parsed_body(&body, &size)) {
                    resp->set_status(HttpStatusServiceUnavailable);
                    respond_error(resp, "认证服务暂不可用");
                    return;
                }

                try {
                    const std::string responseBody = task->get_resp()->is_chunked()
                                                     ? protocol::HttpUtil::decode_chunked_body(task->get_resp())
                                                     : std::string(static_cast<const char *>(body), size);
                    const json services = json::parse(responseBody);
                    if (!services.is_array() || services.empty()) {
                        resp->set_status(HttpStatusServiceUnavailable);
                        respond_error(resp, "认证服务暂不可用");
                        return;
                    }

                    const json &service = services.front().at("Service");
                    const std::string address = service.at("Address").get<std::string>();
                    const unsigned short port = service.at("Port").get<unsigned short>();
                    if (address.empty() || port == 0) {
                        resp->set_status(HttpStatusServiceUnavailable);
                        respond_error(resp, "认证服务暂不可用");
                        return;
                    }

                    auto client = std::make_shared<Auth::SRPCClient>(address.c_str(), port);
                    srpc::SRPCClientTask *rpcTask = client->create_Register_task(
                        [resp, client](RegisterResponse *srpc_resp, srpc::RPCContext *ctx) {
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

                    RegisterRequest request;
                    request.set_username(username);
                    request.set_password(password);
                    request.set_confirm(confirm);
                    rpcTask->serialize_input(&request);
                    series_of(task)->push_back(rpcTask);
                } catch (const json::exception &) {
                    resp->set_status(HttpStatusServiceUnavailable);
                    respond_error(resp, "认证服务暂不可用");
                }
            });
        series->push_back(consulTask);
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

        const std::string consulUrl = Config::required("CLOUDDISK_CONSUL_URL") +
                                      "/v1/health/service/auth-service?passing=true";
        WFHttpTask *consulTask = WFTaskFactory::create_http_task(
            consulUrl, 0, 0,
            [resp, username, password](WFHttpTask *task) {
                if (task->get_state() != WFT_STATE_SUCCESS) {
                    resp->set_status(HttpStatusServiceUnavailable);
                    respond_error(resp, "认证服务暂不可用");
                    return;
                }

                std::string statusCode;
                if (!task->get_resp()->get_status_code(statusCode) || statusCode != "200") {
                    resp->set_status(HttpStatusServiceUnavailable);
                    respond_error(resp, "认证服务暂不可用");
                    return;
                }

                const void *body;
                size_t size;
                if (!task->get_resp()->get_parsed_body(&body, &size)) {
                    resp->set_status(HttpStatusServiceUnavailable);
                    respond_error(resp, "认证服务暂不可用");
                    return;
                }

                try {
                    const std::string responseBody = task->get_resp()->is_chunked()
                                                     ? protocol::HttpUtil::decode_chunked_body(task->get_resp())
                                                     : std::string(static_cast<const char *>(body), size);
                    const json services = json::parse(responseBody);
                    if (!services.is_array() || services.empty()) {
                        resp->set_status(HttpStatusServiceUnavailable);
                        respond_error(resp, "认证服务暂不可用");
                        return;
                    }

                    const json &service = services.front().at("Service");
                    const std::string address = service.at("Address").get<std::string>();
                    const unsigned short port = service.at("Port").get<unsigned short>();
                    if (address.empty() || port == 0) {
                        resp->set_status(HttpStatusServiceUnavailable);
                        respond_error(resp, "认证服务暂不可用");
                        return;
                    }

                    auto client = std::make_shared<Auth::SRPCClient>(address.c_str(), port);
                    srpc::SRPCClientTask *rpcTask = client->create_Login_task(
                        [resp, client](LoginResponse *srpc_resp, srpc::RPCContext *ctx) {
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

                    LoginRequest request;
                    request.set_username(username);
                    request.set_password(password);
                    rpcTask->serialize_input(&request);
                    series_of(task)->push_back(rpcTask);
                } catch (const json::exception &) {
                    resp->set_status(HttpStatusServiceUnavailable);
                    respond_error(resp, "认证服务暂不可用");
                }
            });
        series->push_back(consulTask);
    });
}
