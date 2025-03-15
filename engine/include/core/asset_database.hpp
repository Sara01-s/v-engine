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
    static constexpr auto s_assets_dir {ASSETS_DIR};

    static std::filesystem::path const&
    root();

    static std::filesystem::path
    resolve(std::string const& relative_path);

    static std::string
    read_asset_file(std::string const& assets_relative_path);

    static std::filesystem::path
    absolute_path(std::string const& assets_relative_path);

    static bool
    exists(std::string const& assets_relative_path);

private:
    static std::filesystem::path
    _init_assets_root();
};

} // namespace core