#pragma once

// PORT leaves out mg_math.cpp's forward declarations along with the assembly after them, but
// not the C++ that calls them. Check_Point_Poly3 is defined in the unit; port/src/mg_math.cpp
// defines MulMatrix3 in place of the assembly one.
int  Check_Point_Poly3(float x, float y, float x0, float y0, float x1, float y1, float x2, float y2);
void MulMatrix3(float (*matrix)[4], float (*second)[4], float (*third)[4]);
