#include "options.h"

#include <algorithm>
#include <cstdio>

namespace sm64nv {

const std::vector<Option> &game_options() {
    static const std::vector<Option> all = {
        {"punch", "Punch damage", {5, 10, 20, 35, 50, 100}, [](const Config &c) { return c.punch; }, [](Config &c, float v) { c.punch = v; }, false},
        {"spare_friends", "Spare friends", {0, 1}, [](const Config &c) { return (float)c.stomp.spare_friends; },
         [](Config &c, float v) { c.stomp.spare_friends = v != 0; }, true},
        // a person, a tall man, a deathclaw, a giant
        {"squash_tallest", "Squash up to height", {132, 172, 250, 400}, [](const Config &c) { return c.stomp.tallest; },
         [](Config &c, float v) { c.stomp.tallest = v; }, false},
        {"particles", "Dust and stars", {0, 1}, [](const Config &c) { return (float)c.particles; }, [](Config &c, float v) { c.particles = v != 0; }, true},
        {"meter", "Power meter", {0, 1}, [](const Config &c) { return (float)c.meter; }, [](Config &c, float v) { c.meter = v != 0; }, true},
        {"autotake", "Mario when a game starts", {0, 1}, [](const Config &c) { return (float)c.autotake; },
         [](Config &c, float v) { c.autotake = v != 0; }, true},
        {"scale", "World scale", {1, 1.25f, 1.5f, 2, 2.5f}, [](const Config &c) { return c.scale; }, [](Config &c, float v) { c.scale = v; }, false},
    };
    return all;
}

static std::string value_text(const Option &o, float v, bool for_file) {
    if (o.flag) return for_file ? (v != 0 ? "1" : "0") : (v != 0 ? "on" : "off");
    char buf[32];
    snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

std::string option_button(const Option &o, const Config &c) { return std::string(o.label) + ": " + value_text(o, o.get(c), false); }

void step_option(const Option &o, Config &c) {
    auto next = std::ranges::upper_bound(o.steps, o.get(c));
    o.set(c, next == o.steps.end() ? o.steps.front() : *next);
}

std::string write_config(const std::string &text, const Config &c) {
    std::string out, eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::vector<bool> written(game_options().size());
    for (size_t start = 0; start < text.size();) {
        size_t end = text.find('\n', start);
        end = end == std::string::npos ? text.size() : end + 1;
        std::string line = text.substr(start, end - start);
        start = end;
        size_t eq = line.find('=');
        for (size_t i = 0; eq != std::string::npos && i < written.size(); i++) {
            const Option &o = game_options()[i];
            if (line.compare(0, eq, o.key)) continue;
            std::string ending = line.substr(line.find_last_not_of("\r\n") + 1);
            line = std::string(o.key) + "=" + value_text(o, o.get(c), true) + ending;
            written[i] = true;
        }
        out += line;
    }
    if (!out.empty() && out.back() != '\n') out += eol;
    for (size_t i = 0; i < written.size(); i++)
        if (!written[i]) out += std::string(game_options()[i].key) + "=" + value_text(game_options()[i], game_options()[i].get(c), true) + eol;
    return out;
}

}
