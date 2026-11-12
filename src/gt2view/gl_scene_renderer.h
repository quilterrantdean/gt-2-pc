#pragma once

#include "gt2view/scene_types.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace gt2view {

// Windows XP renderer. The baseline path uses the OpenGL 1.5 fixed-function
// pipeline and ordinary 2D textures. The window owns the WGL context.
class GlSceneRenderer final : public SceneRendererState {
public:
    struct Size { uint32_t width = 0, height = 0; };
    struct CullingStats { uint32_t tested = 0, culled = 0; };

    GlSceneRenderer(void* deviceContext, void* window);
    ~GlSceneRenderer() override;
    GlSceneRenderer(const GlSceneRenderer&) = delete;
    GlSceneRenderer& operator=(const GlSceneRenderer&) = delete;

    void SetVertices(uint32_t first, const std::vector<SceneVertex>& vertices);
    void UploadVram(uint32_t firstRow, uint32_t rowCount, const uint16_t* words);
    void UploadExternalTexture(uint32_t first, uint32_t count, const uint32_t* rgba) override;

    // Compatibility surface used by shared game code. VR and reconstructed
    // native-model uploads are deliberately unavailable in the XP profile.
    void SetHandVertices(uint32_t, const std::vector<SceneVertex>&) {}
    void UploadHandTexture(uint32_t, uint32_t, const uint32_t*) {}
    void BeginFrameUploads() {}
    void Draw(const std::vector<DrawItem>& items, const std::string& screenshotPath = {}, size_t sceneItems = 0);
    void SetOptions(const RenderOptions& options);
    const RenderOptions& Options() const { return options_; }

    Size Extent() const { return extent_; }
    Size StereoExtent() const { return {}; }
    float AspectRatio() const { return extent_.height ? float(extent_.width) / float(extent_.height) : 4.0f / 3.0f; }
    uint32_t EffectiveMsaa() const { return 1; }
    uint32_t MaxAnisotropy() const { return anisotropyAvailable_ ? static_cast<uint32_t>(maxAnisotropy_) : 1u; }
    int PresentMode() const { return options_.vsync ? 2 : 0; }
    int Foveation() const { return 0; }
    void SetFoveation(int level);
    uint32_t CachedMaterials() const { return cachedMaterials_; }
    uint32_t UncachedMaterials() const { return uncachedMaterials_; }
    double GpuMilliseconds() const { return -1.0; }
    const CullingStats& LastCulling() const { return culling_; }

    float clearColor[3] = {0.227f, 0.243f, 0.275f};

private:
    struct TextureEntry {
        uint32_t page = 0;
        uint32_t clutDepth = 0;
        uint64_t lastUse = 0;
        bool occupied = false;
        bool dirty = true;
    };

    static constexpr uint32_t kTextureCacheSize = 48;
    static constexpr uint32_t kTextureSide = 256;
    static constexpr uint32_t kTexturePixels = kTextureSide * kTextureSide;

    void UpdateExtent();
    void ApplyTextureOptions(bool sceneItem);
    void SetScissor(const DrawItem& item);
    void SetProjection(const DrawItem& item, uint32_t flags);
    uint32_t TextureFor(uint32_t page, uint32_t clut, uint32_t flags, const DrawItem& item,
                      bool sceneItem);
    void UploadTexture(size_t slot);
    void DecodeTexture(const TextureEntry& entry, std::vector<uint8_t>& rgba) const;
    void SaveScreenshot(const std::string& path);
    void DrawItemRange(const DrawItem& item, size_t itemIndex, bool sceneItem, bool mirrorPass = false);
    bool IsClassicTriangle(const SceneVertex& vertex) const;
    void InvalidateTextures(uint32_t firstRow, uint32_t rowCount);

    void* deviceContext_ = nullptr; // HDC; owned by the window
    void* window_ = nullptr;        // HWND; owned by the window
    Size extent_;
    std::vector<SceneVertex> vertices_;
    std::vector<uint16_t> vram_;
    std::vector<TextureEntry> textureEntries_;
    std::vector<uint32_t> textureIds_;
    uint32_t mirrorTextureId_ = 0;
    uint32_t overlayTextureId_ = 0;
    uint32_t externalTextureId_ = 0;
    uint32_t externalTextureFirst_ = 0;
    uint32_t externalTexturePage_ = 0;
    uint32_t externalTextureWidth_ = 0, externalTextureHeight_ = 0;
    bool externalTextureDirty_ = true;
    std::vector<uint32_t> externalTexels_;
    std::unordered_map<uint64_t, size_t> textureLookup_;
    uint64_t useClock_ = 0;
    uint32_t cachedMaterials_ = 0, uncachedMaterials_ = 0;
    uint32_t maxTextureSize_ = 0;
    float maxAnisotropy_ = 1.0f;
    bool anisotropyAvailable_ = false;
    bool swapControlAvailable_ = false;
    bool warnedSwapControl_ = false;
    bool warnedUnsupported_ = false;
    using SwapIntervalProc = int (__stdcall *)(int);
    SwapIntervalProc swapInterval_ = nullptr;
    using BlendEquationProc = void (__stdcall *)(unsigned int);
    using BlendColorProc = void (__stdcall *)(float, float, float, float);
    BlendEquationProc blendEquation_ = nullptr;
    BlendColorProc blendColor_ = nullptr;
    RenderOptions options_;
    CullingStats culling_;
};

} // namespace gt2view
