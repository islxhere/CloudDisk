#pragma once

#include <jwt.h>
#include <sodium.h>
#include <string>
#include <stdexcept>

struct User {
    int id;
    std::string username;
    std::string pwhash;
    std::string createAt;
};

class CryptoUtil {
public:
    static std::string generate_hashcode(const void *data, size_t n);

    static std::string hash_password(const std::string &password);

    static bool verify_password(const std::string &password, const std::string &pwhash);

    static std::string generate_token(const User &user, const std::string &secret,
                                      jwt_alg_t alg = JWT_ALG_HS256);

    static bool verify_token(const std::string &token, const std::string &secret, User &user);

    CryptoUtil() = delete;

    ~CryptoUtil() = delete;

    CryptoUtil(const CryptoUtil &) = delete;

    CryptoUtil &operator=(const CryptoUtil &) = delete;

private:
    struct SodiumInitializer {
        SodiumInitializer() {
            if (sodium_init() < 0)
                throw std::runtime_error("libsodium 初始化失败");
        }
    };

    static SodiumInitializer initializer_;
};
