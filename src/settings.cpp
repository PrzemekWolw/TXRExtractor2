#include "settings.hpp"

#include "platform.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

namespace {

Settings state;

template <typename T>
void take(const nlohmann::json& json, const char* key, T& value)
{
    if (!json.contains(key) || !json[key].is_string()) {
        return;
    }
    value = json[key].get<std::string>();
}

void takeBool(const nlohmann::json& json, const char* key, bool& value)
{
    if (json.contains(key) && json[key].is_boolean()) {
        value = json[key].get<bool>();
    }
}

fs::path settingsPath()
{
    return userConfigDir() / "settings.json";
}

} // namespace

Settings& settings()
{
    return state;
}

void loadSettings()
{
    std::ifstream in(settingsPath());
    if (!in) {
        return;
    }
    nlohmann::json json;
    try {
        in >> json;
    } catch (const std::exception&) {
        return;
    }
    if (!json.is_object()) {
        return;
    }
    take(json, "archive", state.archive);
    take(json, "toc", state.toc);
    take(json, "output", state.output);
    take(json, "image", state.image);
    takeBool(json, "raw", state.raw);
    takeBool(json, "models", state.models);
    takeBool(json, "textures", state.textures);
    takeBool(json, "combine", state.combine);
}

void saveSettings()
{
    nlohmann::json json = {
        {"archive", state.archive}, {"toc", state.toc}, {"output", state.output}, {"image", state.image},
        {"raw", state.raw},         {"models", state.models}, {"textures", state.textures}, {"combine", state.combine},
    };
    const fs::path path = settingsPath();
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path);
    if (out) {
        out << json.dump(2);
    }
}
