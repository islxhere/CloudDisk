#pragma once
#include <wfrest/HttpServer.h>

// 面向对象：将专业的事情交给专业的“人”去做

class CloudDiskServer {
public:
    CloudDiskServer() = default;

    // 注册路由
    void register_routes();

    // 包装了一层: 要保证包装后的接口与原来的接口一致！
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
    // 名字中最好不要带具体的实现细节
    // 方便以后修改具体的实现
    wfrest::HttpServer server_; // 组合
};
