#include "auth.srpc.h"
#include "CryptoUtil.h"
#include "Config.h"

#include <workflow/WFFacilities.h>
#include <csignal>
#include <workflow/MySQLUtil.h>
#include <workflow/MySQLResult.h>

using namespace srpc;
using namespace protocol;

static WFFacilities::WaitGroup waitGroup{1};

const std::string &database_url() {
    static const std::string value = Config::required("CLOUDDISK_DB_URL");
    return value;
}

const std::string &jwt_secret() {
    static const std::string value = Config::required("CLOUDDISK_JWT_SECRET");
    return value;
}

class AuthServiceImpl : public Auth::Service {
public:
    void Register(RegisterRequest *request, RegisterResponse *response, RPCContext *ctx) override {
        const std::string &username = request->username();
        const std::string &password = request->password();
        const std::string &confirm = request->confirm();

        if (password != confirm) {
            response->set_result(AUTH_RESULT_PASSWORD_MISMATCH);
            return;
        }

        if (username.empty() || password.empty()) {
            response->set_result(AUTH_RESULT_EMPTY_CREDENTIALS);
            return;
        }

        std::string pwhash;
        try {
            pwhash = CryptoUtil::hash_password(password); //
        } catch (const std::exception &) {
            response->set_result(AUTH_RESULT_INTERNAL_ERROR);
            return;
        }

        const std::string insertSql =
                "insert into tbl_user(username, pwhash, tomb) values('" +
                MySQLUtil::escape_string(username) + "','" +
                MySQLUtil::escape_string(pwhash) + "', 0) "
                "on duplicate key update username = username";

        WFMySQLTask *sqlTask = WFTaskFactory::create_mysql_task(
            database_url(),
            3,
            [=](WFMySQLTask *task) {
                const int state = task->get_state();
                if (state != WFT_STATE_SUCCESS) {
                    response->set_result(AUTH_RESULT_INTERNAL_ERROR);
                    return;
                }

                MySQLResponse *resp = task->get_resp();
                if (resp->get_packet_type() == MYSQL_PACKET_ERROR) {
                    response->set_result(
                        AUTH_RESULT_INTERNAL_ERROR);
                    return;
                }

                MySQLResultCursor cursor{resp};
                if (cursor.get_cursor_status() != MYSQL_STATUS_OK) {
                    response->set_result(AUTH_RESULT_INTERNAL_ERROR);
                    return;
                }

                if (cursor.get_affected_rows() == 0) {
                    response->set_result(AUTH_RESULT_USERNAME_EXISTS);
                    return;
                }

                if (cursor.get_affected_rows() != 1) {
                    response->set_result(AUTH_RESULT_INTERNAL_ERROR);
                    return;
                }

                response->set_userid(cursor.get_insert_id());
                response->set_username(username);
                response->set_result(AUTH_RESULT_SUCCESS);
            });

        sqlTask->get_req()->set_query(insertSql);
        SeriesWork *series = ctx->get_series();
        series->push_back(sqlTask);
    }

    void Login(LoginRequest *request, LoginResponse *response, RPCContext *ctx) override {
        const std::string &username = request->username();
        const std::string &password = request->password();

        const std::string querySql =
                "select * from tbl_user where username = '" +
                MySQLUtil::escape_string(username) + "' and tomb = 0";

        WFMySQLTask *sqlTask = WFTaskFactory::create_mysql_task(
            database_url(),
            3,
            [=](WFMySQLTask *task) {
                const int state = task->get_state();
                if (state != WFT_STATE_SUCCESS) {
                    response->set_result(AUTH_RESULT_INTERNAL_ERROR);
                    return;
                }

                MySQLResponse *resp = task->get_resp();
                if (resp->get_packet_type() == MYSQL_PACKET_ERROR) {
                    response->set_result(
                        AUTH_RESULT_INTERNAL_ERROR);
                    return;
                }

                MySQLResultCursor cursor{resp};
                if (cursor.get_cursor_status() != MYSQL_STATUS_GET_RESULT) {
                    response->set_result(AUTH_RESULT_INTERNAL_ERROR);
                    return;
                }

                if (cursor.get_rows_count() != 1) {
                    response->set_result(AUTH_RESULT_INVALID_CREDENTIALS);
                    return;
                }

                std::vector<MySQLCell> record;
                if (!cursor.fetch_row(record)) {
                    response->set_result(AUTH_RESULT_INTERNAL_ERROR);
                    return;
                }

                User user;
                user.id = record.at(0).as_int();
                user.username = record.at(1).as_string();
                user.pwhash = record.at(2).as_string();
                user.createAt = record.at(3).as_datetime();
                if (!CryptoUtil::verify_password(password, user.pwhash)) {
                    response->set_result(AUTH_RESULT_INVALID_CREDENTIALS);
                    return;
                }

                response->set_accesstoken(CryptoUtil::generate_token(user, jwt_secret()));
                response->set_tokentype("Bearer");
                response->set_userid(user.id);
                response->set_username(user.username);
                response->set_result(AUTH_RESULT_SUCCESS);
            });
        sqlTask->get_req()->set_query(querySql);
        SeriesWork *series = ctx->get_series();
        series->push_back(sqlTask);
    }
};

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    signal(SIGINT, [](int) { waitGroup.done(); });

    SRPCServer server;
    AuthServiceImpl service;
    server.add_service(&service);

    if (!server.start(1412)) {
        waitGroup.wait();
        server.stop();
    } else std::cerr << "Error: Auth server start failed" << std::endl;

    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
