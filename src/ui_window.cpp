// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_window.hpp"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <SDL_syswm.h>
#include <stdexcept>
#include <string>

namespace iq151 {
namespace {
bool attachToParent(SDL_Window* window, SDL_Window* parent) {
#ifdef _WIN32
    SDL_SysWMinfo childInfo{}, parentInfo{};
    SDL_VERSION(&childInfo.version);
    SDL_VERSION(&parentInfo.version);
    if(SDL_GetWindowWMInfo(window,&childInfo) && SDL_GetWindowWMInfo(parent,&parentInfo) &&
       childInfo.subsystem==SDL_SYSWM_WINDOWS && parentInfo.subsystem==SDL_SYSWM_WINDOWS) {
        // SDL2 does not implement SetWindowModalFor on Windows. Set an owner,
        // not WS_CHILD: the debugger must move outside the main client area.
        SetLastError(0);
        LONG_PTR result=SetWindowLongPtr(childInfo.info.win.window,GWLP_HWNDPARENT,
                                        reinterpret_cast<LONG_PTR>(parentInfo.info.win.window));
        if(!result && GetLastError()) throw std::runtime_error("Cannot attach tool window to IQ-151");
        return true;
    }
#endif
    // On X11 this sets WM_TRANSIENT_FOR, without disabling the parent. Some
    // SDL2 drivers (dummy/Wayland) do not support a native parent relationship.
    if(SDL_SetWindowModalFor(window,parent)==0) return true;
    SDL_ClearError();
    return false;
}
}
UiWindow::UiWindow(SDL_Window* parent,const char* title,int width,int height,int minimumWidth,int minimumHeight) {
    ImGuiContext* previous=ImGui::GetCurrentContext();
    try {
        int x,y,w,h;
        SDL_GetWindowPosition(parent,&x,&y);SDL_GetWindowSize(parent,&w,&h);
        window_=SDL_CreateWindow(title,x+(w-width)/2,y+(h-height)/2,width,height,
            SDL_WINDOW_HIDDEN|SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI|SDL_WINDOW_UTILITY|SDL_WINDOW_SKIP_TASKBAR);
        if(!window_) throw std::runtime_error(SDL_GetError());
        SDL_SetWindowMinimumSize(window_,minimumWidth,minimumHeight);
        renderer_=SDL_CreateRenderer(window_,-1,SDL_RENDERER_ACCELERATED);
        if(!renderer_) renderer_=SDL_CreateRenderer(window_,-1,SDL_RENDERER_SOFTWARE);
        if(!renderer_) throw std::runtime_error(SDL_GetError());
        // An accelerated SDL renderer can recreate the native window. Attach
        // the owner afterwards so WM_TRANSIENT_FOR / HWND ownership survives.
        nativeParent_=attachToParent(window_,parent);
        context_=ImGui::CreateContext();
        ImGui::SetCurrentContext(context_);
        ImGui::GetIO().IniFilename=nullptr;
        // Each context must own its own font atlas / renderer texture.
        ImGui::StyleColorsLight();
        ImGui::GetStyle().WindowRounding=0;ImGui::GetStyle().FrameRounding=2;
        platformBackend=ImGui_ImplSDL2_InitForSDLRenderer(window_,renderer_);
        if(platformBackend) rendererBackend=ImGui_ImplSDLRenderer2_Init(renderer_);
        if(!platformBackend||!rendererBackend) throw std::runtime_error("Cannot initialize tool window GUI");
        ImGui::SetCurrentContext(previous);
    } catch(...) {
        destroy();ImGui::SetCurrentContext(previous);throw;
    }
}
void UiWindow::destroy() {
    if(context_) {
        ImGuiContext* previous=ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(context_);
        if(rendererBackend) ImGui_ImplSDLRenderer2_Shutdown();
        if(platformBackend) ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext(context_);
        ImGui::SetCurrentContext(previous==context_?nullptr:previous);
        context_=nullptr;
    }
    if(renderer_) {SDL_DestroyRenderer(renderer_);renderer_=nullptr;}
    if(window_) {SDL_DestroyWindow(window_);window_=nullptr;}
}
bool UiWindow::hasNativeParent(SDL_Window* parent) const {
    if(!nativeParent_) return false;
    SDL_SysWMinfo childInfo{},parentInfo{};
    SDL_VERSION(&childInfo.version);SDL_VERSION(&parentInfo.version);
    if(!SDL_GetWindowWMInfo(window_,&childInfo)||!SDL_GetWindowWMInfo(parent,&parentInfo)) return false;
#ifdef _WIN32
    if(childInfo.subsystem==SDL_SYSWM_WINDOWS&&parentInfo.subsystem==SDL_SYSWM_WINDOWS)
        return GetWindowLongPtr(childInfo.info.win.window,GWLP_HWNDPARENT)==reinterpret_cast<LONG_PTR>(parentInfo.info.win.window);
#endif
#ifdef SDL_VIDEO_DRIVER_X11
    if(childInfo.subsystem==SDL_SYSWM_X11&&parentInfo.subsystem==SDL_SYSWM_X11) {
        // Verify the OS property, not merely SDL's earlier return code: a
        // renderer that recreates its window can silently discard this hint.
        void* xlib=SDL_LoadObject("libX11.so.6");
        if(!xlib) xlib=SDL_LoadObject("libX11.so");
        if(!xlib) return false;
        typedef int (*TransientFor)(Display*,Window,Window*);
        auto query=reinterpret_cast<TransientFor>(SDL_LoadFunction(xlib,"XGetTransientForHint"));
        Window owner=0;
        bool linked=query&&query(childInfo.info.x11.display,childInfo.info.x11.window,&owner)&&owner==parentInfo.info.x11.window;
        SDL_UnloadObject(xlib);return linked;
    }
#endif
    return nativeParent_;
}
void UiWindow::show() { SDL_ShowWindow(window_);SDL_RaiseWindow(window_); }
void UiWindow::hide() { SDL_HideWindow(window_); }
void UiWindow::begin() {
    ImGui_ImplSDLRenderer2_NewFrame();ImGui_ImplSDL2_NewFrame();ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0,0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
}
void UiWindow::present() {
    ImGui::Render();
    SDL_SetRenderDrawColor(renderer_,230,230,230,255);SDL_RenderClear(renderer_);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(),renderer_);SDL_RenderPresent(renderer_);
}
}
