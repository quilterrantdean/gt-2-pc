#pragma once
// Compile-time platform selection. All game/scene builders use this contract.
#if defined(GT2_XP_CLASSIC)
#include "gt2view/gl_scene_renderer.h"
namespace gt2view { using SceneRenderer = GlSceneRenderer; }
#elif defined(__EMSCRIPTEN__)
#include "gt2view/webgl_scene_renderer.h"
namespace gt2view { using SceneRenderer = WebGlSceneRenderer; }
#else
#include "gt2view/vk_scene_renderer.h"
namespace gt2view { using SceneRenderer = VkSceneRenderer; }
#endif
