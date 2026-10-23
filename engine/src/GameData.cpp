#include "tob/GameData.h"

#include <cctype>
#include <fstream>
#include <stdexcept>

namespace tob {

namespace {

nlohmann::json readJson(const std::string& path, bool required) {
    std::ifstream in(path);
    if (!in) {
        if (required) throw std::runtime_error("cannot open " + path);
        return nlohmann::json::object();
    }
    return nlohmann::json::parse(in);
}

} // namespace

std::string looseKey(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

GameData GameData::load(const std::string& dataDir) {
    GameData d;
    const std::string dir = dataDir + "/json/";
    d.boards = readJson(dir + "talent_boards.json", true);
    d.skills = readJson(dir + "skills.json", true);
    d.traits = readJson(dir + "hero_traits.json", true);
    d.alternateYouga = readJson(dir + "alternate_spacetime_youga.json", false);
    return d;
}

const nlohmann::json* GameData::board(const std::string& slug) const {
    for (const auto& b : boards) {
        if (b.value("slug", "") == slug) return &b;
    }
    return nullptr;
}

const nlohmann::json* GameData::coreTalent(const std::string& name) const {
    const std::string key = looseKey(name);
    for (const auto& b : boards) {
        for (const auto& c : b["core_talents"]) {
            if (looseKey(c.value("name", "")) == key) return &c;
        }
    }
    return nullptr;
}

const nlohmann::json* GameData::skill(const std::string& name) const {
    const std::string key = looseKey(name);
    for (auto it = skills.begin(); it != skills.end(); ++it) {
        if (looseKey(it.key()) == key) return &it.value();
    }
    return nullptr;
}

const nlohmann::json* GameData::traitTree(const std::string& slug) const {
    for (const auto& t : traits) {
        if (t.value("slug", "") == slug) return &t;
    }
    return nullptr;
}

} // namespace tob
