#pragma once
#include <alibabacloud/oss/OssClient.h>
using namespace AlibabaCloud::OSS;

class OssManager {
public:
    static OssManager &instance();

    OssManager(const OssManager &) = delete;

    OssManager &operator=(const OssManager &) = delete;

    OssClient *getClient() const;

private:
    OssManager();

    ~OssManager();

    std::unique_ptr<OssClient> client_;
};
