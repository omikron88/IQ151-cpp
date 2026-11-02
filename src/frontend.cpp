// SPDX-License-Identifier: GPL-3.0-or-later
#include "iq151/app.hpp"
#include "iq151/files.hpp"
#include "iq151/preferences.hpp"
#include "ui_window.hpp"
#include <SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
namespace iq151 {
namespace {
#include "icons.inc"
bool physical(SDL_Scancode code,int& r,int& b) {
    switch(code) {
#include "sdl_keymap.inc"
    default:return false;
    }
}
std::string extension(std::string p) {auto dot=p.find_last_of('.');p=dot==std::string::npos?"":p.substr(dot);for(char& c:p)c=char(std::tolower(static_cast<unsigned char>(c)));return p;}
enum class FileAction {None,LoadTape,SaveTape,LoadBinary,SaveBinary,LoadState,SaveState,DiskA,DiskB,Screenshot};
class App {
    SDL_Window* window=nullptr;
    SDL_Renderer* renderer=nullptr;
    SDL_Texture* screen=nullptr;
    ImGuiContext* mainContext=nullptr;
    std::unique_ptr<UiWindow> settingsWindow,debuggerWindow;
    bool settingsFocus=false,debuggerFocus=false;
    SDL_AudioDeviceID audioDevice=0;
    std::map<std::string,SDL_Texture*> icons;
    std::unique_ptr<Machine> machine;
    Preferences prefs;
    Config edit;
    std::array<char,4096> editSdRoot{},sdBrowsePath{};
    std::string settingsError,sdBrowseDir,sdBrowseError;
    std::vector<Entry> sdFolders;
    Options options;
    bool quit=false,debugger=false,settings=false,about=false,showHelp=false,z80=false;
    bool platformBackend=false,rendererBackend=false;
    bool inputFocused=true,selecting=false;
    ImVec2 screenOrigin,screenSize,selectionStart,selectionEnd;
    int debugAddress=0,memoryAddress=0,tempBreakpoint=-1;
    int loadAddress=0,saveEnd=0xffff;
    std::array<int,5> breakpoints{{-1,-1,-1,-1,-1}};
    std::array<bool,5> bpEnabled{{false,false,false,false,false}};
    std::array<std::pair<int,int>,SDL_NUM_SCANCODES> held;
    std::string error,status="Ready",fileDir;
    FileAction fileAction=FileAction::None;
    std::vector<Entry> entries;
    char filePath[2048]{};
    bool openFilePopup=false,openErrorPopup=false;
    float speed=1;
    ImVec2 settingsBrowsePosition,sdBrowseUsePosition;
    ImVec2 settingsApplyPosition; // Used by the SDL interaction smoke test.
    bool hexInput(const char* label,int& value,int digits) {
        ImGui::SetNextItemWidth(float(digits*10+24));
        bool changed=ImGui::InputScalar(label,ImGuiDataType_S32,&value,nullptr,nullptr,digits==2?"%02X":"%04X",ImGuiInputTextFlags_CharsHexadecimal);
        value&=digits==2?255:65535;return changed;
    }
    void fail(const std::exception& e) {error=e.what();openErrorPopup=true;machine->paused=true;}
    template<class F> void guarded(F f) {try {f();}catch(const std::exception& e){fail(e);}}
    void releaseKeys() {
        machine->cancelPaste();machine->keyboard.reset();selecting=false;
        for(auto& key:held)key.first=-1;
    }
    void showSettings() {
        if(!settings) {
            edit=machine->config;settingsError.clear();
            std::snprintf(editSdRoot.data(),editSdRoot.size(),"%s",edit.sdRoot.c_str());
        }
        settings=true;settingsFocus=true;releaseKeys();
    }
    void showDebugger() {
        debugger=true;debuggerFocus=true;machine->paused=true;
        debugAddress=machine->cpu.getRegPC();releaseKeys();
    }
    void drawToolWindows() {
        if(settings) {
            if(!settingsWindow) settingsWindow.reset(new UiWindow(window,"IQ-151 - Settings",560,520,500,480));
            if(settingsFocus) {settingsWindow->show();settingsFocus=false;}
            ImGuiContextScope context(settingsWindow->context());
            settingsWindow->begin();settingsDialog();settingsWindow->present();
        }
        if(!settings&&settingsWindow) settingsWindow->hide();
        if(debugger) {
            if(!debuggerWindow) debuggerWindow.reset(new UiWindow(window,"IQ-151 - Debugger",760,610,700,500));
            if(debuggerFocus) {debuggerWindow->show();debuggerFocus=false;}
            ImGuiContextScope context(debuggerWindow->context());
            debuggerWindow->begin();debugWindow();debuggerWindow->present();
        }
        if(!debugger&&debuggerWindow) debuggerWindow->hide();
    }
    void synchronizeBreakpoints() {
        auto& c=machine->cpu;c.resetBreakpoints();
        for(int i=0;i<5;++i) if(bpEnabled[i]&&breakpoints[i]>=0)c.setBreakpoint(breakpoints[i],true);
        if(tempBreakpoint>=0)c.setBreakpoint(tempBreakpoint,true);
    }
    void step() {machine->paused=true;machine->step();debugAddress=machine->cpu.getRegPC();}
    void resume() {
        if(machine->cpu.isBreakpoint(machine->cpu.getRegPC())) machine->step();
        machine->paused=false;
    }
    void stepOver() {
        int pc=machine->cpu.getRegPC(),op=machine->memory.read(pc),len;
        disassemble(machine->memory,pc,len);
        if(op==0xcd||(op&0xc7)==0xc4||(op&0xc7)==0xc7) {
            tempBreakpoint=(pc+len)&65535;synchronizeBreakpoints();resume();
        } else step();
    }
    void reset(bool dirty=false) {machine->reset(dirty);machine->paused=false;status="Reset";}
    void browse(FileAction action) {
        fileAction=action;openFilePopup=true;machine->cancelPaste();
        if(fileDir.empty())fileDir=absolutePath(".");
        entries=directory(fileDir);
        const char* name=action==FileAction::SaveState?"state.isn":action==FileAction::SaveTape?"tape.csw":action==FileAction::Screenshot?"screen.ppm":action==FileAction::SaveBinary?"memory.bin":"";
        std::snprintf(filePath,sizeof(filePath),"%s",name);
    }
    void openPath(const std::string& path) {
        std::string ext=extension(path);
        if(ext==".isn")machine->loadSnapshot(path);
        else if(ext==".csw"||ext==".wav")machine->tape.open(path);
        else if(ext==".iqd") {machine->config.disc2=true;machine->floppy.disks[0].open(path,false);}
        else if(ext==".hex")machine->loadHex(path);
        else if(ext==".bas") {auto b=readFile(path);std::string t(b.begin(),b.end());auto esc=t.find(char(27));if(esc!=std::string::npos)t.resize(esc);machine->paste(t);}
        else machine->loadBinary(path,loadAddress);
        status="Opened "+path;
    }
    void fileDialog() {
        if(openFilePopup) {ImGui::OpenPopup("File");openFilePopup=false;}
        ImGui::SetNextWindowSize(ImVec2(650,470),ImGuiCond_FirstUseEver);
        if(!ImGui::BeginPopupModal("File",nullptr)) return;
        ImGui::TextUnformatted(fileDir.c_str());
        if(ImGui::Button("Parent directory")) guarded([&]{fileDir=absolutePath(fileDir+"/..");entries=directory(fileDir);});
        ImGui::SameLine();if(ImGui::Button("Refresh"))guarded([&]{entries=directory(fileDir);});
        ImGui::BeginChild("files",ImVec2(0,280),ImGuiChildFlags_Borders);
        std::string navigate;
        for(const auto& e:entries) {
            std::string label=(e.directory?"[DIR] ":"      ")+e.name;
            if(ImGui::Selectable(label.c_str(),false,ImGuiSelectableFlags_AllowDoubleClick)) {
                if(e.directory)navigate=fileDir+"/"+e.name;
                else std::snprintf(filePath,sizeof(filePath),"%s",e.name.c_str());
            }
        }
        ImGui::EndChild();
        if(!navigate.empty())guarded([&]{fileDir=absolutePath(navigate);entries=directory(fileDir);});
        ImGui::SetNextItemWidth(-1);ImGui::InputText("##filename",filePath,sizeof(filePath));
        if(fileAction==FileAction::LoadBinary||fileAction==FileAction::SaveBinary) {
            hexInput("Start address",loadAddress,4);ImGui::SameLine();
            if(fileAction==FileAction::SaveBinary)hexInput("Last address",saveEnd,4);
        }
        bool saving=fileAction==FileAction::SaveBinary||fileAction==FileAction::SaveTape||fileAction==FileAction::SaveState||fileAction==FileAction::Screenshot;
        if(ImGui::Button(saving?"Save":"Open")) {
            std::string path=filePath;
            if(path.empty()) error="Choose a file";
            else {
                if(path[0]!='/'&&!(path.size()>1&&path[1]==':'))path=fileDir+"/"+path;
                // Explicit second confirmation before replacing existing files.
                if(saving&&std::ifstream(path.c_str()).good())ImGui::OpenPopup("Replace file?");
                else {guarded([&]{performFile(path);});ImGui::CloseCurrentPopup();}
            }
        }
        ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();
        if(ImGui::BeginPopupModal("Replace file?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Replace the existing file?");
            if(ImGui::Button("Replace")) {
                std::string path=filePath;if(path[0]!='/'&&!(path.size()>1&&path[1]==':'))path=fileDir+"/"+path;
                guarded([&]{performFile(path);});ImGui::CloseCurrentPopup();fileAction=FileAction::None;
            }
            ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
        }
        if(fileAction==FileAction::None)ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    void performFile(const std::string& path) {
        auto& m=*machine;
        switch(fileAction) {
            case FileAction::LoadTape:m.tape.open(path);break;
            case FileAction::SaveTape:m.tape.save(path);m.tape.record=true;break;
            case FileAction::LoadBinary:if(extension(path)==".hex")m.loadHex(path);else m.loadBinary(path,loadAddress);break;
            case FileAction::SaveBinary:m.saveBinary(path,loadAddress,saveEnd);break;
            case FileAction::LoadState:m.loadSnapshot(path);break;
            case FileAction::SaveState:m.saveSnapshot(path);break;
            case FileAction::DiskA:case FileAction::DiskB:m.config.disc2=true;m.floppy.disks[fileAction==FileAction::DiskA?0:1].open(path,false);break;
            case FileAction::Screenshot:screenshot(m,path);break;
            default:break;
        }
        status=path;fileAction=FileAction::None;
    }
    void quick(bool save) {
        std::string path=prefs.dir+"/quickslot.isn";
        if(save)machine->saveSnapshot(path);else machine->loadSnapshot(path);
        status=save?"Quick slot saved":"Quick slot loaded";
    }
    void copy(bool selection) {
        std::string text;
        if(!selection)text=machine->screenText();
        else {
            int cols=machine->config.video?64:32;
            float sx=screenSize.x/560,sy=screenSize.y/272;
            float x1=std::min(selectionStart.x,selectionEnd.x),x2=std::max(selectionStart.x,selectionEnd.x);
            float y1=std::min(selectionStart.y,selectionEnd.y),y2=std::max(selectionStart.y,selectionEnd.y);
            int left=std::max(0,int((x1-screenOrigin.x)/sx-20)/(512/cols));
            int right=std::min(cols,int((x2-screenOrigin.x)/sx-20)/(512/cols)+1);
            int top=std::max(0,int((y1-screenOrigin.y)/sy-8)/8),bottom=std::min(32,int((y2-screenOrigin.y)/sy-8)/8+1);
            for(int y=top;y<bottom;++y) {std::string l;for(int x=left;x<right;++x){int c=machine->memory.vram[y*cols+x]&127;l+=c>=32&&c<127?char(c):' ';}while(!l.empty()&&l.back()==' ')l.pop_back();text+=l+'\n';}
        }
        if(SDL_SetClipboardText(text.c_str()))throw std::runtime_error(SDL_GetError());status="Screen copied";
    }
    void paste() {char* p=SDL_GetClipboardText();if(p){machine->paste(p);SDL_free(p);}}
    void menu() {
        auto& m=*machine;
        if(ImGui::BeginMenuBar()) {
            if(ImGui::BeginMenu("File")) {
                if(ImGui::MenuItem("Open LOAD tape..."))browse(FileAction::LoadTape);
                if(ImGui::MenuItem("Open SAVE tape..."))browse(FileAction::SaveTape);
                ImGui::Separator();
                if(ImGui::MenuItem("Load memory / Intel HEX..."))browse(FileAction::LoadBinary);
                if(ImGui::MenuItem("Save memory..."))browse(FileAction::SaveBinary);
                ImGui::Separator();
                if(ImGui::MenuItem("Load snapshot...","F9"))browse(FileAction::LoadState);
                if(ImGui::MenuItem("Save snapshot...","F8"))browse(FileAction::SaveState);
                if(ImGui::MenuItem("Quick save","Ctrl+F8"))quick(true);
                if(ImGui::MenuItem("Quick load","Ctrl+F9"))quick(false);
                if(ImGui::MenuItem("Save screenshot (PPM)..."))browse(FileAction::Screenshot);
                ImGui::Separator();if(ImGui::MenuItem("Exit"))quit=true;
                ImGui::EndMenu();
            }
            if(ImGui::BeginMenu("Control")) {
                if(ImGui::BeginMenu("Tape recorder")) {
                    if(ImGui::MenuItem("Play",nullptr,!m.tape.record))m.tape.record=false;
                    if(ImGui::MenuItem("Record",nullptr,m.tape.record))m.tape.record=true;
                    ImGui::MenuItem("Invert signal",nullptr,&m.tape.inverted);
                    if(ImGui::MenuItem("Rewind"))m.tape.rewind();
                    if(ImGui::MenuItem("Finish recording"))m.tape.close();
                    ImGui::EndMenu();
                }
                if(ImGui::MenuItem("Reset","F7"))reset();
                if(ImGui::MenuItem("Cold reset"))reset(true);
                if(ImGui::MenuItem("Break (BR)","F6"))m.pic.irq(5);
                ImGui::MenuItem("Pause","Ctrl+P",&m.paused);
                if(ImGui::MenuItem("Copy screen to clipboard"))copy(false);
                if(ImGui::MenuItem("Paste from clipboard"))paste();
                if(ImGui::BeginMenu("Speed")) {const float speeds[]={.5f,1,2,4,10,40};for(float s:speeds){char label[16];std::snprintf(label,sizeof(label),"%.1fx",s);if(ImGui::MenuItem(label,nullptr,s==speed))speed=s;}ImGui::EndMenu();}
                ImGui::EndMenu();
            }
            if(ImGui::BeginMenu("Tools")) {
                if(ImGui::MenuItem("Debugger","F12"))showDebugger();
                if(ImGui::MenuItem("Settings..."))showSettings();
                if(ImGui::BeginMenu("Screen color")) {const char* colors[]={"White","Green","Amber"};for(int i=0;i<3;++i)if(ImGui::MenuItem(colors[i],nullptr,prefs.color==i))prefs.color=i;ImGui::EndMenu();}
                if(ImGui::MenuItem("Keyboard / help"))showHelp=true;
                if(ImGui::MenuItem("About"))about=true;
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
    }
    bool iconButton(const char* id,const char* tooltip,const char* icon) {
        ImGui::PushID(id);bool clicked=ImGui::ImageButton("##icon",(ImTextureID)(intptr_t)icons.at(icon),ImVec2(24,24));
        if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",tooltip);ImGui::PopID();return clicked;
    }
    void toolbar() {
        auto& m=*machine;
        if(iconButton("reset","Reset (F7)","reset"))reset();ImGui::SameLine();
        if(iconButton("pause","Pause / continue (Ctrl+P)",m.paused?"run":"pause"))m.paused=!m.paused;ImGui::SameLine();
        if(iconButton("load","Load memory block","binaryopn"))browse(FileAction::LoadBinary);ImGui::SameLine();
        if(iconButton("save","Save memory block","binarysav"))browse(FileAction::SaveBinary);ImGui::SameLine();
        if(iconButton("debug","Debugger (F12)","debugger"))showDebugger();ImGui::SameLine();
        if(iconButton("settings","Settings","settings"))showSettings();ImGui::SameLine();
        if(iconButton("loadstate","Load snapshot (F9)","osn_open"))browse(FileAction::LoadState);ImGui::SameLine();
        if(iconButton("savestate","Save snapshot (F8)","osn_save"))browse(FileAction::SaveState);ImGui::SameLine();
        if(iconButton("break","Break (F6)","nmi"))m.pic.irq(5);
    }
    void navigateSdFolder(const std::string& path) {
        try {
            auto folders=directory(path);std::string absolute=absolutePath(path);
            sdFolders.swap(folders);sdBrowseDir=absolute;sdBrowseError.clear();
            std::snprintf(sdBrowsePath.data(),sdBrowsePath.size(),"%s",absolute.c_str());
        } catch(const std::exception& e) {sdBrowseError=e.what();}
    }
    void openSdFolderBrowser() {
        navigateSdFolder(editSdRoot[0]?editSdRoot.data():prefs.dir+"/SDRoot");
        if(!sdBrowseError.empty())navigateSdFolder(prefs.dir);
        ImGui::OpenPopup("SD-ROM folder");
    }
    void sdFolderBrowser() {
        ImGui::SetNextWindowSize(ImVec2(470,370),ImGuiCond_Appearing);
        if(!ImGui::BeginPopupModal("SD-ROM folder",nullptr,ImGuiWindowFlags_NoResize))return;
        ImGui::SetNextItemWidth(-52);
        bool enter=ImGui::InputText("##directory",sdBrowsePath.data(),sdBrowsePath.size(),ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();if(ImGui::Button("Go")||enter)navigateSdFolder(sdBrowsePath.data());
        if(ImGui::Button("Parent directory"))navigateSdFolder(sdBrowseDir+"/..");
        ImGui::BeginChild("folders",ImVec2(0,220),ImGuiChildFlags_Borders);
        std::string navigate;
        for(const auto& entry:sdFolders) {
            if(!entry.directory||entry.name=="..")continue;
            if(ImGui::Selectable(("[DIR] "+entry.name).c_str()))navigate=sdBrowseDir+"/"+entry.name;
        }
        ImGui::EndChild();
        if(!navigate.empty())navigateSdFolder(navigate);
        if(!sdBrowseError.empty())ImGui::TextWrapped("%s",sdBrowseError.c_str());
        if(ImGui::Button("Use this folder")) {
            std::snprintf(editSdRoot.data(),editSdRoot.size(),"%s",sdBrowseDir.c_str());
            settingsError.clear();ImGui::CloseCurrentPopup();
        }
        ImVec2 useMin=ImGui::GetItemRectMin(),useMax=ImGui::GetItemRectMax();
        sdBrowseUsePosition=ImVec2((useMin.x+useMax.x)*.5f,(useMin.y+useMax.y)*.5f);
        ImGui::SameLine();if(ImGui::Button("Cancel")||ImGui::IsKeyPressed(ImGuiKey_Escape))ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    void applySettings() {
        try {
            Config next=edit;
            next.sdRoot=editSdRoot[0]?editSdRoot.data():prefs.dir+"/SDRoot";
            if(next.sdRoot.find_first_of("\r\n")!=std::string::npos)throw std::runtime_error("SD-ROM folder must be a single line");
            // Reject a file, unreadable location or missing parent before resetting the CPU.
            if(next.sdRoot!=machine->config.sdRoot||next.sdrom) {
                makeDirectory(next.sdRoot);directory(next.sdRoot);next.sdRoot=absolutePath(next.sdRoot);
            }
            machine->config=next;reset();prefs.config=machine->config;
            if(!options.smoke)prefs.save();
            settings=false;
        } catch(const std::exception& e) {settingsError=e.what();}
    }
    void settingsDialog() {
        if(!settings)return;
        ImGui::Begin("Settings",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings);
        ImGui::Combo("Main module",&edit.main,"None\0BASIC 6\0BASIC G\0AMOS\0");
        int mon=edit.monitor-10;ImGui::Combo("Monitor",&mon,"Standard\0Disassembler\0CP/M Komenium\0CP/M Felicie\0");edit.monitor=mon+10;
        ImGui::Combo("Video",&edit.video,"Video 32\0Video 64\0");
        ImGui::Checkbox("64 KB RAM",&edit.mem64);ImGui::Checkbox("Grafik",&edit.grafik);
        ImGui::Checkbox("SD-ROM",&edit.sdrom);ImGui::SameLine();ImGui::Checkbox("SD autorun",&edit.sdAutorun);
        ImGui::TextUnformatted("SD-ROM folder");
        ImGui::SetNextItemWidth(-155);
        if(ImGui::InputTextWithHint("##sdroot",(prefs.dir+"/SDRoot").c_str(),editSdRoot.data(),editSdRoot.size()))settingsError.clear();
        ImGui::SameLine();if(ImGui::Button("Browse..."))openSdFolderBrowser();
        ImVec2 browseMin=ImGui::GetItemRectMin(),browseMax=ImGui::GetItemRectMax();
        settingsBrowsePosition=ImVec2((browseMin.x+browseMax.x)*.5f,(browseMin.y+browseMax.y)*.5f);
        ImGui::SameLine();if(ImGui::Button("Default")){editSdRoot[0]=0;settingsError.clear();}
        ImGui::Checkbox("Disk2",&edit.disc2);ImGui::Checkbox("Sound",&edit.audio);
        ImGui::Checkbox("Smart keyboard (host characters)",&edit.smartkeyboard);
        ImGui::Checkbox("AMOS autorun",&edit.amosAutorun);ImGui::SameLine();ImGui::Checkbox("FEL autorun",&edit.felAutorun);
        ImGui::Separator();ImGui::TextWrapped("Apply resets the machine. ROMs are built into the executable.");
        if(ImGui::Button("Apply and reset"))applySettings();
        ImVec2 buttonMin=ImGui::GetItemRectMin(),buttonMax=ImGui::GetItemRectMax();
        settingsApplyPosition=ImVec2((buttonMin.x+buttonMax.x)*.5f,(buttonMin.y+buttonMax.y)*.5f);
        ImGui::SameLine();if(ImGui::Button("Cancel"))settings=false;
        if(!settingsError.empty())ImGui::TextWrapped("%s",settingsError.c_str());
        sdFolderBrowser();
        ImGui::End();
    }
    void debugWindow() {
        if(!debugger)return;
        auto& m=*machine;auto& c=m.cpu;
        ImGui::Begin("Debugger",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings);
        if(ImGui::Button(m.paused?"Run":"Stop")) {if(m.paused)resume();else m.paused=true;}ImGui::SameLine();
        if(ImGui::Button("Step (F10)"))step();ImGui::SameLine();if(ImGui::Button("Step over (F11)"))stepOver();ImGui::SameLine();
        ImGui::Checkbox("Z80 mnemonics",&z80);
        ImGui::Text("%s  T=%lld  S Z - H - P V C = %02X",m.paused?"Stopped":"Running",static_cast<long long>(m.clock.tstates),c.getFlags());
        int a=c.getRegA(),f=c.getFlags(),bc=c.getRegBC(),de=c.getRegDE(),hl=c.getRegHL(),pc=c.getRegPC(),sp=c.getRegSP();
        if(hexInput("A",a,2))c.setRegA(a);ImGui::SameLine();if(hexInput("F",f,2))c.setFlags(f);ImGui::SameLine();if(hexInput("BC",bc,4))c.setRegBC(bc);ImGui::SameLine();if(hexInput("DE",de,4))c.setRegDE(de);
        if(hexInput("HL",hl,4))c.setRegHL(hl);ImGui::SameLine();if(hexInput("SP",sp,4))c.setRegSP(sp);ImGui::SameLine();if(hexInput("PC",pc,4))c.setRegPC(pc);
        if(ImGui::BeginTable("debugcolumns",2,ImGuiTableFlags_Resizable)) {
            ImGui::TableNextColumn();hexInput("Disassembly",debugAddress,4);ImGui::SameLine();if(ImGui::Button("PC"))debugAddress=c.getRegPC();
            ImGui::BeginChild("disassembly",ImVec2(0,265),ImGuiChildFlags_Borders);
            int p=debugAddress;
            for(int line=0;line<17;++line) {
                int len;std::string text=disassemble(m.memory,p,len,z80);char label[160];
                std::snprintf(label,sizeof(label),"%c %04X  %02X  %s",c.getRegPC()==p?'>':' ',p,m.memory.read(p),text.c_str());
                if(ImGui::Selectable(label,c.isBreakpoint(p))) {c.setBreakpoint(p,!c.isBreakpoint(p));}
                p=(p+len)&65535;
            }
            ImGui::EndChild();ImGui::TableNextColumn();hexInput("Memory",memoryAddress,4);
            ImGui::BeginChild("memory",ImVec2(0,265),ImGuiChildFlags_Borders);
            for(int row=0;row<16;++row) {
                int adr=(memoryAddress+row*8)&65535;char line[100];int n=std::snprintf(line,sizeof(line),"%04X ",adr);
                for(int j=0;j<8;++j)n+=std::snprintf(line+n,sizeof(line)-n," %02X",m.memory.read(adr+j));
                ImGui::TextUnformatted(line);
            }
            ImGui::EndChild();ImGui::EndTable();
        }
        static int pokeAddress=0,pokeValue=0;
        hexInput("Write address",pokeAddress,4);ImGui::SameLine();hexInput("Value",pokeValue,2);ImGui::SameLine();if(ImGui::Button("Write"))m.memory.write(pokeAddress,pokeValue);
        ImGui::Separator();ImGui::TextUnformatted("Execution breakpoints");
        for(int i=0;i<5;++i) {
            ImGui::PushID(i);bool change=ImGui::Checkbox("##enabled",&bpEnabled[i]);ImGui::SameLine();int v=std::max(0,breakpoints[i]);
            change|=hexInput("##address",v,4);if(change){breakpoints[i]=v;synchronizeBreakpoints();}ImGui::PopID();if(i<4)ImGui::SameLine();
        }
        static bool memBP=false;static int memBPAddress=0;
        ImGui::Checkbox("Break on write",&memBP);ImGui::SameLine();hexInput("##writebp",memBPAddress,4);m.memoryBreakpoint=memBP?memBPAddress:-1;
        ImGui::End();
    }
    void keyEvent(const SDL_Event& e) {
        auto& m=*machine;SDL_Scancode code=e.key.keysym.scancode;bool down=e.type==SDL_KEYDOWN;
        if(!down) {
            if(held[code].first>=0){m.keyboard.key(held[code].first,held[code].second,false);held[code].first=-1;}
            if(code==SDL_SCANCODE_LSHIFT||code==SDL_SCANCODE_RSHIFT)m.keyboard.shift=false;
            if(code==SDL_SCANCODE_LCTRL||code==SDL_SCANCODE_RCTRL)m.keyboard.ctrl=false;
            if(code==SDL_SCANCODE_LALT)m.keyboard.fa=false;if(code==SDL_SCANCODE_RALT)m.keyboard.fb=false;
            return;
        }
        if(e.key.repeat)return;
        bool ctrl=(e.key.keysym.mod&KMOD_CTRL)!=0;
        if(!ImGui::GetIO().WantTextInput) {
            if(code==SDL_SCANCODE_F7){reset();return;}if(code==SDL_SCANCODE_F6){m.pic.irq(5);return;}
            if(code==SDL_SCANCODE_F8){if(ctrl)quick(true);else browse(FileAction::SaveState);return;}
            if(code==SDL_SCANCODE_F9){if(ctrl)quick(false);else browse(FileAction::LoadState);return;}
            if(code==SDL_SCANCODE_F12){if(debugger)debugger=false;else showDebugger();return;}
            if(debugger&&code==SDL_SCANCODE_F10){step();return;}if(debugger&&code==SDL_SCANCODE_F11){stepOver();return;}
            if(ctrl&&code==SDL_SCANCODE_P){m.paused=!m.paused;return;}
            if(ctrl&&code==SDL_SCANCODE_V){paste();return;}
        }
        if(ImGui::GetIO().WantTextInput||!inputFocused||fileAction!=FileAction::None)return;
        m.cancelPaste();
        if(code==SDL_SCANCODE_LSHIFT||code==SDL_SCANCODE_RSHIFT){m.keyboard.shift=true;return;}
        if(code==SDL_SCANCODE_LCTRL||code==SDL_SCANCODE_RCTRL){m.keyboard.ctrl=true;return;}
        if(code==SDL_SCANCODE_LALT){m.keyboard.fa=true;return;}if(code==SDL_SCANCODE_RALT){m.keyboard.fb=true;return;}
        int r,b;
        if(physical(code,r,b)) {
            bool textKey=(code>=SDL_SCANCODE_A&&code<=SDL_SCANCODE_0)||(code>=SDL_SCANCODE_SPACE&&code<=SDL_SCANCODE_SLASH);
            if(m.config.smartkeyboard&&textKey&&!ctrl)return;
            m.keyboard.key(r,b,true);held[code]=std::make_pair(r,b);
        }
    }
    void textEvent(const SDL_Event& e) {
        if(!machine->config.smartkeyboard||!inputFocused||ImGui::GetIO().WantTextInput||fileAction!=FileAction::None)return;
        SDL_Scancode code=lastScancode;int r,b;bool shift;
        char c=e.text.text[0];if(c>='a'&&c<='z')c=char(c-'a'+'A');else if(c>='A'&&c<='Z')c=char(c-'A'+'a');
        if(Keyboard::decode(c,r,b,shift)) {
            machine->keyboard.key(r,b,true);machine->keyboard.shift=shift;held[code]=std::make_pair(r,b);
        }
    }
    static Uint32 eventWindow(const SDL_Event& e) {
        switch(e.type) {
        case SDL_WINDOWEVENT:return e.window.windowID;
        case SDL_KEYDOWN:case SDL_KEYUP:return e.key.windowID;
        case SDL_TEXTINPUT:return e.text.windowID;
        case SDL_TEXTEDITING:return e.edit.windowID;
        case SDL_MOUSEMOTION:return e.motion.windowID;
        case SDL_MOUSEBUTTONDOWN:case SDL_MOUSEBUTTONUP:return e.button.windowID;
        case SDL_MOUSEWHEEL:return e.wheel.windowID;
        case SDL_DROPFILE:case SDL_DROPTEXT:return e.drop.windowID;
        default:return 0;
        }
    }
    void processEvent(const SDL_Event& e) {
        // SDL owns drop allocations even when their target is hidden/unknown.
        struct DropGuard {
            char* value;
            ~DropGuard(){if(value)SDL_free(value);}
        } drop{(e.type==SDL_DROPFILE||e.type==SDL_DROPTEXT)?e.drop.file:nullptr};
        if(e.type==SDL_QUIT) {quit=true;return;}
        Uint32 id=eventWindow(e);
        UiWindow* tool=nullptr;
        if(settings&&settingsWindow&&id==settingsWindow->id())tool=settingsWindow.get();
        if(debugger&&debuggerWindow&&id==debuggerWindow->id())tool=debuggerWindow.get();
        if(tool) {
            ImGuiContextScope context(tool->context());
            ImGui_ImplSDL2_ProcessEvent(&e);
            if(e.type==SDL_WINDOWEVENT&&e.window.event==SDL_WINDOWEVENT_CLOSE) {
                if(tool==settingsWindow.get())settings=false;else debugger=false;
                tool->hide();return;
            }
            if(e.type==SDL_KEYDOWN&&!e.key.repeat&&!ImGui::GetIO().WantTextInput) {
                SDL_Scancode key=e.key.keysym.scancode;
                if(key==SDL_SCANCODE_ESCAPE&&!ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId)) {
                    if(tool==settingsWindow.get())settings=false;else debugger=false;
                    tool->hide();
                } else if(tool==debuggerWindow.get()) {
                    if(key==SDL_SCANCODE_F10)step();
                    if(key==SDL_SCANCODE_F11)stepOver();
                    if(key==SDL_SCANCODE_F12){debugger=false;tool->hide();}
                }
            }
            return; // Tool-window typing must never reach the emulated keyboard.
        }
        if(id&&id!=SDL_GetWindowID(window))return;
        ImGuiContextScope context(mainContext);
        ImGui_ImplSDL2_ProcessEvent(&e);
        if(e.type==SDL_WINDOWEVENT) {
            if(e.window.event==SDL_WINDOWEVENT_CLOSE)quit=true;
            if(e.window.event==SDL_WINDOWEVENT_FOCUS_LOST)releaseKeys();
        }
        if(e.type==SDL_KEYDOWN){lastScancode=e.key.keysym.scancode;keyEvent(e);}
        if(e.type==SDL_KEYUP)keyEvent(e);
        if(e.type==SDL_TEXTINPUT)textEvent(e);
        if(e.type==SDL_DROPFILE)openPath(e.drop.file);
    }
    SDL_Scancode lastScancode=SDL_SCANCODE_UNKNOWN;
    void draw() {
        ImGuiContextScope context(mainContext);
        auto& m=*machine;
        ImGui_ImplSDLRenderer2_NewFrame();ImGui_ImplSDL2_NewFrame();ImGui::NewFrame();
        auto& io=ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("IQ-151",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_MenuBar|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoBringToFrontOnFocus);
        inputFocused=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)&&!ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId);
        menu();toolbar();
        ImVec2 available=ImGui::GetContentRegionAvail();available.y=std::max(1.f,available.y-65);
        float factor=std::min(available.x/560.f,available.y/544.f);screenSize=ImVec2(560*factor,544*factor);
        ImVec2 cursor=ImGui::GetCursorPos();ImGui::SetCursorPosX(cursor.x+std::max(0.f,(available.x-screenSize.x)*.5f));screenOrigin=ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)(intptr_t)screen,screenSize);
        if(ImGui::IsItemClicked(0)){inputFocused=true;selecting=true;selectionStart=selectionEnd=io.MousePos;}
        if(selecting) {
            selectionEnd=io.MousePos;
            ImGui::GetWindowDrawList()->AddRect(ImVec2(std::min(selectionStart.x,selectionEnd.x),std::min(selectionStart.y,selectionEnd.y)),ImVec2(std::max(selectionStart.x,selectionEnd.x),std::max(selectionStart.y,selectionEnd.y)),IM_COL32(0,120,215,255));
            if(ImGui::IsMouseReleased(0)){selecting=false;if(std::abs(selectionEnd.x-selectionStart.x)>5&&std::abs(selectionEnd.y-selectionStart.y)>5)copy(true);}
        }
        if(ImGui::BeginPopupContextItem("screenmenu")){if(ImGui::MenuItem("Paste"))paste();if(ImGui::MenuItem("Copy screen"))copy(false);ImGui::EndPopup();}
        ImGui::Text("%s | Video %d | %d KB | %.1fx | %s",m.paused?"PAUSED":"RUN",m.config.video?64:32,m.config.mem64?64:32,speed,m.tape.record?"REC":"PLAY");
        ImGui::SameLine();ImGui::TextColored(m.sd.activity?ImVec4(0,.6f,0,1):ImVec4(.5f,.5f,.5f,1),"SD");
        ImGui::TextUnformatted(status.c_str());
        for(int d=0;d<2;++d) {
            ImGui::PushID(d);if(ImGui::SmallButton(d?"B:":"A:"))browse(d?FileAction::DiskB:FileAction::DiskA);ImGui::SameLine();
            auto& disk=m.floppy.disks[d];std::string label=disk.path.empty()?"Empty":disk.path;
            ImGui::TextUnformatted(label.c_str());
            if(ImGui::BeginPopupContextItem("disk")) {
                ImGui::Checkbox("Write enabled",&disk.writable);
                if(ImGui::MenuItem("Eject")){disk.flush();disk=Disk();}
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::End();
        fileDialog();
        if(openErrorPopup){ImGui::OpenPopup("Error");openErrorPopup=false;}
        if(ImGui::BeginPopupModal("Error",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextWrapped("%s",error.c_str());if(ImGui::Button("OK"))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
        if(about){ImGui::Begin("About IQ-151",&about,ImGuiWindowFlags_AlwaysAutoResize);ImGui::TextUnformatted("IQ-151 | C++11 + SDL2\nPort of jIQ151 by Omikron / tmilata\nGPL-3.0-or-later\nDear ImGui: MIT | SDL2: zlib\nROMs and original toolbar artwork are embedded.");ImGui::End();}
        if(showHelp){ImGui::Begin("Keyboard / help",&showHelp);ImGui::TextUnformatted("F1-F5: IQ function keys | Alt / AltGr: FA / FB\nF6: BR | F7: Reset | Ctrl+P: Pause\nF8 / F9: Save / load snapshot | Ctrl+F8 / F9: Quick slot\nF12: Debugger | F10: Step | F11: Step over\nRight-click screen: Paste | Drag selection: Copy\nDrop CSW / WAV / IQD / HEX / BAS / ISN onto the window.\nRight-click a mounted disk filename for write protection / eject.\nOriginal keyboard maps physical US keys; Smart keyboard uses typed characters.\nTape motor is controlled by the emulated machine.\nSD-ROM files are read from the selected SD root.");ImGui::End();}
        ImGui::Render();SDL_SetRenderDrawColor(renderer,230,230,230,255);SDL_RenderClear(renderer);ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(),renderer);SDL_RenderPresent(renderer);
        drawToolWindows();
    }
    // Exercise actual SDL window IDs, event routing, renderers and context lifetimes.
    // Kept beside the existing --ui-smoke driver; normal sessions never enter it.
    static void windowCheck(bool ok,const char* message) {
        if(!ok)throw std::runtime_error(std::string("SDL windows: ")+message);
    }
    static void pushKey(SDL_Window* target,SDL_Scancode code,bool down=true) {
        SDL_Event event{};event.type=down?SDL_KEYDOWN:SDL_KEYUP;
        event.key.windowID=SDL_GetWindowID(target);event.key.keysym.scancode=code;
        event.key.keysym.sym=SDL_GetKeyFromScancode(code);event.key.state=down?SDL_PRESSED:SDL_RELEASED;
        windowCheck(SDL_PushEvent(&event)==1,"cannot queue keyboard event");
    }
    static void pushWindow(SDL_Window* target,Uint8 type) {
        SDL_Event event{};event.type=SDL_WINDOWEVENT;event.window.windowID=SDL_GetWindowID(target);event.window.event=type;
        windowCheck(SDL_PushEvent(&event)==1,"cannot queue window event");
    }
    static void pushClick(UiWindow& target,ImVec2 position,bool down) {
        SDL_WarpMouseInWindow(target.window(),int(position.x),int(position.y));
        SDL_Event e{};e.type=SDL_MOUSEMOTION;e.motion.windowID=target.id();
        e.motion.x=int(position.x);e.motion.y=int(position.y);SDL_PushEvent(&e);
        e=SDL_Event{};e.type=down?SDL_MOUSEBUTTONDOWN:SDL_MOUSEBUTTONUP;
        e.button.windowID=target.id();e.button.button=SDL_BUTTON_LEFT;
        e.button.state=down?SDL_PRESSED:SDL_RELEASED;e.button.x=int(position.x);e.button.y=int(position.y);
        windowCheck(SDL_PushEvent(&e)==1,"cannot queue tool button click");
    }
    static void renderedWindow(UiWindow& tool) {
        int w,h;windowCheck(SDL_GetRendererOutputSize(tool.renderer(),&w,&h)==0,"renderer output size");
        std::vector<uint32_t> pixels(std::size_t(w)*h);
        windowCheck(SDL_RenderReadPixels(tool.renderer(),nullptr,SDL_PIXELFORMAT_ARGB8888,pixels.data(),w*4)==0,"read tool framebuffer");
        windowCheck(std::count_if(pixels.begin(),pixels.end(),[&](uint32_t p){return p!=pixels[0];})>100,"tool controls were not rendered");
    }
    int64_t smokeClock=0;
    std::string smokeSdRoot;
    Uint32 smokeSettingsID=0,smokeDebuggerID=0;
    void smokeWindows(int frame) {
        auto& m=*machine;
        if(!error.empty())throw std::runtime_error(error);
        switch(frame) {
        case 120:
            windowCheck(m.screenText().find("BASIC-G")!=std::string::npos,"boot text missing");
            showSettings();break;
        case 122: {
            windowCheck(settingsWindow&&settingsWindow->id()!=SDL_GetWindowID(window),"Settings is not a separate SDL window");
            windowCheck(!m.paused,"Settings paused emulation");
            smokeSettingsID=settingsWindow->id();renderedWindow(*settingsWindow);
            const char* driver=SDL_GetCurrentVideoDriver();
            if(driver&&(std::strcmp(driver,"x11")==0||std::strcmp(driver,"windows")==0))
                windowCheck(settingsWindow->hasNativeParent(window),"missing native Settings owner");
            // Reopening must raise the existing window without resetting edits.
            edit.video=1;showSettings();windowCheck(edit.video==1,"reopening discarded Settings edits");
            pushKey(settingsWindow->window(),SDL_SCANCODE_A);
            SDL_Event text{};text.type=SDL_TEXTINPUT;text.text.windowID=smokeSettingsID;text.text.text[0]='x';
            SDL_PushEvent(&text);break;
        }
        case 124:
            windowCheck(std::all_of(m.keyboard.row.begin(),m.keyboard.row.end(),[](int r){return r==255;}),"Settings typing leaked into IQ keyboard");
            windowCheck(settingsWindow->id()==smokeSettingsID,"Settings was duplicated");
            showDebugger();break;
        case 126: {
            windowCheck(debuggerWindow&&debuggerWindow->id()!=smokeSettingsID&&debuggerWindow->id()!=SDL_GetWindowID(window),"Debugger is not a separate SDL window");
            windowCheck(debuggerWindow->context()!=settingsWindow->context()&&debuggerWindow->context()!=mainContext,"ImGui context shared between renderers");
            const char* driver=SDL_GetCurrentVideoDriver();
            if(driver&&(std::strcmp(driver,"x11")==0||std::strcmp(driver,"windows")==0))
                windowCheck(debuggerWindow->hasNativeParent(window),"missing native Debugger owner");
            smokeDebuggerID=debuggerWindow->id();renderedWindow(*debuggerWindow);
            smokeClock=m.clock.tstates;pushKey(debuggerWindow->window(),SDL_SCANCODE_F10);break;
        }
        case 128:
            windowCheck(m.paused&&m.clock.tstates>smokeClock,"F10 in debugger did not step CPU");
            pushKey(debuggerWindow->window(),SDL_SCANCODE_F10,false);
            SDL_SetWindowSize(debuggerWindow->window(),900,650);break;
        case 130: {
            int w,h;SDL_GetRendererOutputSize(debuggerWindow->renderer(),&w,&h);
            windowCheck(w>=900&&h>=650,"resizing debugger did not resize renderer");
            renderedWindow(*debuggerWindow);pushWindow(settingsWindow->window(),SDL_WINDOWEVENT_CLOSE);break;
        }
        case 132:
            windowCheck(!quit&&!settings&&m.config.video==0,"Settings close applied changes or exited emulator");
            windowCheck((SDL_GetWindowFlags(settingsWindow->window())&SDL_WINDOW_SHOWN)==0,"Settings close did not hide window");
            showSettings();break;
        case 134:
            windowCheck(settings&&settingsWindow->id()==smokeSettingsID&&edit.video==m.config.video,"Settings could not reopen with current configuration");
            pushKey(settingsWindow->window(),SDL_SCANCODE_ESCAPE);break;
        case 136:
            windowCheck(!settings,"Escape did not cancel Settings");
            pushWindow(debuggerWindow->window(),SDL_WINDOWEVENT_CLOSE);break;
        case 138:
            windowCheck(!quit&&!debugger,"Debugger close exited emulator");
            pushKey(window,SDL_SCANCODE_F12);break;
        case 140:
            windowCheck(debugger&&debuggerWindow->id()==smokeDebuggerID,"F12 did not reopen existing debugger");
            pushKey(window,SDL_SCANCODE_F12,false);resume();
            pushWindow(window,SDL_WINDOWEVENT_FOCUS_GAINED);pushKey(window,SDL_SCANCODE_A);
            smokeClock=m.clock.tstates;break;
        case 142:
            windowCheck(m.clock.tstates>smokeClock,"emulation did not run while debugger remained open");
            windowCheck(m.keyboard.row[2]!=255,"main-window input stopped working with debugger open");
            pushWindow(window,SDL_WINDOWEVENT_FOCUS_LOST);break;
        case 144:
            windowCheck(std::all_of(m.keyboard.row.begin(),m.keyboard.row.end(),[](int r){return r==255;}),"focus loss left an IQ key held");
            pushKey(debuggerWindow->window(),SDL_SCANCODE_B);break;
        case 146:
            windowCheck(m.keyboard.row[3]==255,"Debugger typing leaked into IQ keyboard");
            // Destroy both child contexts while the main renderer remains usable.
            settings=false;debugger=false;settingsWindow.reset();debuggerWindow.reset();
            windowCheck(SDL_GetWindowFromID(smokeSettingsID)==nullptr&&SDL_GetWindowFromID(smokeDebuggerID)==nullptr,"tool SDL resources survived destruction");
            break;
        case 148:
            showSettings();showDebugger();break;
        case 150:
            windowCheck(settingsWindow&&debuggerWindow&&settingsWindow->id()!=smokeSettingsID&&debuggerWindow->id()!=smokeDebuggerID,"tools could not be recreated after destruction");
            smokeSdRoot=prefs.dir+"/sd-folder-smoke-"+std::to_string(SDL_GetPerformanceCounter())+" data";
            makeDirectory(smokeSdRoot);
            std::snprintf(editSdRoot.data(),editSdRoot.size(),"%s",smokeSdRoot.c_str());
            edit.video=1;edit.sdrom=true;edit.sdAutorun=true;showSettings();
            pushClick(*settingsWindow,settingsBrowsePosition,true);break;
        case 151:
            pushClick(*settingsWindow,settingsBrowsePosition,false);break;
        case 154: {
            ImGuiContextScope context(settingsWindow->context());
            windowCheck(ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId)&&sdBrowseDir==absolutePath(smokeSdRoot),"SD folder browser did not open at typed folder");
            windowCheck(m.config.sdRoot!=absolutePath(smokeSdRoot),"Browsing applied SD folder before confirmation");
            pushClick(*settingsWindow,sdBrowseUsePosition,true);break;
        }
        case 155:
            pushClick(*settingsWindow,sdBrowseUsePosition,false);break;
        case 158: {
            ImGuiContextScope context(settingsWindow->context());
            windowCheck(!ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId)&&editSdRoot.data()==absolutePath(smokeSdRoot),"Folder selection did not return to Settings");
            pushClick(*settingsWindow,settingsApplyPosition,true);break;
        }
        case 159:
            pushClick(*settingsWindow,settingsApplyPosition,false);break;
        case 162:
            windowCheck(!settings&&m.config.video==1&&!m.paused,"Apply/reset button in Settings did not update machine");
            windowCheck(m.config.sdRoot==absolutePath(smokeSdRoot)&&m.sd.root==m.config.sdRoot,"Selected SD folder did not reach machine configuration");
            windowCheck(readFile(m.sd.root+"/__BLOADER.BAS").size()==asset("__BLOADER.BAS").size,"SD loader was not created in selected folder");
            showSettings();windowCheck(editSdRoot.data()==m.config.sdRoot,"Reopening Settings lost SD folder");break;
        case 164: {
            const uint8_t data='x';writeFile(smokeSdRoot+"/not-a-folder",&data,1);
            std::snprintf(editSdRoot.data(),editSdRoot.size(),"%s",(smokeSdRoot+"/not-a-folder").c_str());
            smokeClock=m.clock.tstates;pushClick(*settingsWindow,settingsApplyPosition,true);break;
        }
        case 165:
            pushClick(*settingsWindow,settingsApplyPosition,false);break;
        case 168:
            windowCheck(settings&&!settingsError.empty()&&m.config.sdRoot==absolutePath(smokeSdRoot)&&m.clock.tstates>smokeClock,"Invalid SD folder changed or reset the machine");
            pushKey(settingsWindow->window(),SDL_SCANCODE_ESCAPE);break;
        case 170:
            windowCheck(!settings&&m.sd.root==absolutePath(smokeSdRoot),"Cancelling Settings changed SD root");
            pushWindow(window,SDL_WINDOWEVENT_CLOSE);break;
        case 171:
            windowCheck(quit,"main-window close did not exit emulator");
            settingsWindow.reset();debuggerWindow.reset();
            windowCheck(ImGui::GetCurrentContext()==mainContext,"destroying tools corrupted main context");
            std::remove((smokeSdRoot+"/not-a-folder").c_str());std::remove((smokeSdRoot+"/__BLOADER.BAS").c_str());std::remove(smokeSdRoot.c_str());
            std::cout<<"SDL windows: separate Settings/Debugger, input, resize, SD folder browse/apply/validation, close/reopen and cleanup verified\n";
            break;
        }
    }

public:
    explicit App(const Options& opts) : options(opts) {for(auto& p:held)p=std::make_pair(-1,-1);}
    ~App() {
        if(audioDevice)SDL_CloseAudioDevice(audioDevice);
        settingsWindow.reset();debuggerWindow.reset();
        if(mainContext){ImGui::SetCurrentContext(mainContext);if(rendererBackend)ImGui_ImplSDLRenderer2_Shutdown();if(platformBackend)ImGui_ImplSDL2_Shutdown();ImGui::DestroyContext(mainContext);}
        for(auto& icon:icons)SDL_DestroyTexture(icon.second);
        if(screen)SDL_DestroyTexture(screen);if(renderer)SDL_DestroyRenderer(renderer);if(window)SDL_DestroyWindow(window);SDL_Quit();
    }
    void init() {
        SDL_SetMainReady();if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_TIMER))throw std::runtime_error(SDL_GetError());
        char* dir=(options.portable||options.smoke)?SDL_GetBasePath():SDL_GetPrefPath("omikron88","IQ151-cpp");
        if(!dir)throw std::runtime_error(SDL_GetError());prefs.dir=dir;SDL_free(dir);
        prefs.config=options.config;
        if(!options.smoke) {
            prefs.load();
            if(options.configExplicit) {
                std::string savedRoot=prefs.config.sdRoot;
                prefs.config=options.config;prefs.config.sdRoot=savedRoot;
            }
        }
        prefs.config.sdRoot=prefs.sdRootFor(options.sdRoot);
        machine.reset(new Machine(prefs.config));auto& m=*machine;
        loadAddress=options.address;
        if(!options.snapshot.empty())m.loadSnapshot(options.snapshot);if(!options.input.empty())openPath(options.input);
        if(!options.tape.empty())m.tape.open(options.tape);
        if(!options.diskA.empty()){m.config.disc2=true;m.floppy.disks[0].open(options.diskA,false);}
        if(!options.diskB.empty()){m.config.disc2=true;m.floppy.disks[1].open(options.diskB,false);}
        if(options.pc>=0){m.memory.setBootstrap(false);m.cpu.setRegPC(options.pc);}
        loadAddress=options.address;
        window=SDL_CreateWindow("IQ-151",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,576,720,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
        if(!window)throw std::runtime_error(SDL_GetError());SDL_SetWindowMinimumSize(window,560,650);
        renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED);
        if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
        if(!renderer)throw std::runtime_error(SDL_GetError());
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"0");screen=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,560,272);
        if(!screen)throw std::runtime_error(SDL_GetError());
        mainContext=ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;ImGui::StyleColorsLight();
        auto& style=ImGui::GetStyle();style.WindowRounding=0;style.FrameRounding=2;
        platformBackend=ImGui_ImplSDL2_InitForSDLRenderer(window,renderer);
        if(platformBackend)rendererBackend=ImGui_ImplSDLRenderer2_Init(renderer);
        if(!platformBackend||!rendererBackend)throw std::runtime_error("Cannot initialize SDL GUI");
#define ICON(n) addIcon(#n,icon_##n,icon_##n##_w,icon_##n##_h)
        ICON(reset);ICON(pause);ICON(run);ICON(binaryopn);ICON(binarysav);ICON(debugger);ICON(settings);ICON(nmi);ICON(osn_open);ICON(osn_save);ICON(iq);
#undef ICON
        SDL_Surface* icon=SDL_CreateRGBSurfaceWithFormatFrom(const_cast<unsigned char*>(icon_iq),icon_iq_w,icon_iq_h,32,icon_iq_w*4,SDL_PIXELFORMAT_RGBA32);
        if(icon){SDL_SetWindowIcon(window,icon);SDL_FreeSurface(icon);}
        SDL_AudioSpec want{},have{};want.freq=44100;want.format=AUDIO_S16SYS;want.channels=1;want.samples=1024;
        audioDevice=SDL_OpenAudioDevice(nullptr,0,&want,&have,0);if(audioDevice)SDL_PauseAudioDevice(audioDevice,0);
        SDL_StartTextInput();
    }
    void addIcon(const char* name,const unsigned char* bytes,int w,int h) {
        SDL_Texture* t=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,w,h);
        if(!t||SDL_UpdateTexture(t,nullptr,bytes,w*4))throw std::runtime_error(SDL_GetError());SDL_SetTextureBlendMode(t,SDL_BLENDMODE_BLEND);icons[name]=t;
    }
    int run() {
        init();uint64_t last=SDL_GetPerformanceCounter();double accumulator=0;int rendered=0;
        while(!quit) {
            SDL_Event e;
            while(SDL_PollEvent(&e)) guarded([&]{processEvent(e);});
            uint64_t now=SDL_GetPerformanceCounter();double elapsed=double(now-last)/SDL_GetPerformanceFrequency();last=now;
            accumulator+=std::min(.1,elapsed)*speed;
            if(options.smoke)accumulator=.02;
            guarded([&]{
                while(accumulator>=.02) {
                    accumulator-=.02;machine->frame();
                    if(machine->paused&&tempBreakpoint>=0){tempBreakpoint=-1;synchronizeBreakpoints();showDebugger();}
                    if(audioDevice&&machine->config.audio&&!machine->paused&&speed==1) {
                        if(SDL_GetQueuedAudioSize(audioDevice)<44100)SDL_QueueAudio(audioDevice,machine->audio.data(),Uint32(machine->audio.size()*2));
                    }
                    machine->audio.clear();
                }
                uint32_t colors[]={0xffffffff,0xff80ff80,0xffffbf40};auto pixels=machine->render(colors[prefs.color]);
                if(SDL_UpdateTexture(screen,nullptr,pixels.data(),560*4))throw std::runtime_error(SDL_GetError());draw();
            });
            if(options.windowSmoke) smokeWindows(++rendered);
            else if(options.smoke) {
                ++rendered;
                if(rendered==120&&machine->screenText().find("BASIC-G")==std::string::npos)
                    throw std::runtime_error("SDL smoke: boot text missing");
                if(rendered==180) {
                    if(machine->screenText().find("\n 123\n")==std::string::npos)throw std::runtime_error("SDL physical keyboard did not execute PRINT");
                    machine->config.smartkeyboard=true;
                }
                if(rendered>=120&&rendered<240) {
                    const char* commands[]={"PRINT 123\n","PRINT 456\n"};
                    int part=(rendered-120)/60,offset=(rendered-120)%60;
                    char ch=commands[part][offset/6];
                    SDL_Scancode sc=ch=='\n'?SDL_SCANCODE_RETURN:ch==' '?SDL_SCANCODE_SPACE:SDL_GetScancodeFromKey(ch>='A'&&ch<='Z'?ch-'A'+'a':ch);
                    if(offset%6==0||offset%6==3) {
                        SDL_Event event{};event.type=offset%6==0?SDL_KEYDOWN:SDL_KEYUP;
                        event.key.windowID=SDL_GetWindowID(window);event.key.keysym.scancode=sc;event.key.keysym.sym=SDL_GetKeyFromScancode(sc);
                        event.key.state=offset%6==0?SDL_PRESSED:SDL_RELEASED;
                        SDL_PushEvent(&event);
                        if(part==1&&offset%6==0&&ch!='\n') {
                            event=SDL_Event{};event.type=SDL_TEXTINPUT;event.text.windowID=SDL_GetWindowID(window);event.text.text[0]=ch;SDL_PushEvent(&event);
                        }
                    }
                }
                if(rendered==260) {
                    if(machine->screenText().find("\n 456\n")==std::string::npos)throw std::runtime_error("SDL smart keyboard did not execute PRINT");
                    if(!error.empty())throw std::runtime_error(error);
                    int w,h;SDL_GetRendererOutputSize(renderer,&w,&h);std::vector<uint32_t> pixels(std::size_t(w)*h);
                    if(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,pixels.data(),w*4))throw std::runtime_error(SDL_GetError());
                    if(std::count(pixels.begin(),pixels.end(),0xff000000)<1000)throw std::runtime_error("SDL smoke: screen was not rendered");
                    std::cout<<"SDL smoke: 260 frames, BASIC boot, physical/smart keyboard PRINT and framebuffer verified\n";quit=true;
                }
            }
            SDL_Delay(options.smoke?0:2);
        }
        machine->tape.close();if(!options.smoke){prefs.config=machine->config;prefs.save();}
        if(!options.screenshot.empty())screenshot(*machine,options.screenshot);
        if(!options.state.empty())machine->saveSnapshot(options.state);
        return 0;
    }
};
}
int frontend(const Options& options) {App app(options);return app.run();}
}
