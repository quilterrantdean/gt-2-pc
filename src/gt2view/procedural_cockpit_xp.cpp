#include "gt2view/procedural_cockpit.h"

namespace gt2view {

// XP Classic intentionally omits the reconstructed cockpit. The original
// in-game camera, car model, HUD and screen-space rear-view mirror remain.
ProceduralCockpit::ProceduralCockpit(SceneRenderer& renderer) : renderer_(renderer) {}

void ProceduralCockpit::Append(std::vector<DrawItem>&, const CockpitFit&, const float*,
                               float, float, float, int, bool, float, float,
                               const std::array<float, 3>*, bool) {}

void ProceduralCockpit::AppendMirror(std::vector<DrawItem>&, const float*, float) {}

} // namespace gt2view
