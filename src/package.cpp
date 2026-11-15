#include "package.hpp"

#include "bytes.hpp"
#include "glb.hpp"
#include "itc_mesh.hpp"
#include "png.hpp"
#include "ucl.hpp"
#include "wmn.hpp"
#include "xpr.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s)
{
    for (char& c : s) {
        c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

fs::path safeJoin(const fs::path& root, const std::string& name)
{
    fs::path out = root;
    std::string cur;
    auto flush = [&] {
        if (!cur.empty() && cur != "." && cur != "..") {
            out /= cur;
        }
        cur.clear();
    };
    for (char c : name) {
        if (c == '/' || c == '\\') {
            flush();
        } else {
            cur.push_back(c);
        }
    }
    flush();
    return out;
}

std::string stemOf(const std::string& name)
{
    std::string base = name;
    const auto slash = base.find_last_of("/\\");
    if (slash != std::string::npos) {
        base = base.substr(slash + 1);
    }
    const auto dot = base.find_last_of('.');
    if (dot != std::string::npos) {
        base = base.substr(0, dot);
    }
    return lower(base);
}

struct ItcRec {
    uint32_t id = 0;
    uint32_t comp = 0;
    uint32_t dec = 0;
    bool placeholder() const { return comp == 0; }
    bool raw() const { return dec == 0 && comp > 0; }
};

std::vector<ItcRec> readItcToc(const fs::path& path)
{
    auto toc = txr::readFile(path);
    if (toc.size() < 16) {
        throw std::runtime_error("TOC is too small");
    }
    const uint32_t count = txr::be32(toc.data());
    if (count > 2000000 || 16 + size_t(count) * 16 > toc.size()) {
        throw std::runtime_error("bad ITC TOC");
    }
    std::vector<ItcRec> recs;
    recs.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* p = toc.data() + 16 + size_t(i) * 16;
        recs.push_back({txr::be32(p), txr::be32(p + 4), txr::be32(p + 8)});
    }
    return recs;
}

bool peekCourse(const fs::path& archive, const ItcRec& rec)
{
    if (!rec.raw() || rec.comp < 16) {
        return false;
    }
    std::ifstream in(archive, std::ios::binary);
    auto head = txr::readSlice(in, uint64_t(rec.id) << 11, std::min<size_t>(rec.comp, 256));
    if (head.size() < 8) {
        return false;
    }
    const uint32_t count = txr::le32(head.data());
    if (count == 0 || count > 64 || 4 + size_t(count) * 4 > head.size()) {
        return false;
    }
    const uint32_t off = txr::le32(head.data() + 4);
    if (uint64_t(off) + 4 > rec.comp) {
        return false;
    }
    auto magic = txr::readSlice(in, (uint64_t(rec.id) << 11) + off, 4);
    return magic.size() == 4 && std::memcmp(magic.data(), "XPR2", 4) == 0;
}

std::vector<uint8_t> loadItc(const fs::path& archive, const ItcRec& rec)
{
    std::ifstream in(archive, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open archive");
    }
    auto bytes = txr::readSlice(in, uint64_t(rec.id) << 11, rec.comp);
    if (rec.raw()) {
        return bytes;
    }
    return txr::decompressUclpack(bytes.data(), bytes.size());
}

void attachTexture(txr::Prim& prim, const txr::Image& image)
{
    if (image.rgba.empty()) {
        return;
    }
    const bool dirty = prim.name.size() >= 6 && prim.name.compare(prim.name.size() - 6, 6, "_dirty") == 0;
    prim.blend = !dirty && txr::mostlyTransparent(image);
    prim.png = txr::encodePng(image);
}

void embedRefs(std::vector<txr::ShapeMesh>& meshes, const txr::Xpr* course, const txr::Xpr* own)
{
    for (txr::ShapeMesh& mesh : meshes) {
        for (txr::Prim& prim : mesh.prims) {
            for (const txr::TexRef& ref : prim.refs) {
                const txr::Xpr* src = ref.slot == 1 ? course : ref.slot == 2 ? own : nullptr;
                if (!src || int(ref.index) >= src->count()) {
                    continue;
                }
                txr::Image image = src->image(int(ref.index));
                if (image.rgba.empty()) {
                    continue;
                }
                attachTexture(prim, image);
                break;
            }
        }
    }
}

void writeShapes(const fs::path& path, std::vector<txr::ShapeMesh>& meshes)
{
    auto glb = txr::buildGlb(meshes);
    txr::writeFile(path, glb);
}

void exportItc(const ExportRequest& req, TaskState& task)
{
    auto recs = readItcToc(req.toc);
    fs::path root = req.output;
    fs::create_directories(root);
    std::unique_ptr<txr::Xpr> course;
    std::vector<txr::ShapeMesh> courseModels;
    std::vector<txr::ShapeMesh> combined;
    int courseCount = 0;

    auto exportCourse = [&](const ItcRec& rec, const std::vector<uint8_t>& bytes) {
        txr::CourseSet set = txr::decodeCourseSet(bytes.data(), bytes.size());
        if (set.textureNames.empty() && set.models.empty()) {
            return false;
        }
        ++courseCount;
        const fs::path dir = courseCount == 1 ? root / "courseset" : root / ("courseset_" + std::to_string(rec.id));
        const fs::path texDir = courseCount == 1 ? root / "textures" / "course" : root / "textures" / ("course_" + std::to_string(rec.id));
        txr::Xpr local;
        const txr::Xpr* tex = nullptr;
        if (!set.textureBlob.empty()) {
            local = txr::Xpr::parse(set.textureBlob.data(), set.textureBlob.size(), txr::BcLayout::Pool);
            if (rec.id == 716244) {
                course = std::make_unique<txr::Xpr>(local);
                tex = course.get();
            } else {
                tex = &local;
            }
            if (req.textures) {
                int written = 0;
                for (int i = 0; i < local.count(); ++i) {
                    if (task.stop) {
                        return true;
                    }
                    txr::Image image = (tex ? tex : &local)->image(i);
                    if (image.rgba.empty()) {
                        continue;
                    }
                    const std::string token = txr::cleanToken(local.name(i).empty() ? "tex" : local.name(i), 48);
                    char file[96];
                    std::snprintf(file, sizeof(file), "%04d_%s.png", i, token.c_str());
                    auto png = txr::encodePng(image);
                    txr::writeFile(texDir / file, png);
                    ++written;
                }
                task.log("course " + std::to_string(rec.id) + ": " + std::to_string(written) + " textures");
            }
        }
        if (req.models) {
            for (auto& model : set.models) {
                if (tex) {
                    embedRefs(model.shapes, tex, nullptr);
                }
                for (auto& shape : model.shapes) {
                    shape.name = model.name;
                }
                writeShapes(dir / (model.name + ".glb"), model.shapes);
                const bool points = model.name.rfind("points_", 0) == 0;
                if (points) {
                    courseModels.insert(courseModels.end(), model.shapes.begin(), model.shapes.end());
                }
                if (req.combine && !(points && !req.image.empty() && rec.id == 716244)) {
                    combined.insert(combined.end(), model.shapes.begin(), model.shapes.end());
                }
            }
            task.log("course " + std::to_string(rec.id) + ": " + std::to_string(set.models.size()) + " models");
        }
        if (req.models && rec.id == 716244 && !req.image.empty() && !courseModels.empty()) {
            auto image = txr::readFile(req.image);
            auto points = txr::readCheckerPoints(image.data(), image.size());
            nlohmann::json js = nlohmann::json::array();
            for (const auto& p : points) {
                js.push_back({{"group", p.group}, {"x", p.x}, {"y", p.y}, {"z", p.z}, {"heading", p.heading}});
            }
            const std::string text = js.dump(1);
            txr::writeFile(dir / "points.json", reinterpret_cast<const uint8_t*>(text.data()), text.size());
            auto placed = txr::placeCheckers(courseModels, points);
            if (tex) {
                embedRefs(placed, tex, nullptr);
            }
            writeShapes(dir / "points_instanced.glb", placed);
            if (req.combine) {
                combined.insert(combined.end(), placed.begin(), placed.end());
            }
            task.log("checkers: " + std::to_string(points.size()) + " points, " + std::to_string(placed.size()) + " shapes");
        }
        return true;
    };

    std::vector<size_t> jobs;
    for (size_t i = 0; i < recs.size(); ++i) {
        if (!recs[i].placeholder()) {
            jobs.push_back(i);
        }
    }
    // Course sets first, so slot-1 names and pixels exist for the mesh pool.
    for (size_t index : jobs) {
        if (task.stop) {
            break;
        }
        if (!recs[index].raw() || !peekCourse(req.archive, recs[index])) {
            continue;
        }
        try {
            auto bytes = loadItc(req.archive, recs[index]);
            if (req.raw) {
                char name[32];
                std::snprintf(name, sizeof(name), "rec_%07u.bin", recs[index].id);
                txr::writeFile(root / "raw" / name, bytes);
            }
            exportCourse(recs[index], bytes);
            recs[index].comp = 0; // don't export it again
        } catch (const std::exception& ex) {
            task.log(std::string("course ") + std::to_string(recs[index].id) + ": " + ex.what());
        }
    }

    std::atomic<int> done{0};
    std::mutex combineMu;
    const unsigned workers = std::max(1u, std::thread::hardware_concurrency());
    runPool(jobs.size(), workers, task,
            [&](size_t job) -> std::string {
                const ItcRec& rec = recs[jobs[job]];
                if (rec.placeholder() || task.stop) {
                    return {};
                }
                try {
                    auto bytes = loadItc(req.archive, rec);
                    if (req.raw) {
                        char name[32];
                        std::snprintf(name, sizeof(name), "rec_%07u.bin", rec.id);
                        txr::writeFile(root / "raw" / name, bytes);
                    }
                    if (txr::isPartFile(bytes.data(), bytes.size())) {
                        if (req.models || req.textures) {
                            std::unique_ptr<txr::Xpr> own;
                            size_t off = 0, len = 0;
                            if (txr::dataFileSlice(bytes.data(), bytes.size(), 1, off, len) && len >= 4 &&
                                std::memcmp(bytes.data() + off, "XPR2", 4) == 0) {
                                own = std::make_unique<txr::Xpr>(txr::Xpr::parse(bytes.data() + off, len, txr::BcLayout::Pool));
                                if (req.textures) {
                                    for (int i = 0; i < own->count(); ++i) {
                                        txr::Image image = own->image(i);
                                        if (image.rgba.empty()) {
                                            continue;
                                        }
                                        const std::string token = txr::cleanToken(own->name(i).empty() ? "tex" : own->name(i), 48);
                                        char file[96];
                                        std::snprintf(file, sizeof(file), "%04d_%s.png", i, token.c_str());
                                        txr::writeFile(root / "textures" / "own" / std::to_string(rec.id) / file, txr::encodePng(image));
                                    }
                                }
                            }
                            if (req.models) {
                                auto meshes = txr::decodePart(bytes.data(), bytes.size(), course ? &course->names() : nullptr);
                                embedRefs(meshes, course.get(), own.get());
                                char name[32];
                                std::snprintf(name, sizeof(name), "rec_%07u", rec.id);
                                for (auto& mesh : meshes) {
                                    mesh.name = name;
                                }
                                writeShapes(root / "models" / (std::string(name) + ".glb"), meshes);
                                if (req.combine) {
                                    std::lock_guard lock(combineMu);
                                    combined.insert(combined.end(), meshes.begin(), meshes.end());
                                }
                            }
                        }
                    } else if (bytes.size() >= 4 && std::memcmp(bytes.data(), "XPR2", 4) == 0 && req.textures) {
                        txr::Xpr xpr = txr::Xpr::parse(bytes.data(), bytes.size(), txr::BcLayout::Xenos);
                        for (int i = 0; i < xpr.count(); ++i) {
                            txr::Image image = xpr.image(i);
                            if (image.rgba.empty()) {
                                continue;
                            }
                            const std::string token = txr::cleanToken(xpr.name(i).empty() ? "tex" : xpr.name(i), 48);
                            char file[96];
                            std::snprintf(file, sizeof(file), "rec_%07u_%04d_%s.png", rec.id, i, token.c_str());
                            txr::writeFile(root / "textures" / file, txr::encodePng(image));
                        }
                    }
                } catch (const std::exception& ex) {
                    return std::string("rec ") + std::to_string(rec.id) + ": " + ex.what();
                }
                const int n = ++done;
                if (n % 25 == 0) {
                    task.setStatus("ITC " + std::to_string(n) + "/" + std::to_string(jobs.size()));
                }
                return {};
            },
            [&](size_t, const std::string& error) {
                if (!error.empty()) {
                    task.log(error);
                }
            });
    if (req.combine && !combined.empty() && !task.stop) {
        task.setStatus("Writing combined map");
        writeShapes(root / "map.glb", combined);
    }
}

void exportWmn(const ExportRequest& req, TaskState& task)
{
    auto toc = txr::readFile(req.toc);
    auto entries = txr::parseWmnToc(toc.data(), toc.size());
    const fs::path root = req.output;
    fs::create_directories(root);
    task.setStatus("Reading archive");
    auto dat = txr::readFile(req.archive);
    std::vector<fs::path> written(entries.size());
    std::atomic<int> done{0};
    const unsigned workers = std::max(1u, std::thread::hardware_concurrency());
    runPool(entries.size(), workers, task,
            [&](size_t i) -> std::string {
                if (task.stop) {
                    return {};
                }
                try {
                    std::vector<uint8_t> bytes;
                    if (entries[i].size == 0) {
                        if (entries[i].offset + entries[i].stored > dat.size()) {
                            throw std::runtime_error("raw entry past end");
                        }
                        bytes.assign(dat.begin() + ptrdiff_t(entries[i].offset),
                                     dat.begin() + ptrdiff_t(entries[i].offset + entries[i].stored));
                    } else {
                        bytes = txr::inflateWmn(dat.data(), dat.size(), size_t(entries[i].offset), entries[i].size);
                    }
                    const fs::path path = safeJoin(root, entries[i].name);
                    if (req.raw || req.models || req.textures) {
                        txr::writeFile(path, bytes);
                        written[i] = path;
                    }
                } catch (const std::exception& ex) {
                    return entries[i].name + ": " + ex.what();
                }
                const int n = ++done;
                if (n % 50 == 0) {
                    task.setStatus("WMN " + std::to_string(n) + "/" + std::to_string(entries.size()));
                }
                return {};
            },
            [&](size_t, const std::string& error) {
                if (!error.empty()) {
                    task.log(error);
                }
            });

    std::map<std::string, fs::path> pngs;
    if (req.textures) {
        for (size_t i = 0; i < written.size(); ++i) {
            if (task.stop || written[i].empty()) {
                continue;
            }
            const std::string ext = lower(written[i].extension().string());
            if (ext != ".gtf") {
                continue;
            }
            try {
                auto bytes = txr::readFile(written[i]);
                auto images = txr::decodeGtf(bytes.data(), bytes.size());
                for (size_t k = 0; k < images.size(); ++k) {
                    if (images[k].rgba.empty()) {
                        continue;
                    }
                    txr::Image image;
                    image.width = images[k].width;
                    image.height = images[k].height;
                    image.rgba = std::move(images[k].rgba);
                    fs::path png = k == 0 ? fs::path(written[i]).replace_extension(".png")
                                          : written[i].parent_path() / (written[i].stem().string() + "_" + std::to_string(k) + ".png");
                    txr::writeFile(png, txr::encodePng(image));
                    pngs[stemOf(png.string())] = png;
                }
            } catch (const std::exception& ex) {
                task.log(written[i].filename().string() + ": " + ex.what());
            }
        }
    }
    if (req.models) {
        for (const fs::path& path : written) {
            if (task.stop || path.empty() || lower(path.extension().string()) != ".grid") {
                continue;
            }
            try {
                auto bytes = txr::readFile(path);
                auto decoded = txr::decodeGrid(bytes.data(), bytes.size());
                for (auto& image : decoded.images) {
                    fs::path png = path.parent_path() / (image.first + ".png");
                    txr::writeFile(png, txr::encodePng(image.second));
                    pngs[lower(image.first)] = png;
                }
                for (auto& mesh : decoded.meshes) {
                    for (auto& prim : mesh.prims) {
                        const std::string key = lower(prim.name);
                        auto it = pngs.find(key);
                        if (it == pngs.end()) {
                            it = pngs.find(stemOf(prim.name));
                        }
                        if (it != pngs.end()) {
                            auto png = txr::readFile(it->second);
                            prim.png = std::move(png);
                        }
                    }
                }
                if (!decoded.meshes.empty()) {
                    fs::path glb = path;
                    glb.replace_extension(".glb");
                    writeShapes(glb, decoded.meshes);
                }
            } catch (const std::exception& ex) {
                task.log(path.filename().string() + ": " + ex.what());
            }
        }
    }
}

} // namespace

void exportPackage(const ExportRequest& request, TaskState& task)
{
    try {
        if (request.archive.empty() || request.toc.empty() || request.output.empty()) {
            task.finish("Choose an archive, a TOC and an output folder");
            return;
        }
        auto toc = txr::readFile(request.toc);
        task.setStatus("Reading");
        if (toc.size() >= 4 && std::memcmp(toc.data(), "BLDh", 4) == 0) {
            task.log("Wangan Midnight");
            exportWmn(request, task);
        } else {
            task.log("Import Tuner Challenge");
            exportItc(request, task);
        }
        task.finish(task.stop ? "Stopped" : "Done");
    } catch (const std::exception& ex) {
        task.log(ex.what());
        task.finish("Failed");
    }
}
