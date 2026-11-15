#pragma once

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

fs::path exeDir();
fs::path userConfigDir();
std::string u8str(const fs::path& path);

// Empty string when the platform dialog is unavailable.
std::string pickPath(bool directory);
