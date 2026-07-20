#include "CloudDiskServer.h"
#include "CryptoUtil.h"
#include "RouteSupport.h"

#include <nlohmann/json.hpp>
#include <workflow/HttpUtil.h>
#include <workflow/MySQLResult.h>
#include <workflow/MySQLUtil.h>
#include <workflow/mysql_types.h>

using namespace protocol;
using namespace wfrest;
using json = nlohmann::json;
using route::database_url;
using route::jwt_secret;
using route::respond_error;
using route::respond_success;

void CloudDiskServer::register_auth_module() {
    server_.POST("/api/v1/auth/register", [](const HttpReq *req, HttpResp *resp) {
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

        if (password != confirm) {
            resp->set_status(HttpStatusBadRequest);
            respond_error(resp, "两次输入的密码不一致");
            return;
        }

        if (username.empty() || password.empty()) {
            resp->set_status(HttpStatusBadRequest);
            respond_error(resp, "用户名和密码不能为空");
            return;
        }

        std::string pwhash;
        try { pwhash = CryptoUtil::hash_password(password); } catch (const std::exception &) {
            resp->set_status(HttpStatusInternalServerError);
            respond_error(resp, "内部服务器错误");
            return;
        }

        const std::string insertSql =
                "insert into tbl_user(username, pwhash, tomb) values('" +
                MySQLUtil::escape_string(username) + "','" +
                MySQLUtil::escape_string(pwhash) + "', 0) "
                "on duplicate key update username = username";

        resp->MySQL(database_url(), insertSql, [resp, username](const MySQLResultCursor *cursor) {
            if (cursor->get_cursor_status() != MYSQL_STATUS_OK) {
                resp->set_status(HttpStatusInternalServerError);
                respond_error(resp, "内部服务器错误");
                return;
            }

            if (cursor->get_affected_rows() == 0) {
                resp->set_status(HttpStatusConflict);
                respond_error(resp, "用户名已存在");
                return;
            }

            if (cursor->get_affected_rows() != 1) {
                resp->set_status(HttpStatusInternalServerError);
                respond_error(resp, "内部服务器错误");
                return;
            }

            json data;
            data["userId"] = cursor->get_insert_id();
            data["username"] = username;
            resp->set_status(HttpStatusCreated);
            respond_success(resp, "注册成功", data);
        });
    });

    server_.POST("/api/v1/auth/login", [](const HttpReq *req, HttpResp *resp) {
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

        const std::string sql =
                "select * from tbl_user where username = '" +
                MySQLUtil::escape_string(username) + "' and tomb = 0";
        resp->MySQL(database_url(), sql, [resp, password](MySQLResultCursor *cursor) {
            if (cursor->get_cursor_status() != MYSQL_STATUS_GET_RESULT) {
                resp->set_status(HttpStatusInternalServerError);
                respond_error(resp, "内部服务器错误");
                return;
            }

            if (cursor->get_rows_count() != 1) {
                resp->set_status(HttpStatusUnauthorized);
                respond_error(resp, "用户名或密码错误");
                return;
            }

            std::vector<MySQLCell> record;
            if (!cursor->fetch_row(record)) {
                resp->set_status(HttpStatusInternalServerError);
                respond_error(resp, "内部服务器错误");
                return;
            }

            User user;
            user.id = record.at(0).as_int();
            user.username = record.at(1).as_string();
            user.pwhash = record.at(2).as_string();
            user.createAt = record.at(3).as_datetime();
            if (!CryptoUtil::verify_password(password, user.pwhash)) {
                resp->set_status(HttpStatusUnauthorized);
                respond_error(resp, "用户名或密码错误");
                return;
            }

            json data;
            data["accessToken"] = CryptoUtil::generate_token(user, jwt_secret());
            data["tokenType"] = "Bearer";
            data["user"]["userId"] = user.id;
            data["user"]["username"] = user.username;
            resp->set_status(HttpStatusOK);
            respond_success(resp, "登录成功", data);
        });
    });
}
