#include "CryptoUtil.h"

#include <cstring>

std::string CryptoUtil::generate_hashcode(const void *data, const size_t n) {
    unsigned char output[crypto_hash_sha256_BYTES];
    if (crypto_hash_sha256(output, static_cast<const unsigned char *>(data), n) != 0) {
        throw std::runtime_error("生成SHA256哈希失败");
    }
    // 转换成十六进制字符，存储到result中
    char result[crypto_hash_sha256_BYTES * 2 + 1] = "";
    for (unsigned i = 0; i < crypto_hash_sha256_BYTES; ++i) { sprintf(result + 2 * i, "%02x", output[i]); }
    return result;
}

std::string CryptoUtil::hash_password(const std::string &password) {
    char hashed[crypto_pwhash_STRBYTES];

    if (crypto_pwhash_str(
            hashed,
            password.c_str(),
            password.size(),
            crypto_pwhash_OPSLIMIT_INTERACTIVE, // 操作限制(operation limit)
            crypto_pwhash_MEMLIMIT_INTERACTIVE // 内存限制(memory limit)
        ) != 0)
        throw std::runtime_error("Error in crypto hash");

    return std::string{hashed};
}

bool CryptoUtil::verify_password(const std::string &password, const std::string &pwhash) {
    return crypto_pwhash_str_verify(
               pwhash.c_str(),
               password.c_str(),
               password.size()
           ) == 0;
}

std::string CryptoUtil::generate_token(const User &user, const std::string &secret, jwt_alg_t alg) {
    jwt_t *jwt;
    jwt_new(&jwt);

    jwt_set_alg(jwt, alg, reinterpret_cast<const unsigned char *>(secret.data()),
                static_cast<int>(secret.size()));

    // 设置载荷(Payload): 用户自定义数据(不能存放敏感数据，比如：密码的哈希值)
    jwt_add_grant(jwt, "sub", "login");
    jwt_add_grant_int(jwt, "id", user.id);
    jwt_add_grant(jwt, "username", user.username.c_str());
    jwt_add_grant(jwt, "createdAt", user.createAt.c_str());
    jwt_add_grant_int(jwt, "expire", time(nullptr) + 1800);

    char *token = jwt_encode_str(jwt);
    std::string result = token;

    jwt_free(jwt);
    free(token);

    return result;
}

bool CryptoUtil::verify_token(const std::string &token, const std::string &secret, User &user) {
    jwt_t *jwt;
    if (jwt_decode(&jwt, token.c_str(),
                   reinterpret_cast<const unsigned char *>(secret.data()),
                   static_cast<int>(secret.size()))
    ) { return false; }

    if (strcmp("login", jwt_get_grant(jwt, "sub")) != 0) {
        jwt_free(jwt);
        return false;
    }

    if (jwt_get_grant_int(jwt, "expire") < time(nullptr)) {
        jwt_free(jwt);
        return false;
    }

    user.id = static_cast<int>(jwt_get_grant_int(jwt, "id"));
    user.username = jwt_get_grant(jwt, "username");
    user.createAt = jwt_get_grant(jwt, "createdAt");

    jwt_free(jwt);
    return true;
}
