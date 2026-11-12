#define WIN32_LEAN_AND_MEAN
#include "gt2view/gl_scene_renderer.h"

#include <windows.h>
#include <GL/gl.h>

#include "gt2export/png_writer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>

#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_COMBINE
#define GL_COMBINE 0x8570
#endif
#ifndef GL_COMBINE_RGB
#define GL_COMBINE_RGB 0x8571
#endif
#ifndef GL_SOURCE0_RGB
#define GL_SOURCE0_RGB 0x8580
#endif
#ifndef GL_SOURCE1_RGB
#define GL_SOURCE1_RGB 0x8581
#endif
#ifndef GL_OPERAND0_RGB
#define GL_OPERAND0_RGB 0x8590
#endif
#ifndef GL_OPERAND1_RGB
#define GL_OPERAND1_RGB 0x8591
#endif
#ifndef GL_PRIMARY_COLOR
#define GL_PRIMARY_COLOR 0x8577
#endif
#ifndef GL_RGB_SCALE
#define GL_RGB_SCALE 0x8573
#endif
#ifndef GL_CONSTANT_COLOR
#define GL_CONSTANT_COLOR 0x8001
#endif
#ifndef GL_FUNC_ADD
#define GL_FUNC_ADD 0x8006
#endif
#ifndef GL_FUNC_REVERSE_SUBTRACT
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#endif
#ifndef GL_TEXTURE_FILTER_CONTROL
#define GL_TEXTURE_FILTER_CONTROL 0x8500
#endif
#ifndef GL_TEXTURE_LOD_BIAS
#define GL_TEXTURE_LOD_BIAS 0x8501
#endif

namespace gt2view {
namespace {

constexpr uint32_t kGlAnisotropy = 0x84FE;
constexpr uint32_t kGlMaxAnisotropy = 0x84FF;
constexpr uint32_t kBlendAdd = 0x8006;
constexpr uint32_t kBlendReverseSubtract = 0x800B;
constexpr float kStpAlpha = 128.0f / 255.0f;

using WglGetExtensionsStringARB = const char* (__stdcall *)(HDC);
using WglGetExtensionsStringEXT = const char* (__stdcall *)();

template <typename T>
T WglProc(const char* name) {
    PROC proc = wglGetProcAddress(name);
    if (!proc || proc == reinterpret_cast<PROC>(1) || proc == reinterpret_cast<PROC>(2) ||
        proc == reinterpret_cast<PROC>(3) || proc == reinterpret_cast<PROC>(-1)) return nullptr;
    return reinterpret_cast<T>(proc);
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

uint8_t Expand5(uint32_t value) {
    return static_cast<uint8_t>((value << 3) | (value >> 2));
}

uint64_t TextureKey(uint32_t page, uint32_t clutDepth) {
    return (uint64_t(page) << 32) | clutDepth;
}

} // namespace

GlSceneRenderer::GlSceneRenderer(void* deviceContext, void* window)
    : deviceContext_(deviceContext), window_(window), vram_(size_t(kVramWidth) * kVramRows, 0),
      textureEntries_(kTextureCacheSize), textureIds_(kTextureCacheSize, 0) {
    if (!deviceContext_ || !wglGetCurrentContext())
        throw std::runtime_error("OpenGL renderer requires an active WGL context");

    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    int major = 0, minor = 0;
    if (version) std::sscanf(version, "%d.%d", &major, &minor);
    if (major < 1 || (major == 1 && minor < 5))
        throw std::runtime_error(std::string("OpenGL 1.5 is required; driver reports ") +
                                 (version ? version : "no version string"));

    GLint maxTexture = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
    maxTextureSize_ = maxTexture > 0 ? static_cast<uint32_t>(maxTexture) : 0;
    if (maxTextureSize_ < kTextureSide)
        throw std::runtime_error("OpenGL driver cannot allocate a 256 x 256 texture");

    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    anisotropyAvailable_ = HasExtension(extensions, "GL_EXT_texture_filter_anisotropic");
    if (anisotropyAvailable_) {
        GLfloat maximum = 1.0f;
        glGetFloatv(kGlMaxAnisotropy, &maximum);
        maxAnisotropy_ = std::max(1.0f, maximum);
    }

    HDC dc = static_cast<HDC>(deviceContext_);
    const auto getArb = WglProc<WglGetExtensionsStringARB>("wglGetExtensionsStringARB");
    const auto getExt = WglProc<WglGetExtensionsStringEXT>("wglGetExtensionsStringEXT");
    const char* wglExtensions = getArb ? getArb(dc) : (getExt ? getExt() : nullptr);
    swapControlAvailable_ = HasExtension(wglExtensions, "WGL_EXT_swap_control");
    if (swapControlAvailable_) swapInterval_ = WglProc<SwapIntervalProc>("wglSwapIntervalEXT");
    swapControlAvailable_ = swapControlAvailable_ && swapInterval_;

    blendEquation_ = WglProc<BlendEquationProc>("glBlendEquation");
    blendColor_ = WglProc<BlendColorProc>("glBlendColor");
    if (!blendEquation_ || !blendColor_)
        throw std::runtime_error("OpenGL 1.5 driver is missing the required blend entry points");

    glGenTextures(static_cast<GLsizei>(textureIds_.size()), textureIds_.data());
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GEQUAL);
    glDepthMask(GL_TRUE);
    glClearDepth(0.0);
    glDisable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    glDisable(GL_DITHER);
    glDisable(GL_FOG);
    glDisable(GL_LIGHTING);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
    glShadeModel(GL_SMOOTH);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    UpdateExtent();
    SetOptions(options_);

