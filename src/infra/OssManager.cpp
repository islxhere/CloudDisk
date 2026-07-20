#include "OssManager.h"
#include "Config.h"

#include <fstream>
#include <iostream>
#include <memory>

OssManager::OssManager() {
    InitializeSdk();

    ClientConfiguration conf;
    conf.signatureVersion = SignatureVersionType::V4;

    const std::string endpoint = Config::required("CLOUDDISK_OSS_ENDPOINT");
    const std::string accessKeyId = Config::required("CLOUDDISK_OSS_ACCESS_KEY_ID");
    const std::string accessKeySecret = Config::required("CLOUDDISK_OSS_ACCESS_KEY_SECRET");
    const std::string region = Config::required("CLOUDDISK_OSS_REGION");

    client_ = std::make_unique<OssClient>(endpoint, accessKeyId, accessKeySecret, conf);
    client_->SetRegion(region);
}

OssManager::~OssManager() {
    client_.reset();
    ShutdownSdk();
}

OssManager &OssManager::instance() {
    static OssManager ossManager;
    return ossManager;
}

bool OssManager::putFromMem(const std::string &bucket, const std::string &ossPath, const std::string &content) const {
    auto stream = std::make_shared<std::stringstream>(content);
    PutObjectRequest request{bucket, ossPath, stream};
    const auto outcome = client_->PutObject(request);
    if (!outcome.isSuccess()) {
        std::cerr << "PutObject FAILED, code: " << outcome.error().Code()
                << ", message: " << outcome.error().Message()
                << ", requestId: " << outcome.error().RequestId() << std::endl;
        return false;
    }
    return true;
}

bool OssManager::putFromFile(const std::string &bucket, const std::string &ossPath,
                             const std::string &localPath) const {
    auto stream = std::make_shared<std::fstream>(localPath, std::ios::in | std::ios::binary);
    if (!*stream) {
        std::cerr << "无法读取备份文件: " << localPath << std::endl;
        return false;
    }

    PutObjectRequest request{bucket, ossPath, stream};
    const auto outcome = client_->PutObject(request);
    if (!outcome.isSuccess()) {
        std::cerr << "PutObject FAILED, code: " << outcome.error().Code()
                << ", message: " << outcome.error().Message()
                << ", requestId: " << outcome.error().RequestId() << std::endl;
        return false;
    }
    return true;
}
