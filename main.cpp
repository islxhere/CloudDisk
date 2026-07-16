#include "CloudDiskServer.h"
#include <iostream>
#include <csignal>

using namespace std;

static WFFacilities::WaitGroup waitGroup(1);

int main() {
    signal(SIGINT, [](int) { waitGroup.done(); });
    srand(time(nullptr));

    CloudDiskServer server;

    server.register_routes();

    if (server.start(8888) == 0) {
        server.list_routes();
        waitGroup.wait();
        server.stop();
    } else cerr << "Error: Server start FAILED!" << endl;

    return 0;
}