    std::printf("graphics: OpenGL %s, %s (%s), texture cache %u x 256x256",
                version ? version : "unknown", vendor ? vendor : "unknown",
                renderer ? renderer : "unknown", kTextureCacheSize);
    if (anisotropyAvailable_) std::printf(", anisotropy %.1fx", double(maxAnisotropy_));
    else std::printf(", no anisotropic filtering extension");
    std::printf(", WGL swap control %s\n", swapControlAvailable_ ? "available" : "unavailable");
}

GlSceneRenderer::~GlSceneRenderer() {
    if (!textureIds_.empty() && wglGetCurrentContext())
        glDeleteTextures(static_cast<GLsizei>(textureIds_.size()), textureIds_.data());
}

void GlSceneRenderer::SetVertices(uint32_t first, const std::vector<SceneVertex>& vertices) {
    const uint64_t end = uint64_t(first) + vertices.size();
    if (end > SceneRendererState::kMaxVertices)
        throw std::runtime_error("OpenGL vertex buffer range exceeds the scene limit");
    if (vertices.empty()) return;
    if (vertices_.size() < end) vertices_.resize(static_cast<size_t>(end));
    std::copy(vertices.begin(), vertices.end(), vertices_.begin() + first);
}

void GlSceneRenderer::UploadVram(uint32_t firstRow, uint32_t rowCount, const uint16_t* words) {
    if (!words && rowCount) throw std::runtime_error("OpenGL VRAM upload received a null pointer");
    if (uint64_t(firstRow) + rowCount > kVramRows)
        throw std::runtime_error("OpenGL VRAM upload is outside the emulated texture memory");
    if (!rowCount) return;
    std::copy_n(words, size_t(rowCount) * kVramWidth, vram_.data() + size_t(firstRow) * kVramWidth);
    InvalidateTextures(firstRow, rowCount);
}

void GlSceneRenderer::UploadExternalTexture(uint32_t, uint32_t, const uint32_t*) {
    // External RGBA textures are used by optional imported-model/mod paths.
    // The XP Classic profile deliberately excludes those assets.
}

void GlSceneRenderer::InvalidateTextures(uint32_t firstRow, uint32_t rowCount) {
    const uint64_t end = uint64_t(firstRow) + rowCount;
    for (auto& entry : textureEntries_) {
        if (!entry.occupied) continue;
        const uint32_t imageRow = entry.page >> 16;
        const uint32_t paletteRow = (entry.clutDepth & 0x0fffffffu) >> 16;
        if ((firstRow < imageRow + kTextureSide && end > imageRow) ||
            (firstRow <= paletteRow && end > paletteRow)) entry.dirty = true;
    }
}

void GlSceneRenderer::UpdateExtent() {
    RECT client{};
    if (window_ && GetClientRect(static_cast<HWND>(window_), &client)) {
        extent_.width = static_cast<uint32_t>(std::max<LONG>(0, client.right - client.left));
        extent_.height = static_cast<uint32_t>(std::max<LONG>(0, client.bottom - client.top));
    }
}

void GlSceneRenderer::SetOptions(const RenderOptions& options) {
    const bool mipmapsChanged = options_.mipmaps != options.mipmaps;
    options_ = options;
    if (mipmapsChanged)
        for (auto& entry : textureEntries_) if (entry.occupied) entry.dirty = true;
    if (swapControlAvailable_) {
        if (!swapInterval_(options_.vsync ? 1 : 0))
            std::printf("graphics: driver rejected the requested VSync setting\n");
    } else if (!warnedSwapControl_) {
        std::printf("graphics: WGL_EXT_swap_control unavailable; VSync is controlled by the display driver\n");
        warnedSwapControl_ = true;
    }
}

void GlSceneRenderer::SetFoveation(int level) {
    if (level) throw std::runtime_error("Foveation is not available in the Windows XP renderer");
}

void GlSceneRenderer::SetScissor(const DrawItem& item) {
    if (item.scissor[2] > item.scissor[0] && item.scissor[3] > item.scissor[1]) {
        const int width = static_cast<int>(extent_.width);
        const int height = static_cast<int>(extent_.height);
        const int x0 = std::clamp(static_cast<int>(std::lround(item.scissor[0] * width)), 0, width);
        const int y0 = std::clamp(static_cast<int>(std::lround(item.scissor[1] * height)), 0, height);
        const int x1 = std::clamp(static_cast<int>(std::lround(item.scissor[2] * width)), x0, width);
        const int y1 = std::clamp(static_cast<int>(std::lround(item.scissor[3] * height)), y0, height);
        glScissor(x0, height - y1, x1 - x0, y1 - y0);
        glEnable(GL_SCISSOR_TEST);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
}

void GlSceneRenderer::SetProjection(const DrawItem& item, uint32_t flags) {
    float matrix[16];
    std::memcpy(matrix, item.mvp, sizeof(matrix));
    const float tier = 1.0f + float((flags >> kNearTierShift) & 3u) * 2.0e-5f;
    // Shared scene matrices use Vulkan clip space (Y down, Z in [0,1]).
    // Convert to conventional OpenGL clip space without changing W.
    for (int col = 0; col < 4; ++col) {
        const int i = col * 4;
        matrix[i + 1] = -matrix[i + 1];
        matrix[i + 2] = 2.0f * tier * matrix[i + 2] - matrix[i + 3];
    }
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(matrix);
}

void GlSceneRenderer::ApplyTextureOptions(bool sceneItem) {
    const bool smooth = sceneItem && options_.smoothTextures;
    const bool mipmaps = sceneItem && options_.mipmaps;
    const GLenum mag = smooth ? GL_LINEAR : GL_NEAREST;
    const GLenum min = mipmaps ? (smooth ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST)
                               : (smooth ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(mag));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(min));
    const float requested = sceneItem ? float(options_.anisotropy) : 0.0f;
    if (anisotropyAvailable_) {
        const float use = std::clamp(requested, 1.0f, maxAnisotropy_);
        glTexParameterf(GL_TEXTURE_2D, kGlAnisotropy, use);
    }
}

void GlSceneRenderer::DecodeTexture(const TextureEntry& entry, std::vector<uint8_t>& rgba) const {
    rgba.resize(size_t(kTexturePixels) * 4);
    const uint32_t pageX = entry.page & 0xffffu;
    const uint32_t pageY = entry.page >> 16;
    const uint32_t clutX = entry.clutDepth & 0xffffu;
    const uint32_t clutY = (entry.clutDepth & 0x0fffffffu) >> 16;
    const uint32_t depth = entry.clutDepth >> 28;
    const auto word = [&](uint32_t x, uint32_t y) -> uint32_t {
        if (x >= kVramWidth || y >= kVramRows) return 0;
        return vram_[size_t(y) * kVramWidth + x];
    };

    for (uint32_t y = 0; y < kTextureSide; ++y) {
        for (uint32_t x = 0; x < kTextureSide; ++x) {
            uint32_t texel = 0;
            if (depth == 0) {
                const uint32_t packed = word(pageX + x / 4, pageY + y);
                const uint32_t index = (packed >> ((x & 3u) * 4u)) & 15u;
                texel = word(clutX + index, clutY);
            } else if (depth == 1) {
                const uint32_t packed = word(pageX + x / 2, pageY + y);
                const uint32_t index = (packed >> ((x & 1u) * 8u)) & 255u;
                texel = word(clutX + index, clutY);
            } else {
                texel = word(pageX + x, pageY + y);
            }
            const size_t at = (size_t(y) * kTextureSide + x) * 4;
            if (texel == 0) {
                rgba[at] = rgba[at + 1] = rgba[at + 2] = rgba[at + 3] = 0;
            } else {
                rgba[at] = Expand5(texel & 31u);
                rgba[at + 1] = Expand5((texel >> 5) & 31u);
                rgba[at + 2] = Expand5((texel >> 10) & 31u);
                rgba[at + 3] = (texel & 0x8000u) ? 128 : 255;
            }
        }
    }
}

void GlSceneRenderer::UploadTexture(size_t slot) {
    auto& entry = textureEntries_[slot];
    std::vector<uint8_t> rgba;
    DecodeTexture(entry, rgba);
    glBindTexture(GL_TEXTURE_2D, textureIds_[slot]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kTextureSide, kTextureSide, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    uint32_t side = kTextureSide;
    uint32_t level = 0;
    while (options_.mipmaps && side > 1) {
        const uint32_t nextSide = side >> 1;
        std::vector<uint8_t> next(size_t(nextSide) * nextSide * 4);
        for (uint32_t y = 0; y < nextSide; ++y) {
            for (uint32_t x = 0; x < nextSide; ++x) {
                const uint32_t source[4] = {
                    (y * 2 * side + x * 2) * 4,
                    (y * 2 * side + x * 2 + 1) * 4,
                    ((y * 2 + 1) * side + x * 2) * 4,
                    ((y * 2 + 1) * side + x * 2 + 1) * 4
                };
                const size_t target = (size_t(y) * nextSide + x) * 4;
                const uint8_t alphaClass = rgba[source[0] + 3];
                for (int channel = 0; channel < 3; ++channel) {
                    unsigned sum = 0, count = 0;
                    for (uint32_t at : source) {
                        if (rgba[at + 3] != alphaClass || alphaClass == 0) continue;
                        sum += rgba[at + channel];
                        ++count;
                    }
                    next[target + channel] = count ? static_cast<uint8_t>(sum / count) : 0;
                }
                next[target + 3] = alphaClass;
            }
        }
        ++level;
        side = nextSide;
        glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), GL_RGBA, side, side, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, next.data());
        rgba.swap(next);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, options_.mipmaps ? 8 : 0);
    entry.dirty = false;
}

