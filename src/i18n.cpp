#include "i18n.hpp"

#include "platform.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <map>

namespace {

std::map<std::string, std::string> strings;

}

void i18nInit()
{
    const fs::path candidates[] = {exeDir() / "data" / "lang" / "en.json", fs::path("data") / "lang" / "en.json"};
    for (const fs::path& path : candidates) {
        std::ifstream in(path);
        if (!in) {
            continue;
        }
        nlohmann::json json;
        in >> json;
        if (!json.is_object()) {
            return;
        }
        for (auto it = json.begin(); it != json.end(); ++it) {
            if (it.value().is_string()) {
                strings[it.key()] = it.value().get<std::string>();
            }
        }
        return;
    }
}

const char* tr(const char* id)
{
    const auto it = strings.find(id);
    if (it == strings.end()) {
        return id;
    }
    return it->second.c_str();
}
