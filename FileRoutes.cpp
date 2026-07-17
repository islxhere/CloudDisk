#include "CloudDiskServer.h"
#include "CryptoUtil.h"
#include "OssManager.h"
#include "RouteSupport.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <wfrest/PathUtil.h>
#include <workflow/HttpUtil.h>
#include <workflow/MySQLResult.h>
#include <workflow/MySQLUtil.h>
#include <workflow/mysql_types.h>

using namespace protocol;
using namespace wfrest;
using json = nlohmann::json;
namespace fs = std::filesystem;
using clouddisk::route::check_token;
using clouddisk::route::database_url;
using clouddisk::route::respond_error;
using clouddisk::route::respond_success;

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

        std::string hashcode;
        try {
            hashcode = CryptoUtil::generate_hashcode(content.c_str(), content.size());
        } catch (std::exception &) {
            resp->set_status(HttpStatusInternalServerError);
            respond_error(resp, "内部服务器错误");
            return;
        }

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

        int fileId = req->param<int>("id");
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