GLuint GlSceneRenderer::TextureFor(uint32_t page, uint32_t clut, uint32_t flags, const DrawItem& item,
                                   bool sceneItem) {
    if (flags & kCarPaint) {
        uint32_t x = clut & 0xffffu;
        uint32_t y = clut >> 16;
        y += item.paint;
        if (x == 224u && item.brakeLit) x = 240u;
        clut = x | (y << 16);
    }
    const uint32_t depth = (flags & kOverlay) ? 2u : ((flags >> 8) & 3u);
    if (flags & kOverlay) clut = 0;
    const uint32_t clutDepth = (clut & 0x0fffffffu) | (depth << 28);
    const uint64_t key = TextureKey(page, clutDepth);
    size_t slot = 0;
    const auto found = textureLookup_.find(key);
    if (found != textureLookup_.end()) {
        slot = found->second;
        ++cachedMaterials_;
    } else {
        ++uncachedMaterials_;
        size_t victim = textureEntries_.size();
        for (size_t i = 0; i < textureEntries_.size(); ++i) {
            if (!textureEntries_[i].occupied) { victim = i; break; }
            if (victim == textureEntries_.size() ||
                textureEntries_[i].lastUse < textureEntries_[victim].lastUse) victim = i;
        }
        if (victim == textureEntries_.size()) return 0;
        slot = victim;
        auto& entry = textureEntries_[slot];
        if (entry.occupied) textureLookup_.erase(TextureKey(entry.page, entry.clutDepth));
        entry.page = page;
        entry.clutDepth = clutDepth;
        entry.occupied = true;
        entry.dirty = true;
        textureLookup_[key] = slot;
    }

    auto& entry = textureEntries_[slot];
    entry.lastUse = ++useClock_;
    if (entry.dirty) UploadTexture(slot);
    glBindTexture(GL_TEXTURE_2D, textureIds_[slot]);
    ApplyTextureOptions(sceneItem);
    return textureIds_[slot];
}

