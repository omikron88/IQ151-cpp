// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <vector>
namespace iq151 {
struct Entry { std::string name; bool directory; };
std::vector<Entry> directory(const std::string& path);
void makeDirectory(const std::string& path);
std::string absolutePath(const std::string& path);
const std::string& executableDirectory();
const std::string& defaultSdRoot();
std::string sdPath(const std::string& root,const std::string& request);
}
