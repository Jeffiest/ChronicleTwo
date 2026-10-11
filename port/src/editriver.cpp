#include "common.h"

#include "editriver.hpp"

// Retail forms a cell's address from the data pointer's low half; the pointer is used as it is
// here.
CGridData *CEditGrid::GetFast(int x, int y) {
    return (CGridData *) ((u_char *) data + (x + (y * num_x)) * sizeof(CGridData));
}
