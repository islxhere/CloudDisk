#pragma once
#include <wfrest/HttpServer.h>

#include "auth.srpc.h"
#include "RouteSupport.h"


class CloudDiskServer {
public:
    CloudDiskServer();

    void register_routes();

    int start(unsigned short port) { return server_.start(port); }

    void stop() { server_.stop(); }

    void list_routes() { server_.list_routes(); }

private:
    // 注册路由
    void register_www_module();

    void register_auth_module();

    void register_user_module();

    void register_file_module();

private:
    wfrest::HttpServer server_;
};
