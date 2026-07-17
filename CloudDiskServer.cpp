#include "CloudDiskServer.h"
#include "Config.h"
#include "CryptoUtil.h"
#include "OssManager.h"
#include <nlohmann/json.hpp>
#include <wfrest/PathUtil.h>
#include <workflow/HttpUtil.h>
#include <workflow/MySQLResult.h>
#include <workflow/MySQLUtil.h>
#include <workflow/mysql_types.h>

using namespace protocol;
using namespace wfrest;
using json = nlohmann::json;
namespace fs = std::filesystem;

// static const int RetryMax = 3;

static const std::string &database_url() {
    static const std::string value = Config::required("CLOUDDISK_DB_URL");
    return value;
}

static const std::string &jwt_secret() {
    static const std::string value = Config::required("CLOUDDISK_JWT_SECRET");
    return value;
}

static void respond_success(HttpResp *resp, const char *msg, const json &data) {
    resp->set_header_pair("Content-Type", "application/json");
    json respJson;
    respJson["status"] = "success";
    respJson["message"] = msg;
    respJson["data"] = data;
    resp->Json(respJson.dump(2));
}

static void respond_error(HttpResp *resp, const char *msg) {
    resp->set_header_pair("Content-Type", "application/json");
    json respJson;
    respJson["status"] = "error";
    respJson["message"] = msg;
    resp->Json(respJson.dump(2));
}

static bool check_token(const HttpReq *req, User &user) {
    const std::string &token = req->header("Authorization");
    if (token.empty() || token.find("Bearer ") != 0) { return false; }
    const std::string authToken = token.substr(7);
    return CryptoUtil::verify_token(authToken, jwt_secret(), user);
}

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


static bool putFromMem(const std::string &bucket, const std::string &osspath, const std::string &content,
                       OssClient *ossClient) {
    std::shared_ptr<std::iostream> stream = std::make_shared<std::stringstream>(content);
    PutObjectRequest request{bucket, osspath, stream};
    auto outcome = ossClient->PutObject(request);

    if (!outcome.isSuccess()) {
        std::cout << "PutObject FAILED"
                << ", code:" << outcome.error().Code()
                << ", message:" << outcome.error().Message()
                << ", requestId:" << outcome.error().RequestId() << std::endl;
        return false;
    }
    return true;
}

