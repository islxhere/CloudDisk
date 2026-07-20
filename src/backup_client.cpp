#include "BackupPublisher.h"
#include "Config.h"
#include "OssManager.h"

#include <SimpleAmqpClient/SimpleAmqpClient.h>
#include <iostream>

using namespace AmqpClient;

bool backup(const Backup &backup) {
    const std::string storage_path =
            "./upload_files/" + std::to_string(backup.uid) + "/" + backup.hashcode;
    const std::string oss_path = "upload_files/" + std::to_string(backup.uid) + "/" + backup.filename;
    return OssManager::instance().putFromFile("kk-oss-demo", oss_path, storage_path);
}

int main() {
    try {
        const auto channel = Channel::CreateFromUri(Config::required("CLOUDDISK_AMQP_URI"));
        channel->BasicConsume("oss_queue");

        while (true) {
            const auto envelope = channel->BasicConsumeMessage();
            if (!envelope || !envelope->Message()) continue;

            try {
                backup(parseBackup(envelope->Message()->Body())); //
            } catch (const std::exception &error) {
                std::cerr << "处理备份消息失败: " << error.what() << std::endl; //
            }
        }
    } catch (const std::exception &error) {
        std::cerr << "备份消费者启动失败: " << error.what() << std::endl;
        return 1;
    }
    return 0;
}