bool GlSceneRenderer::IsClassicTriangle(const SceneVertex& vertex) const {
    constexpr uint32_t unsupported = kExternalTexture | kHandTexture | kHdUi | kSmoothUi |
        kMirrorTexture | kModelNative | kModelReflection | kModelBlend | kModelAtlasTexture;
    return (vertex.flags & unsupported) == 0;
}

void GlSceneRenderer::DrawItemRange(const DrawItem& item, size_t /*itemIndex*/, bool sceneItem) {
    if (!item.vertexCount || uint64_t(item.firstVertex) + item.vertexCount > vertices_.size()) return;
    if (item.space == kSpaceMirrorSource) return;
    const SceneVertex* base = vertices_.data() + item.firstVertex;
    const uint32_t count = item.vertexCount - (item.vertexCount % 3u);
    if (!count) return;

    const int band = DepthBand(item, false);
    const GLdouble minDepth = band == 3 ? 0.875 : band == 2 ? 0.75 : band == 1 ? 0.5 : 0.0;
    const GLdouble maxDepth = band == 3 ? 1.0 : band == 2 ? 0.875 : band == 1 ? 0.75 : 0.5;
    glDepthRange(minDepth, maxDepth);
    glDepthFunc(item.blend == kBlendStockSurfaceEnvironment ? GL_EQUAL : GL_GEQUAL);
    glDepthMask(item.blend == kBlendOpaque || item.blend == kBlendStockSurfaceEnvironment ? GL_TRUE : GL_FALSE);
    SetScissor(item);

    if (item.clearDepth) {
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
    }

    const bool blended = item.blend != kBlendOpaque;
    if (blended) {
        glEnable(GL_BLEND);
        if (item.blend <= 3) {
            if (item.blend == 0) {
                if (blendEquation_) blendEquation_(kBlendAdd);
                if (blendColor_) blendColor_(0.5f, 0.5f, 0.5f, 0.5f);
                glBlendFunc(GL_CONSTANT_COLOR, GL_CONSTANT_COLOR);
            } else if (item.blend == 1) {
                if (blendEquation_) blendEquation_(kBlendAdd);
                glBlendFunc(GL_ONE, GL_ONE);
            } else if (item.blend == 2) {
                if (blendEquation_) blendEquation_(kBlendReverseSubtract);
                glBlendFunc(GL_ONE, GL_ONE);
            } else {
                if (blendEquation_) blendEquation_(kBlendAdd);
                if (blendColor_) blendColor_(0.25f, 0.25f, 0.25f, 0.25f);
                glBlendFunc(GL_CONSTANT_COLOR, GL_ONE);
            }
        } else if (item.blend == kBlendAlpha) {
            if (blendEquation_) blendEquation_(kBlendAdd);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else if (item.blend == kBlendStockSurfaceEnvironment) {
            if (blendEquation_) blendEquation_(kBlendAdd);
            glBlendFunc(GL_ONE, GL_ONE);
        } else {
            glDisable(GL_BLEND);
        }
    } else {
        glDisable(GL_BLEND);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(SceneVertex), base->pos);
    glEnableClientState(GL_COLOR_ARRAY);
    glColorPointer(3, GL_FLOAT, sizeof(SceneVertex), base->color);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glScalef(1.0f / 256.0f, 1.0f / 256.0f, 1.0f);

    size_t first = 0;
    while (first < count) {
        const SceneVertex& vertex = base[first];
        if (!IsClassicTriangle(vertex)) { first += 3; continue; }
        size_t last = first + 3;
        while (last < count) {
            const SceneVertex& next = base[last];
            if (next.flags != vertex.flags || next.page != vertex.page || next.clut != vertex.clut) break;
            last += 3;
        }

        const bool textured = (vertex.flags & (kTextured | kOverlay)) != 0;
        const bool raw = (vertex.flags & kRawTexture) != 0;
        if (item.stpPass == 1) {
            glAlphaFunc(GL_GREATER, 0.75f);
        } else if (item.stpPass == 2) {
            glAlphaFunc(GL_EQUAL, kStpAlpha);
        } else {
            glAlphaFunc(GL_GREATER, 0.0f);
        }

        if (textured) {
            if (TextureFor(vertex.page, vertex.clut, vertex.flags, item, sceneItem) == 0) {
                first = last;
                continue;
            }
            glEnable(GL_TEXTURE_2D);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glTexCoordPointer(2, GL_FLOAT, sizeof(SceneVertex), base->texel);
            if (raw) {
                glDisableClientState(GL_COLOR_ARRAY);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
            } else {
                glEnableClientState(GL_COLOR_ARRAY);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
                glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
                glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
                glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
                glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
                glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
                glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, (vertex.flags & kIntegerModulate) ? 1.0f : (255.0f / 128.0f));
            }
        } else {
            if (item.stpPass == 1) { first = last; continue; }
            glDisable(GL_TEXTURE_2D);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glEnableClientState(GL_COLOR_ARRAY);
        }

        if (vertex.flags & kCullBack) glEnable(GL_CULL_FACE);
        else glDisable(GL_CULL_FACE);
        SetProjection(item, vertex.flags);
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(first), static_cast<GLsizei>(last - first));
        first = last;
    }

    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_GEQUAL);
    glDepthRange(0.0, 1.0);
    glDisable(GL_SCISSOR_TEST);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
}

