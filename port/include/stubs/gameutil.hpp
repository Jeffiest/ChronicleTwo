#pragma once

// testVUnew is VU0 assembly, so PORT leaves out its static definition in gameutil.cpp but not the
// call in the weighted skinning loop. port/src/gameutil_motion.cpp defines it.
void testVUnew(float (*matrix)[4], float *vertex, float *weight, float *accum, float *out);
