#pragma once
#include "config.h"

#include <string>
#include <vector>

namespace sm64nv {

// a setting the player can change from the game, stepping through its values
struct Option {
    const char *key, *label;
    std::vector<float> steps;
    float (*get)(const Config &);
    void (*set)(Config &, float);
    bool flag;
};

const std::vector<Option> &game_options();
std::string option_button(const Option &o, const Config &c);
// the next of its values after the one it has, the first after the last
void step_option(const Option &o, Config &c);
// the settings file with each option's line set from the config, the rest kept
std::string write_config(const std::string &text, const Config &c);

}
