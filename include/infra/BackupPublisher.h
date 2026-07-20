#pragma once

#include <string>

struct Backup {
    int uid;
    std::string filename;
    std::string hashcode;
};

std::string jsonBackup(const Backup &backup);

Backup parseBackup(const std::string &body);

void publish(const Backup &backup);
