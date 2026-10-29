// Forces the effect batch translation units into the link.
//
// Every batch file defines its registrar with HS_BG_EFFECT_BATCH in an anonymous namespace, so the registrar has
// internal linkage and nothing outside the file references it. A static-library link (the server module is one) then
// drops those objects and Batches() (HsBgEffects.cpp) stays empty, so the running server would have no effect entries
// at all. ForceLinkEffectBatches() calls the anchor each batch file exports for exactly this, which pulls the objects
// in; the glue calls it once at startup, before the first EffectRegistry::Global() use (HsBgGlue.cpp).
//
// This file is compiled into the server module only: the headless bg tests list their sources explicitly (see
// tests/build_tests.ps1) and never reference ForceLinkEffectBatches, so the ones that do not link the batch files
// (bg_combat_tests) stay linkable.

#include "HsBgEffects.h"

namespace Hs
{
    namespace Bg
    {
        void ForceLinkEffectBatches()
        {
            HsBgBatchAnchor_Beast();
            HsBgBatchAnchor_Demon();
            HsBgBatchAnchor_Dragon();
            HsBgBatchAnchor_Elemental();
            HsBgBatchAnchor_Mech();
            HsBgBatchAnchor_Murloc();
            HsBgBatchAnchor_Naga();
            HsBgBatchAnchor_Neutral();
            HsBgBatchAnchor_Pirate();
            HsBgBatchAnchor_Quilboar();
            HsBgBatchAnchor_Heroes();
        }
    }
}