void CloudDiskServer::register_file_module() {
    server_.GET("/api/v1/files", [](const HttpReq *req, HttpResp *resp) {
        User user;
        if (!check_token(req, user)) {
            resp->set_status(HttpStatusUnauthorized);
            respond_error(resp, "无效的访问令牌");
            return;
        }
        const std::string sql = "select * from tbl_file where uid = " + std::to_string(user.id);
        resp->MySQL(database_url(), sql, [resp](MySQLResultCursor *cursor) {
            if (cursor->get_cursor_status() != MYSQL_STATUS_GET_RESULT) {
                resp->set_status(HttpStatusInternalServerError);
                respond_error(resp, "内部服务器错误");
                return;
            }

            json files = json::array();
            std::vector<MySQLCell> record;
            while (cursor->fetch_row(record)) {
                json file = json::object();
                file["fileId"] = record.at(0).as_int();
                file["filename"] = record.at(2).as_string();
                file["size"] = record.at(4).as_int();
                file["createdAt"] = record.at(5).as_datetime();
                file["updatedAt"] = record.at(6).as_datetime();
                files.push_back(file);
            }
            json data;
            data["files"] = files;
            resp->set_status(HttpStatusOK);
            respond_success(resp, "获取文件列表成功", data);
        });
    });

    server_.POST("/api/v1/files", [](const HttpReq *req, HttpResp *resp) {
        User user;
        if (!check_token(req, user)) {
            resp->set_status(HttpStatusUnauthorized);
            respond_error(resp, "无效的访问令牌");
            return;
        }

        if (req->content_type() != MULTIPART_FORM_DATA) {
            resp->set_status(HttpStatusBadRequest);
            respond_error(resp, "请求格式有误");
            return;
        }

        Form &form = req->form();
        // todo: 先只上传一个文件进行测试，后续改多文件上传测试
        if (form.size() != 1) {
            resp->set_status(HttpStatusBadRequest);
            respond_error(resp, "请求格式有误");
            return;
        }

        const auto &[fname, fcontent] = form.begin()->second;
        const std::string &filename = fname;
        const std::string &content = fcontent;
        const std::string basename = PathUtil::base(filename);
        if (basename.empty()) {
            resp->set_status(HttpStatusBadRequest);
            respond_error(resp, "请求格式有误");
            return;
        }

        // std::string hashcode = CryptoUtil::generate_hashcode(content.c_str(), content.size());
        std::string hashcode;
        try {
            // generate_hashcode会抛出异常
            hashcode = CryptoUtil::generate_hashcode(content.c_str(), content.size());
        } catch (std::exception &) {
            resp->set_status(HttpStatusInternalServerError);
            respond_error(resp, "内部服务器错误");
            return;
        }

        // std::string storage_path = "./upload_files/" + std::to_string(user.id) + "/" + hashcode;
        // resp->Save(storage_path, content);
        // 不要用Save()，Save() 是异步任务，当前写法无法根据文件写入结果决定是否插入数据库
        // Save()失败也会进行数据库插入操作
        // 并且Save()底层使用pwrite，不会自动创建目录

        const fs::path storage_dir = "./upload_files/" + std::to_string(user.id);
        fs::create_directories(storage_dir);
        fs::path storage_path = storage_dir / hashcode;

        std::ofstream ofs{storage_path, std::ios::binary};
        if (!ofs) {
            resp->set_status(HttpStatusInternalServerError);
            respond_error(resp, "内部服务器错误");
            return;
        }
        ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        ofs.close();

        // ofstream是阻塞IO，适合小文件上传，对于大文件上传可以使用异步WFFileIOTask

        // oss云存储
        auto *ossClient = OssManager::instance().getClient();
        const fs::path oss_storage_path = "upload_files/" + std::to_string(user.id) + "/" + basename;
        if (!putFromMem("kk-oss-demo", oss_storage_path, content, ossClient)) {
            // OSS PutObject FAILED
        }

        const std::string sql =
                "insert into tbl_file(uid, filename, hashcode, size) values(" +
                std::to_string(user.id) + ", '" +
                MySQLUtil::escape_string(basename) + "', '" +
                MySQLUtil::escape_string(hashcode) + "', " +
                std::to_string(content.size()) + ")";

        resp->MySQL(database_url(), sql, [resp,basename](const MySQLResultCursor *cursor) {
            if (cursor->get_cursor_status() != MYSQL_STATUS_OK
                || cursor->get_affected_rows() != 1) {
                resp->set_status(HttpStatusInternalServerError);
                respond_error(resp, "内部服务器错误");
                return;
            }
            json data;
            data["fileId"] = cursor->get_insert_id();
            data["filename"] = basename;
            resp->set_status(HttpStatusCreated);
            respond_success(resp, "上传成功", data);
        });
    });

    server_.GET("/api/v1/file/{id}", [](const HttpReq *req, HttpResp *resp) {
        User user;
        if (!check_token(req, user)) {
            resp->set_status(HttpStatusUnauthorized);
            respond_error(resp, "无效的访问令牌");
            return;
        }

        int fileId = 0;
        fileId = req->param<int>("id");

        const int userId = user.id;
        const std::string sql =
                "select filename, hashcode from tbl_file where id = " +
                std::to_string(fileId) + " and uid = " +
                std::to_string(userId);
        resp->MySQL(database_url(), sql, [resp,userId](MySQLResultCursor *cursor) {
            if (cursor->get_cursor_status() != MYSQL_STATUS_GET_RESULT) {
                resp->set_status(HttpStatusInternalServerError);
                respond_error(resp, "内部服务器错误");
                return;
            }

            if (cursor->get_rows_count() != 1) {
                resp->set_status(HttpStatusNotFound);
                respond_error(resp, "文件不存在");
                return;
            }

            std::vector<MySQLCell> record;
            if (!cursor->fetch_row(record)) {
                resp->set_status(HttpStatusInternalServerError);
                respond_error(resp, "内部服务器错误");
                return;
            }

            std::string filename = PathUtil::base(record.at(0).as_string());
            std::string hashcode = record.at(1).as_string();

            if (filename.empty() || hashcode.empty()) {
                resp->set_status(HttpStatusInternalServerError);
                respond_error(resp, "内部服务器错误");
                return;
            }

            std::string storage_path =
                    "./upload_files/" + std::to_string(userId) + "/" + hashcode;
            resp->set_header_pair("Content-Disposition", "attachment; filename=" + filename);
            resp->set_status(HttpStatusOK);
            resp->File(storage_path);
        });
    });
}
