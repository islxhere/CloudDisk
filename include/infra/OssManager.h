#pragma once
#include <alibabacloud/oss/OssClient.h>
#include <string>
using namespace AlibabaCloud::OSS;

class OssManager {
public:
    static OssManager &instance();

    OssManager(const OssManager &) = delete;

    OssManager &operator=(const OssManager &) = delete;

    bool putFromMem(const std::string &bucket, const std::string &ossPath, const std::string &content) const;

    bool putFromFile(const std::string &bucket, const std::string &ossPath, const std::string &localPath) const;

private:
    OssManager();

    ~OssManager();

    std::unique_ptr<OssClient> client_;
};
