#include "config.h"

#include <cctype>
#include <cstdlib>

namespace sm64nv {

static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

static bool parse_float(const std::string &v, float &out) {
    char *end = nullptr;
    float f = std::strtof(v.c_str(), &end);
    if (v.empty() || *end || !(f > 0)) return false;
    out = f;
    return true;
}

static bool parse_flag(const std::string &v, bool &out) {
    if (v != "0" && v != "1") return false;
    out = v == "1";
    return true;
}

static bool parse_count(const std::string &v, int &out) {
    char *end = nullptr;
    long n = std::strtol(v.c_str(), &end, 10);
    if (v.empty() || *end || n < 0 || n > 10000000) return false;
    out = (int)n;
    return true;
}

Config parse_config(const std::string &text) {
    Config c;
    size_t start = 0;
    for (int line = 1; start <= text.size(); line++) {
        size_t end = text.find('\n', start);
        std::string raw = trim(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        start = end == std::string::npos ? text.size() + 1 : end + 1;
        if (raw.empty() || raw[0] == ';' || raw[0] == '#' || raw[0] == '[') continue;
        size_t eq = raw.find('=');
        if (eq == std::string::npos) {
            c.errors.push_back("bad line=" + std::to_string(line) + " text=" + raw);
            continue;
        }
        std::string key = trim(raw.substr(0, eq)), value = trim(raw.substr(eq + 1));
        bool ok = true;
        if (key == "rom") c.rom = value;
        else if (key == "scale") ok = parse_float(value, c.scale);
        else if (key == "scenario") c.scenario = value;
        else if (key == "cell") c.cell = value;
        else if (key == "load") c.load = value;
        else if (key == "frames") ok = parse_count(value, c.frames);
        else if (key == "autotake") ok = parse_flag(value, c.autotake);
        else if (key == "punch") ok = parse_float(value, c.punch);
        else if (key == "particles") ok = parse_flag(value, c.particles);
        else if (key == "meter") ok = parse_flag(value, c.meter);
        else if (key == "spare_friends") ok = parse_flag(value, c.stomp.spare_friends);
        else if (key == "squash_tallest") ok = parse_float(value, c.stomp.tallest);
        else {
            c.errors.push_back("unknown key=" + key + " line=" + std::to_string(line));
            continue;
        }
        if (!ok) c.errors.push_back("bad value key=" + key + " value=" + value + " line=" + std::to_string(line));
    }
    if (c.rom.empty()) c.errors.push_back("missing key=rom");
    return c;
}

bool MessageKinds::first(const std::string &msg) {
    total_++;
    std::string key;
    for (size_t i = 0; i < msg.size();) {
        size_t j = i;
        bool digit = false;
        while (j < msg.size() && isxdigit((unsigned char)msg[j])) digit |= isdigit((unsigned char)msg[j]) != 0, j++;
        if (j == i) key += msg[i++];
        else key += digit ? "#" : msg.substr(i, j - i), i = j;
    }
    return seen_.size() < cap_ && seen_.insert(key).second;
}

}
