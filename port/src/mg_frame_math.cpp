#include "common.h"

#include <libvu0.h>

#include "mg_drawenv.hpp"
#include "mg_frame.hpp"
#include "mg_math.hpp"

// mgCFrame builds its local and world matrices with VU0 assembly, which PORT leaves out; the
// port's mg_frame.cpp replacement (test1/test2) did not fill these four in, so below PORT every
// child frame kept its stale matrix and nothing moved. CPU skinning reads the bone matrices
// through GetLWMatrix, so the skeleton needs these before a posed model can be skinned; each body
// mirrors the ps2 function around its asm block, with the block itself written in C.

void mgCFrame::GetLocalMatrix(float (*matrix)[4]) {
    if (use_srt) {
        // vmul rows 0-2 by the scale lanes and row 3 by one (vf0w): the scale's w is zero, so the
        // rows of a scaled matrix lose their w.
        for (int row = 0; row < 3; row++) {
            for (int column = 0; column < 4; column++) {
                matrix[row][column] = trans_matrix[row][column] * scale[column];
            }
        }
        sceVu0CopyVector(matrix[3], trans_matrix[3]);

        sceVu0FVECTOR translation;

        if (rot_type & MG_FRAME_ROT_LOCAL_ORIGIN) {
            sceVu0CopyVector(translation, matrix[3]);
            mgZeroVectorW(matrix[3]);
        }

        if (rot_type & MG_FRAME_ROT_APPLY) {
            if (rotation[0] != 0.0f) {
                sceVu0RotMatrixX(matrix, matrix, rotation[0]);
            }

            if (rotation[1] != 0.0f) {
                sceVu0RotMatrixY(matrix, matrix, rotation[1]);
            }

            if (rotation[2] != 0.0f) {
                sceVu0RotMatrixZ(matrix, matrix, rotation[2]);
            }
        }

        if (rot_type & MG_FRAME_ROT_LOCAL_ORIGIN) {
            sceVu0AddVector(matrix[3], translation, position);
            matrix[3][3] = 1.0f;
        } else {
            sceVu0AddVector(matrix[3], matrix[3], position);
            matrix[3][3] = 1.0f;
        }
    } else {
        sceVu0CopyMatrix(matrix, trans_matrix);
    }
}

void mgCFrame::GetLWMatrix(float (*matrix)[4]) {
    float parent_matrix[4][4];
    float local_matrix[4][4];

    if (reference != 0) {
        changed = 1;
    }

    if (changed == 0) {
        mgCFrame *frame = parent;

        if (frame == NULL) {
            sceVu0CopyMatrix(matrix, lw_matrix);
            return;
        }

        mgCFrame *node = frame;

        while (node != NULL) {
            if (node->changed != 0) {
                break;
            }

            frame = node->parent;

            if (frame == NULL) {
                sceVu0CopyMatrix(matrix, lw_matrix);
                return;
            }

            node = frame;
        }
    }

    ClearChildFlag();
    GetLocalMatrix(local_matrix);

    if (parent == NULL) {
        sceVu0CopyMatrix(lw_matrix, local_matrix);
        sceVu0CopyMatrix(matrix, lw_matrix);
        changed = 0;
    } else {
        parent->GetLWMatrix(parent_matrix);
        mgMulMatrix(lw_matrix, parent_matrix, local_matrix);
        sceVu0CopyMatrix(matrix, lw_matrix);
        changed = 0;
    }
}

void mgCFrame::GetLWMatrixTopBottom(float (*matrix)[4]) {
    float parent_matrix[4][4];
    float local_matrix[4][4];

    if (reference != 0) {
        GetLWMatrix(matrix);
        return;
    }

    if (changed == 0) {
        mgCFrame *parent_frame = parent;

        if (parent_frame == 0) {
            sceVu0CopyMatrix(matrix, lw_matrix);
            return;
        }

        if (parent_frame->changed == 0) {
            sceVu0CopyMatrix(matrix, lw_matrix);
            return;
        }
    }

    ClearChildFlag();
    GetLocalMatrix(local_matrix);
    mgCFrame *parent = this->parent;

    if (parent == 0) {
        sceVu0CopyMatrix(lw_matrix, local_matrix);
        sceVu0CopyMatrix(matrix, lw_matrix);
    } else {
        sceVu0CopyMatrix(parent_matrix, parent->lw_matrix);
        mgMulMatrix(lw_matrix, parent_matrix, local_matrix);
        sceVu0CopyMatrix(matrix, lw_matrix);
    }

    changed = 0;
}

void mgCFrame::GetBBoardMatrix(int mode, float (*matrix)[4], mgRENDER_INFO *info) {
    float position[4];
    float to_eye[4];
    float scale_vec[4];
    float world[4][4];
    float pitch[4][4];

    GetLWMatrix(world);
    scale_vec[0] = mgDistVector(world[0]);
    scale_vec[1] = mgDistVector(world[0]);
    scale_vec[2] = mgDistVector(world[0]);
    sceVu0CopyVector(position, world[3]);
    sceVu0UnitMatrix(matrix);

    if (mode & 2) {
        sceVu0SubVector(matrix[2], info->camera_pos, position);
        matrix[2][1] = 0.0f;
        matrix[2][3] = 0.0f;
        sceVu0Normalize(matrix[2], matrix[2]);
        matrix[0][0] = matrix[2][2];
        matrix[0][2] = -matrix[2][0];
    }

    if (mode & 1) {
        sceVu0UnitMatrix(pitch);
        sceVu0SubVector(to_eye, info->camera_pos, position);
        sceVu0Normalize(to_eye, to_eye);
        float eye_y = to_eye[1];
        to_eye[3] = 0.0f;
        to_eye[1] = 0.0f;
        float horizontal = mgDistVector(to_eye);
        pitch[1][1] = horizontal;
        pitch[2][2] = horizontal;
        pitch[1][2] = -eye_y;
        pitch[2][1] = eye_y;
        sceVu0SubVector(matrix[2], info->camera_pos, position);
        matrix[2][1] = 0.0f;
        matrix[2][3] = 0.0f;
        sceVu0Normalize(matrix[2], matrix[2]);
        matrix[0][0] = matrix[2][2];
        matrix[0][2] = -matrix[2][0];
        mgMulMatrix(matrix, matrix, pitch);
    }

    // The billboard's scale is the length of the frame's x axis; vmul scales the rows' xyz, and
    // the rows of a rotation keep a zero w.
    for (int row = 0; row < 3; row++) {
        matrix[row][0] = matrix[row][0] * scale_vec[0];
        matrix[row][1] = matrix[row][1] * scale_vec[1];
        matrix[row][2] = matrix[row][2] * scale_vec[2];
        matrix[row][3] = 0.0f;
    }
    matrix[3][0] = position[0];
    matrix[3][1] = position[1];
    matrix[3][2] = position[2];
    sceVu0CopyMatrix(lw_matrix, matrix);
    changed = 0;
    ClearChildFlag();
}
