#include "LocalStorage.h"

#include <fstream>

bool writeFile(const std::string &storagePath, const std::string &content) {
    std::ofstream output(storagePath, std::ios::binary);
    if (!output) return false;

    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output) return false;

    output.close();
    return static_cast<bool>(output);
}
