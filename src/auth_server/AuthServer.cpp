#include "auth.srpc.h"
#include "CryptoUtil.h"
#include "Config.h"

#include <workflow/WFFacilities.h>
#include <csignal>
#include <workflow/MySQLUtil.h>
#include <workflow/MySQLResult.h>
#include <ppconsul/agent.h>

using namespace srpc;
using namespace protocol;
using namespace ppconsul;
// using namespace ppconsul::agent;

static WFFacilities::WaitGroup waitGroup{1};
static WFFacilities::WaitGroup heartbeatWaitGroup{1};

const std::string &database_url() {
    static const std::string value = Config::required("CLOUDDISK_DB_URL");
    return value;
}

const std::string &jwt_secret() {
    static const std::string value = Config::required("CLOUDDISK_JWT_SECRET");
    return value;
}

unsigned short auth_port() {
    static const ushort value = static_cast<ushort>(
        std::stoi(Config::required("CLOUDDISK_AUTH_PORT")));
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

void timer_callback(WFTimerTask *task) {
    if (const int state = task->get_state(); state != WFT_STATE_SUCCESS) return;

    SeriesWork *series = series_of(task);
    auto *agent = static_cast<agent::Agent *>(series->get_context());
    try {
        agent->servicePass(Config::required("CLOUDDISK_AUTH_INSTANCE_ID"));
    } catch (const std::exception &) {
    }

    WFTimerTask *next = WFTaskFactory::create_timer_task(
        "auth_check",
        5, 0,
        timer_callback
    );
    series->push_back(next);
}

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    signal(SIGINT, [](int) { waitGroup.done(); });

    SRPCServer server;
    AuthServiceImpl service;
    server.add_service(&service);

    const unsigned short port = auth_port();
    if (!server.start(port)) {
        Consul consul(
            Config::required("CLOUDDISK_CONSUL_URL"),
            kw::dc = "dc1"
        );
        agent::Agent agent{consul};
        agent.registerService(
            agent::kw::name = "auth-service",
            agent::kw::id = Config::required("CLOUDDISK_AUTH_INSTANCE_ID"),
            agent::kw::address = Config::required("CLOUDDISK_AUTH_ADVERTISE_ADDRESS"),
            agent::kw::port = port,
            agent::kw::check = agent::TtlCheck{std::chrono::seconds{10}}
        );

        try {
            agent.servicePass(Config::required("CLOUDDISK_AUTH_INSTANCE_ID"));
        } catch (const std::exception &) {
        }
        WFTimerTask *timeTask = WFTaskFactory::create_timer_task(
            "auth_check",
            5, 0,
            timer_callback
        );

        SeriesWork *series = Workflow::create_series_work(
            timeTask,
            [](const SeriesWork *) { heartbeatWaitGroup.done(); }
        );
        series->set_context(&agent);
        series->start();

        waitGroup.wait();
        WFTaskFactory::cancel_by_name("auth_check");
        heartbeatWaitGroup.wait();
        agent.deregisterService(Config::required("CLOUDDISK_AUTH_INSTANCE_ID"));
        server.stop();
    } else std::cerr << "Error: Auth server start failed" << std::endl;

    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
