#pragma once
#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace sm64nv {

struct Config {
    std::string rom;
    float scale = 1.5f;
    std::string scenario;
    std::string cell;
    int frames = 0;
    bool autotake = false;
    // damage of one punch before the target's armour
    float punch = 20;
    std::vector<std::string> errors;
};

Config parse_config(const std::string &text);

// one log line per kind of message, digits ignored so addresses and counters fold together
class MessageKinds {
public:
    explicit MessageKinds(size_t cap) : cap_(cap) {}
    bool first(const std::string &msg);
    size_t total() const { return total_; }

private:
    std::set<std::string> seen_;
    size_t cap_, total_ = 0;
};

}
