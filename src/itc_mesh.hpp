#pragma once

#include "glb.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace txr {

bool isPartFile(const uint8_t* data, size_t n);

// courseNames indexes the course-pool XPR (slot 1). Null when that pool is not loaded.
std::vector<ShapeMesh> decodePart(const uint8_t* data, size_t n, const std::vector<std::string>* courseNames);

std::vector<std::string> ownXprNames(const uint8_t* data, size_t n);

// Byte range of little-endian DataFile entry `index`, or false.
bool dataFileSlice(const uint8_t* data, size_t n, int index, size_t& off, size_t& len);

struct CourseModel {
    std::string name;
    std::vector<ShapeMesh> shapes;
};

struct CourseSet {
    std::vector<std::string> textureNames;
    std::vector<uint8_t> textureBlob; // pool-layout XPR2 (synth), empty if entry 0 is not an XPR2
    std::vector<CourseModel> models;
};

CourseSet decodeCourseSet(const uint8_t* data, size_t n);

struct Point {
    float x = 0;
    float y = 0;
    float z = 0;
    float heading = 0;
    bool used = false;
    int group = 0;
};

// Deprotected image, file offset = VA - 0x82000000. 108 used points on this course.
std::vector<Point> readCheckerPoints(const uint8_t* image, size_t n);

// Both entry-7 models at every used point, mirrored and yawed.
std::vector<ShapeMesh> placeCheckers(const std::vector<ShapeMesh>& models, const std::vector<Point>& points);

} // namespace txr
