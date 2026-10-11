#include "common.h"

#include "character.hpp"

// The CPU skinning of the motion models lives in dynamicanime.cpp beside the dynamic animation,
// which poses the same models; the character's draw paths run it here just before the model is
// drawn. On the PS2 the skinning happened in VU1 as each model was drawn, and this function only
// remade the bounds.
void PortMgSkinMotionTree(mgCFrame *frame);

PORT_OVERRIDE void CCharacter2::SetDeformMesh() {
    sceVu0FVECTOR max;
    sceVu0FVECTOR min;
    int           i;

    PortMgSkinMotionTree(CObjectFrame::frame);

    // A standalone level of detail is a model of its own, drawn in place of the main one and not
    // linked under it, so it is skinned here too.
    if (lod != NULL && lod_no >= 0 && lod_no < lod_num && lod[lod_no].standalone) {
        PortMgSkinMotionTree(lod[lod_no].frame);
    }

    for (i = 0; i < deform_frame_num; i++) {
        if (deform_frame[i] != NULL) {
            deform_frame[i]->RemakeBBox(max, min);
        }
    }
}
