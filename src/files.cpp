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
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
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
const std::string& executableDirectory() {
    static const std::string result=[]() {
        for(std::size_t size=1024;size<=65536;size*=2) {
            std::vector<char> buffer(size);
#ifdef _WIN32
            DWORD count=GetModuleFileNameA(nullptr,buffer.data(),static_cast<DWORD>(size));
            if(!count)throw std::runtime_error("Cannot locate emulator executable");
#else
            ssize_t count=readlink("/proc/self/exe",buffer.data(),size);
            if(count<0)throw std::runtime_error("Cannot locate emulator executable");
#endif
            if(static_cast<std::size_t>(count)<size) {
                std::string path(buffer.data(),static_cast<std::size_t>(count));
#ifdef _WIN32
                std::size_t slash=path.find_last_of("/\\");
#else
                std::size_t slash=path.find_last_of('/');
#endif
                if(slash==std::string::npos)throw std::runtime_error("Executable has no directory");
                return path.substr(0,slash+1);
            }
        }
        throw std::runtime_error("Executable path is too long");
    }();
    return result;
}
const std::string& defaultSdRoot() {
    static const std::string result=executableDirectory()+"SDRoot";
    return result;
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
    bool same=check==prefix;
    // A selected filesystem root already ends in '/', e.g. / or C:/.
    if(!prefix.empty()&&prefix.back()!='/')prefix+='/';
    if(!same&&check.compare(0,prefix.size(),prefix)!=0)
        throw std::runtime_error("SD-ROM path escapes its root");
    return candidate;
}
}
