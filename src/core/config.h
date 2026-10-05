#pragma once
#include <string>
#include <vector>

namespace sm64nv {

struct Config {
    std::string rom;
    float scale = 1.5f;
    std::string scenario;
    std::string cell;
    int frames = 0;
    std::vector<std::string> errors;
};

Config parse_config(const std::string &text);

}
