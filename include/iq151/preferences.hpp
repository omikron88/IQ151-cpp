// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine.hpp"
namespace iq151 {
struct Preferences {
    Config config;
    int color=0;
    std::string dir;
    void load();
    void save() const;
    std::string sdRootFor(const std::string& commandLineRoot) const;
};
}