void GlSceneRenderer::SaveScreenshot(const std::string& path) {
    if (extent_.width == 0 || extent_.height == 0) return;
    const size_t bytes = size_t(extent_.width) * extent_.height * 4;
    std::vector<uint8_t> pixels(bytes);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, static_cast<GLsizei>(extent_.width), static_cast<GLsizei>(extent_.height),
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const size_t rowBytes = size_t(extent_.width) * 4;
    std::vector<uint8_t> flipped(bytes);
    for (uint32_t y = 0; y < extent_.height; ++y)
        std::memcpy(flipped.data() + size_t(y) * rowBytes,
                    pixels.data() + size_t(extent_.height - 1 - y) * rowBytes, rowBytes);
    gt2::WritePngRgba(path, static_cast<int>(extent_.width), static_cast<int>(extent_.height), flipped);
}

void GlSceneRenderer::Draw(const std::vector<DrawItem>& items, const std::string& screenshotPath, size_t sceneItems) {
    if (!wglGetCurrentContext()) throw std::runtime_error("OpenGL context is not current during Draw");
    UpdateExtent();
    if (extent_.width == 0 || extent_.height == 0) return;
    glViewport(0, 0, static_cast<GLsizei>(extent_.width), static_cast<GLsizei>(extent_.height));
    glClearColor(clearColor[0], clearColor[1], clearColor[2], 1.0f);
    glClearDepth(0.0);
    glDepthMask(GL_TRUE);
    glDisable(GL_SCISSOR_TEST);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    cachedMaterials_ = uncachedMaterials_ = 0;
    sceneItems = std::min(sceneItems, items.size());

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    for (size_t i = 0; i < items.size(); ++i) {
        const DrawItem& item = items[i];
        if (item.blend == kBlendNativeEnvironment || item.blend == kBlendNativeSurfaceEnvironment) {
            if (!warnedUnsupported_) {
                std::printf("graphics: optional native material/reflection draws are excluded from XP Classic\n");
                warnedUnsupported_ = true;
            }
            continue;
        }
        DrawItemRange(item, i, i < sceneItems);
    }
    glFlush();
    if (!screenshotPath.empty()) SaveScreenshot(screenshotPath);
    if (!SwapBuffers(static_cast<HDC>(deviceContext_)))
        throw std::runtime_error("SwapBuffers failed while presenting the OpenGL frame");
}

} // namespace gt2view
