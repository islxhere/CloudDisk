#include "OssManager.h"
#include "Config.h"

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

OssClient *OssManager::getClient() const { return client_.get(); }
