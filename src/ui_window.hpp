// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL.h>
#include "imgui.h"

namespace iq151 {
// ImGui's SDL/renderer backends store per-window data in the current context.
class ImGuiContextScope {
    ImGuiContext* previous;
public:
    explicit ImGuiContextScope(ImGuiContext* context) : previous(ImGui::GetCurrentContext()) { ImGui::SetCurrentContext(context); }
    ~ImGuiContextScope() { ImGui::SetCurrentContext(previous); }
    ImGuiContextScope(const ImGuiContextScope&) = delete;
    ImGuiContextScope& operator=(const ImGuiContextScope&) = delete;
};

// An owned top-level tool window, rather than a panel clipped to the emulator.
// Kept hidden on close so its size/position and ImGui editor state survive reopening.
class UiWindow {
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    ImGuiContext* context_ = nullptr;
    bool platformBackend = false, rendererBackend = false, nativeParent_ = false;
    void destroy();
public:
    UiWindow(SDL_Window* parent, const char* title, int width, int height, int minimumWidth, int minimumHeight);
    ~UiWindow() { destroy(); }
    UiWindow(const UiWindow&) = delete;
    UiWindow& operator=(const UiWindow&) = delete;
    SDL_Window* window() const { return window_; }
    SDL_Renderer* renderer() const { return renderer_; }
    ImGuiContext* context() const { return context_; }
    bool hasNativeParent(SDL_Window* parent) const;
    Uint32 id() const { return SDL_GetWindowID(window_); }
    void show();
    void hide();
    void begin();
    void present();
};
}
