#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>

#include <cstdio>
#include <cstring>

namespace {

constexpr const char* kWindowClass = "GT2XPGLCheckWindow";
constexpr GLenum kMaxAnisotropyExt = 0x84FF;

using WglGetExtensionsStringARB = const char* (WINAPI*)(HDC);
using WglGetExtensionsStringEXT = const char* (WINAPI*)();
using WglSwapIntervalEXT = BOOL (WINAPI*)(int);
using WglGetSwapIntervalEXT = int (WINAPI*)();
using GlBlendEquation = void (WINAPI*)(GLenum);
using GlBlendColor = void (WINAPI*)(GLfloat, GLfloat, GLfloat, GLfloat);

template <typename T>
T WglProc(const char* name) {
    PROC proc = wglGetProcAddress(name);
    if (!proc || proc == reinterpret_cast<PROC>(1) ||
        proc == reinterpret_cast<PROC>(2) ||
        proc == reinterpret_cast<PROC>(3) ||
        proc == reinterpret_cast<PROC>(-1)) {
        return nullptr;
    }
    T function{};
    static_assert(sizeof(function) == sizeof(proc), "WGL function pointer size mismatch");
    std::memcpy(&function, &proc, sizeof(function));
    return function;
}

bool HasExtension(const char* extensions, const char* wanted) {
    if (!extensions || !wanted || !*wanted) return false;
    const size_t length = std::strlen(wanted);
    const char* at = extensions;
    while ((at = std::strstr(at, wanted)) != nullptr) {
        const bool left = at == extensions || at[-1] == ' ';
        const char after = at[length];
        if (left && (after == '\0' || after == ' ')) return true;
        at += length;
    }
    return false;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcA(hwnd, message, wParam, lParam);
}

bool RegisterHiddenWindowClass(HINSTANCE instance) {
    WNDCLASSA wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    if (RegisterClassA(&wc)) return true;
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

int Run() {
    HINSTANCE instance = GetModuleHandleA(nullptr);
    if (!RegisterHiddenWindowClass(instance)) {
        std::fprintf(stderr, "ERROR: RegisterClass failed (%lu).\n", GetLastError());
        return 1;
    }

    HWND window = CreateWindowA(kWindowClass, "GT2 XP OpenGL check", WS_POPUP,
                                0, 0, 32, 32, nullptr, nullptr, instance, nullptr);
    if (!window) {
        std::fprintf(stderr, "ERROR: CreateWindow failed (%lu).\n", GetLastError());
        return 1;
    }

    HDC dc = GetDC(window);
    if (!dc) {
        std::fprintf(stderr, "ERROR: GetDC failed (%lu).\n", GetLastError());
        DestroyWindow(window);
        return 1;
    }

    PIXELFORMATDESCRIPTOR requested{};
    requested.nSize = sizeof(requested);
    requested.nVersion = 1;
    requested.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    requested.iPixelType = PFD_TYPE_RGBA;
    requested.cColorBits = 24;
    requested.cDepthBits = 24;
    requested.iLayerType = PFD_MAIN_PLANE;

    const int format = ChoosePixelFormat(dc, &requested);
    if (!format || !SetPixelFormat(dc, format, &requested)) {
        std::fprintf(stderr, "ERROR: Could not set an OpenGL pixel format (%lu).\n", GetLastError());
        ReleaseDC(window, dc);
        DestroyWindow(window);
        return 1;
    }

    PIXELFORMATDESCRIPTOR actual{};
    DescribePixelFormat(dc, format, sizeof(actual), &actual);

    HGLRC context = wglCreateContext(dc);
    if (!context || !wglMakeCurrent(dc, context)) {
        std::fprintf(stderr, "ERROR: Could not create or activate an OpenGL context.\n");
        if (context) wglDeleteContext(context);
        ReleaseDC(window, dc);
        DestroyWindow(window);
        return 1;
    }

    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));

    int major = 0;
    int minor = 0;
    if (version) std::sscanf(version, "%d.%d", &major, &minor);

