#pragma once

#include <string>

struct Settings {
    std::string archive;
    std::string toc;
    std::string output;
    std::string image;
    bool raw = true;
    bool models = true;
    bool textures = true;
    bool combine = false;
};

Settings& settings();
void loadSettings();
void saveSettings();
