// SPDX-License-Identifier: GPL-3.0-or-later
#include "iq151/files.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cctype>
#include <stdexcept>
#include <sys/stat.h>
#include <dirent.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif
namespace iq151 {
void makeDirectory(const std::string& path) {
#ifdef _WIN32
    int r=_mkdir(path.c_str());
#else
    int r=mkdir(path.c_str(),0700);
#endif
    if(r&&errno!=EEXIST) throw std::runtime_error("Cannot create directory: "+path);
}
std::string absolutePath(const std::string& path) {
#ifdef _WIN32
    char* p=_fullpath(nullptr,path.c_str(),0);
#else
    char* p=realpath(path.c_str(),nullptr);
#endif
    if(!p) throw std::runtime_error("Cannot resolve path: "+path);
    std::string result(p);std::free(p);return result;
}
std::vector<Entry> directory(const std::string& path) {
    DIR* d=opendir(path.c_str());if(!d) throw std::runtime_error("Cannot list directory: "+path);
    std::vector<Entry> entries;
    while(dirent* e=readdir(d)) {
        std::string name=e->d_name;if(name==".") continue;
        struct stat st;
        if(stat((path+"/"+name).c_str(),&st)) continue;
        entries.push_back(Entry{name,(st.st_mode&S_IFMT)==S_IFDIR});
    }
    closedir(d);
    std::sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){return a.directory!=b.directory?a.directory>b.directory:a.name<b.name;});
    return entries;
}
std::string sdPath(const std::string& root,const std::string& request) {
    std::string p=request;std::replace(p.begin(),p.end(),'\\','/');
    std::vector<std::string> parts;std::size_t i=0;
    while(i<p.size()) {
        std::size_t e=p.find('/',i);if(e==std::string::npos) e=p.size();std::string s=p.substr(i,e-i);i=e+1;
        if(s=="..") {if(!parts.empty()) parts.pop_back();}
        else if(!s.empty()&&s!=".") {
            if(s.find(':')!=std::string::npos||s.find('\0')!=std::string::npos) throw std::runtime_error("Invalid SD-ROM path");
            parts.push_back(s);
        }
    }
    std::string base=absolutePath(root),candidate=base;
    for(const auto& s:parts) candidate+="/"+s;
    candidate=absolutePath(candidate);
    std::string check=candidate, prefix=base;
#ifdef _WIN32
    std::replace(check.begin(),check.end(),'\\','/');std::replace(prefix.begin(),prefix.end(),'\\','/');
    std::transform(check.begin(),check.end(),check.begin(),::tolower);std::transform(prefix.begin(),prefix.end(),prefix.begin(),::tolower);
#endif
    if(check!=prefix&&(check.size()<=prefix.size()||check.compare(0,prefix.size(),prefix)!=0||check[prefix.size()]!='/'))
        throw std::runtime_error("SD-ROM path escapes its root");
    return candidate;
}
}