    GLint maxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);

    HDC currentDc = wglGetCurrentDC();
    WglGetExtensionsStringARB getWglExtensionsARB = WglProc<WglGetExtensionsStringARB>("wglGetExtensionsStringARB");
    WglGetExtensionsStringEXT getWglExtensionsEXT = WglProc<WglGetExtensionsStringEXT>("wglGetExtensionsStringEXT");
    const char* wglExtensions = getWglExtensionsARB ? getWglExtensionsARB(currentDc) :
                               (getWglExtensionsEXT ? getWglExtensionsEXT() : nullptr);

    const bool hasAnisotropy = HasExtension(extensions, "GL_EXT_texture_filter_anisotropic");
    GLfloat maxAnisotropy = 1.0f;
    if (hasAnisotropy) glGetFloatv(kMaxAnisotropyExt, &maxAnisotropy);

    const bool hasSwapControl = HasExtension(wglExtensions, "WGL_EXT_swap_control");
    WglSwapIntervalEXT swapInterval = hasSwapControl ? WglProc<WglSwapIntervalEXT>("wglSwapIntervalEXT") : nullptr;
    WglGetSwapIntervalEXT getSwapInterval = hasSwapControl ? WglProc<WglGetSwapIntervalEXT>("wglGetSwapIntervalEXT") : nullptr;
    bool vsyncSet = false;
    int currentSwapInterval = -1;
    GlBlendEquation blendEquation = WglProc<GlBlendEquation>("glBlendEquation");
    GlBlendColor blendColor = WglProc<GlBlendColor>("glBlendColor");
    const bool blendSupport = blendEquation && blendColor;
    if (swapInterval) vsyncSet = swapInterval(1) == TRUE;
    if (getSwapInterval) currentSwapInterval = getSwapInterval();

    std::printf("GT2 PC - Windows XP OpenGL capability check\n");
    std::printf("---------------------------------------------\n");
    std::printf("OpenGL version : %s\n", version ? version : "unavailable");
    std::printf("Vendor         : %s\n", vendor ? vendor : "unavailable");
    std::printf("Renderer       : %s\n", renderer ? renderer : "unavailable");
    std::printf("Pixel format   : %u-bit colour, %u-bit depth, double-buffered\n",
                static_cast<unsigned>(actual.cColorBits), static_cast<unsigned>(actual.cDepthBits));
    std::printf("Max texture    : %d x %d\n", maxTextureSize, maxTextureSize);
    std::printf("OpenGL 1.5     : %s\n", (major > 1 || (major == 1 && minor >= 5)) ? "yes" : "NO");
    std::printf("Anisotropic    : %s", hasAnisotropy ? "yes" : "no");
    if (hasAnisotropy) std::printf(" (maximum %.1fx)", static_cast<double>(maxAnisotropy));
    std::printf("\n");
    std::printf("Blend equations: %s\n", blendEquation ? "available" : "MISSING");
    std::printf("Blend colour   : %s\n", blendColor ? "available" : "MISSING");
    std::printf("WGL swap ctrl  : %s\n", hasSwapControl && swapInterval ? "available" : "unavailable");
    if (currentSwapInterval >= 0) std::printf("VSync interval : %d\n", currentSwapInterval);
    else if (vsyncSet) std::printf("VSync interval : set to 1 (query unavailable)\n");

    const bool software = renderer && (std::strstr(renderer, "GDI Generic") || std::strstr(renderer, "Microsoft"));
    if (software) std::printf("WARNING: the active renderer appears to be software, not the NVIDIA GPU.\n");
    const bool versionSupport = major > 1 || (major == 1 && minor >= 5);
    const bool textureSupport = maxTextureSize >= 256;
    if (!versionSupport) std::printf("FAIL: OpenGL 1.5 is required.\n");
    if (!textureSupport) std::printf("FAIL: a 256 x 256 texture is required; driver limit is %d.\n", maxTextureSize);
    if (!blendSupport) std::printf("FAIL: glBlendEquation and glBlendColor are required.\n");
    if (versionSupport && textureSupport && blendSupport)
        std::printf("PASS: OpenGL fixed-function baseline detected.\n");

    glClearColor(0.15f, 0.20f, 0.25f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const bool swapped = SwapBuffers(dc) == TRUE;
    std::printf("Buffer swap    : %s\n", swapped ? "ok" : "FAILED");

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(context);
    ReleaseDC(window, dc);
    DestroyWindow(window);
    return versionSupport && textureSupport && blendSupport && swapped ? 0 : 2;
}

} // namespace

int main() {
    return Run();
}
