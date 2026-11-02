// SPDX-License-Identifier: GPL-3.0-or-later
#include "iq151/preferences.hpp"
#include "iq151/files.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace iq151 {
void Preferences::load() {
    std::ifstream f((dir+"/iq151.ini").c_str());
    std::string line;
    while(std::getline(f,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        std::istringstream row(line);std::string key;int value;
        if(!(row>>key))continue;
        if(key=="sdRoot") {std::string path;std::getline(row>>std::ws,path);config.sdRoot=path;continue;}
        if(!(row>>value))continue;
        if(key=="module"&&value>=0&&value<=3)config.main=value;
        if(key=="monitor"&&value>=10&&value<=13)config.monitor=value;
        if(key=="video"&&(value==0||value==1))config.video=value;
        if(key=="mem64")config.mem64=value!=0;
        if(key=="grafik")config.grafik=value!=0;
        if(key=="sdrom")config.sdrom=value!=0;
        if(key=="disc2")config.disc2=value!=0;
        if(key=="audio")config.audio=value!=0;
        if(key=="smart")config.smartkeyboard=value!=0;
        if(key=="sdAutorun")config.sdAutorun=value!=0;
        if(key=="amosAutorun")config.amosAutorun=value!=0;
        if(key=="felAutorun")config.felAutorun=value!=0;
        if(key=="color"&&value>=0&&value<=2)color=value;
    }
}
void Preferences::save() const {
    if(config.sdRoot.find_first_of("\r\n")!=std::string::npos)
        throw std::runtime_error("SD-ROM folder must be a single line");
    std::ofstream f((dir+"/iq151.ini").c_str());
    if(!f)throw std::runtime_error("Cannot save preferences in "+dir);
    f<<"module "<<config.main<<"\nmonitor "<<config.monitor<<"\nvideo "<<config.video
     <<"\nmem64 "<<config.mem64<<"\ngrafik "<<config.grafik<<"\nsdrom "<<config.sdrom<<"\ndisc2 "<<config.disc2
     <<"\naudio "<<config.audio<<"\nsmart "<<config.smartkeyboard<<"\nsdAutorun "<<config.sdAutorun
     <<"\namosAutorun "<<config.amosAutorun<<"\nfelAutorun "<<config.felAutorun<<"\ncolor "<<color
     <<"\nsdRoot "<<(config.sdRoot==defaultSdRoot()?std::string():config.sdRoot)<<'\n';
    if(!f.flush())throw std::runtime_error("Cannot write preferences");
}
std::string Preferences::sdRootFor(const std::string& commandLineRoot) const {
    if(!commandLineRoot.empty())return commandLineRoot;
    return config.sdRoot.empty()?defaultSdRoot():config.sdRoot;
}
}
