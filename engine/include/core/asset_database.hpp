// include/core/asset_database.hpp
#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "log.hpp"
#include "types.hpp"

namespace core {

class AssetDatabase {
public:
    static std::filesystem::path const&
    root() {
        static std::filesystem::path const s_assets_root = _init_assets_root();
        return s_assets_root;
    }

    static std::filesystem::path
    resolve(std::string const& relative_path) {
        std::filesystem::path const resolved = root() / relative_path;
        core_assert(
            std::filesystem::exists(resolved),
            "Failed to resolve path: file does not exists."
        );

        return resolved;
    }

    static std::string
    read_asset_file(std::string const& assets_relative_path) {
        std::ifstream file(
            resolve(assets_relative_path),
            std::ios::ate | std::ios::binary
        );

        core_assert(file.is_open(), "Failed to open asset file.");

        // Since we started at the end, we can tell the file size :).
        usize const file_size = static_cast<usize>(file.tellg());
        std::string buffer(file_size, '\0');

        file.seekg(0); // Move read pointer to beginning.
        file.read(buffer.data(), file_size); // Write data to buffer.
        file.close();

        return buffer;
    }

    static std::filesystem::path
    absolute_path(std::string const& assets_relative_path) {
        std::filesystem::path const path = resolve(assets_relative_path);
        core_assert(
            exists(path),
            "Failed to get absolute path: file does not exists."
        );

        return std::filesystem::absolute(path);
    }

    static bool
    exists(std::string const& assets_relative_path) {
        return std::filesystem::exists(resolve(assets_relative_path));
    }

private:
    static std::filesystem::path
    _init_assets_root() {
        // Defined in root CMakeLists.
        std::filesystem::path const root = ASSETS_DIR;

        if (!std::filesystem::exists(root)) {
            throw std::runtime_error(
                "Assets directory not found at: " + root.string()
            );
        }

        return root;
    }
};

} // namespace core