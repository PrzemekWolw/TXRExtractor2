#pragma once

#include "task.hpp"

#include <string>

struct ExportRequest {
    std::string archive;
    std::string toc;
    std::string output;
    std::string image;
    bool raw = true;
    bool models = true;
    bool textures = true;
    bool combine = false;
};

void exportPackage(const ExportRequest& request, TaskState& task);
