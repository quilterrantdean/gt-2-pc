// Windows XP / OpenGL 1.5 window backend for the Classic profile.
#define WIN32_LEAN_AND_MEAN
#include "game_window.h"

#include <windows.h>
#include <mmsystem.h>
#include <GL/gl.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#include <vector>

#include "gt2view/gl_scene_renderer.h"

namespace gt2game {
namespace {

using Clock = std::chrono::steady_clock;

class Win32XpWindow final : public WindowBackend {
public:
    Win32XpWindow(const std::string& title, int clientWidth, int clientHeight, bool withRenderer);
    ~Win32XpWindow() override;

    gt2view::SceneRenderer& Renderer() override {
        if (!renderer_) throw std::runtime_error("this window has no renderer");
        return *renderer_;
    }
    void* NativeHandle() const override { return hwnd_; }
    void SetTitle(const std::string& title) override { SetWindowTextA(hwnd_, title.c_str()); }
    bool SetClientSize(int width, int height) override;
    void Pump() override;
    bool Closed() const override { return closed_; }
    void Close() override { closed_ = true; }
    bool Focused() const override { return GetForegroundWindow() == hwnd_; }
    std::vector<int> TakeKeyDowns() override {
        std::vector<int> out;
        out.swap(keyDowns_);
        return out;
    }
    bool KeyDown(int key) const override { return (GetAsyncKeyState(key) & 0x8000) != 0; }
    bool TakeDevicesChanged() override {
        const bool changed = devicesChanged_;
        devicesChanged_ = false;
        return changed;
    }
    void SleepUntil(Clock::time_point t) override;
    bool VBlankTiming(Clock::time_point&, Clock::duration&) const override { return false; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void ToggleFullscreen();
    void CreateOpenGlContext();

    HWND hwnd_ = nullptr;
    HDC dc_ = nullptr;
    HGLRC context_ = nullptr;
    std::unique_ptr<gt2view::GlSceneRenderer> renderer_;
    std::vector<int> keyDowns_;
    bool closed_ = false;
    bool devicesChanged_ = false;
    bool fullscreen_ = false;
    bool canFullscreen_ = false;
    bool timerPeriod_ = false;
    RECT windowedRect_{};
    HANDLE timer_ = nullptr;
};

LRESULT CALLBACK Win32XpWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Win32XpWindow*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    if (msg == WM_DESTROY) {
        if (self) self->closed_ = true;
        return 0;
    }
    if (msg == WM_CLOSE) {
        if (self) self->closed_ = true;
        return 0;
    }
    if (msg == WM_DEVICECHANGE && self) self->devicesChanged_ = true;
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && !(lp & (1 << 30)) && self) {
        if (msg == WM_SYSKEYDOWN && wp == VK_RETURN && (lp & (1 << 29)) && self->canFullscreen_) {
            self->ToggleFullscreen();
            return 0;
        }
        self->keyDowns_.push_back(static_cast<int>(wp));
        if (msg == WM_SYSKEYDOWN && wp == VK_F10) return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

Win32XpWindow::Win32XpWindow(const std::string& title, int clientWidth, int clientHeight, bool withRenderer) {
    HINSTANCE instance = GetModuleHandleA(nullptr);
    WNDCLASSA wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = "gt2game_xp_gl_window";
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        throw std::runtime_error("RegisterClass failed");

    RECT rect{0, 0, clientWidth, clientHeight};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd_ = CreateWindowA(wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
                          CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
                          nullptr, nullptr, instance, nullptr);
    if (!hwnd_) throw std::runtime_error("CreateWindow failed");
    SetWindowLongPtrA(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    const char* windowMode = std::getenv("GT2_WINDOW");
    const bool hidden = windowMode && std::string(windowMode) == "hidden";
    const bool noFocus = hidden || WindowNoFocus() || std::getenv("GT2_NO_FOCUS") != nullptr;
    canFullscreen_ = !hidden && withRenderer;
    if (withRenderer) {
        try {
            CreateOpenGlContext();
        } catch (...) {
            if (context_) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(context_); context_ = nullptr; }
            if (dc_) { ReleaseDC(hwnd_, dc_); dc_ = nullptr; }
            SetWindowLongPtrA(hwnd_, GWLP_USERDATA, 0);
            DestroyWindow(hwnd_); hwnd_ = nullptr;
            throw;
        }
    }

    if (canFullscreen_ && !noFocus && !WindowedMode()) ToggleFullscreen();
    if (hidden) {
        std::printf("window: hidden OpenGL render test\n");
    } else if (noFocus) {
        SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        ShowWindow(hwnd_, SW_SHOW);
    }
    timerPeriod_ = timeBeginPeriod(1) == TIMERR_NOERROR;
}

void Win32XpWindow::CreateOpenGlContext() {
    dc_ = GetDC(hwnd_);
    if (!dc_) throw std::runtime_error("GetDC failed");

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    int format = ChoosePixelFormat(dc_, &pfd);
    if (!format) {
        pfd.cColorBits = 16;
        pfd.cDepthBits = 16;
        format = ChoosePixelFormat(dc_, &pfd);
    }
    if (!format || !SetPixelFormat(dc_, format, &pfd))
        throw std::runtime_error("Could not select an OpenGL-capable pixel format");

    context_ = wglCreateContext(dc_);
    if (!context_) throw std::runtime_error("wglCreateContext failed");
    if (!wglMakeCurrent(dc_, context_))
        throw std::runtime_error("wglMakeCurrent failed");

    renderer_ = std::make_unique<gt2view::GlSceneRenderer>(dc_, hwnd_);
}

bool Win32XpWindow::SetClientSize(int width, int height) {
    if (width < 320 || height < 240 || width > 4096 || height > 2160 || fullscreen_) return false;
    RECT rect{0, 0, width, height};
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrA(hwnd_, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrA(hwnd_, GWL_EXSTYLE));
    if (!AdjustWindowRectEx(&rect, style, FALSE, exStyle)) return false;
    return SetWindowPos(hwnd_, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
}

void Win32XpWindow::ToggleFullscreen() {
    if (!fullscreen_) {
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        if (!GetMonitorInfoA(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor)) return;
        GetWindowRect(hwnd_, &windowedRect_);
        SetWindowLongPtrA(hwnd_, GWL_STYLE, WS_POPUP);
        const RECT& r = monitor.rcMonitor;
        SetWindowPos(hwnd_, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    } else {
        SetWindowLongPtrA(hwnd_, GWL_STYLE, WS_OVERLAPPEDWINDOW);
        SetWindowPos(hwnd_, nullptr, windowedRect_.left, windowedRect_.top,
                     windowedRect_.right - windowedRect_.left,
                     windowedRect_.bottom - windowedRect_.top,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    fullscreen_ = !fullscreen_;
}

Win32XpWindow::~Win32XpWindow() {
    if (timer_) CloseHandle(timer_);
    if (timerPeriod_) timeEndPeriod(1);
    renderer_.reset();
    if (context_) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(context_);
        context_ = nullptr;
    }
    if (dc_ && hwnd_) {
        ReleaseDC(hwnd_, dc_);
        dc_ = nullptr;
    }
    if (hwnd_ && IsWindow(hwnd_)) {
        SetWindowLongPtrA(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
    }
}

void Win32XpWindow::Pump() {
    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

void Win32XpWindow::SleepUntil(Clock::time_point t) {
    if (!timer_) timer_ = CreateWaitableTimerW(nullptr, TRUE, nullptr);
    const auto spin = std::chrono::microseconds(250);
    const auto now = Clock::now();
    if (timer_ && t - now > spin) {
        LARGE_INTEGER due{};
        due.QuadPart = -std::chrono::duration_cast<std::chrono::duration<long long, std::ratio<1, 10000000>>>(t - now - spin).count();
        if (SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE))
            WaitForSingleObject(timer_, INFINITE);
    } else if (!timer_ && t - now > std::chrono::milliseconds(2)) {
        std::this_thread::sleep_until(t - std::chrono::milliseconds(1));
    }
    while (Clock::now() < t) std::this_thread::yield();
}

} // namespace

std::unique_ptr<WindowBackend> CreateWindowBackend(const std::string& title, int clientWidth, int clientHeight) {
    return std::make_unique<Win32XpWindow>(title, clientWidth, clientHeight, true);
}

std::unique_ptr<WindowBackend> CreateInputWindowBackend(const std::string& title, int clientWidth, int clientHeight) {
    return std::make_unique<Win32XpWindow>(title, clientWidth, clientHeight, false);
}

std::unique_ptr<WindowBackend> CreateXrWindowBackend(const std::string&, int, int) {
    throw std::runtime_error("OpenXR/VR is excluded from the Windows XP Classic build");
}

} // namespace gt2game
