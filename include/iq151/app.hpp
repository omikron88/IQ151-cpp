// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine.hpp"
namespace iq151 {
struct Options {
    Config config;
    bool headless=false,smoke=false,windowSmoke=false,portable=false,configExplicit=false;
    int frames=120,address=0,pc=-1;
    std::string input,snapshot,tape,diskA,diskB,sdRoot="SDRoot",expect,screenshot,state;
};
int frontend(const Options& options);
void screenshot(const Machine& m,const std::string& path);
}
