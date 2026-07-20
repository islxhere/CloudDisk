#include "BackupPublisher.h"
#include "Config.h"

#include <SimpleAmqpClient/SimpleAmqpClient.h>
#include <nlohmann/json.hpp>

std::string jsonBackup(const Backup &backup) {
    return nlohmann::json{
        {"uid", backup.uid},
        {"filename", backup.filename},
        {"hashcode", backup.hashcode}
    }.dump();
}

Backup parseBackup(const std::string &body) {
    const nlohmann::json payload = nlohmann::json::parse(body);
    Backup job{
        .uid = payload.at("uid").get<int>(),
        .filename = payload.at("filename").get<std::string>(),
        .hashcode = payload.at("hashcode").get<std::string>()
    };

    return job;
}

void publish(const Backup &backup) {
    const auto channel = AmqpClient::Channel::CreateFromUri(
        Config::required("CLOUDDISK_AMQP_URI"));
    const auto message = AmqpClient::BasicMessage::Create(jsonBackup(backup));
    channel->BasicPublish("oss.direct", "oss", message);
}
