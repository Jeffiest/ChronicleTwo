#pragma once

// mainloop.cpp redeclares MainBuffer static after a header declares it extern, so MWCC
// gives this unit its own copy (MainBuffer__2 in the retail table). clang's -fms-extensions
// keeps it global, where it collides with dng_main.cpp's MainBuffer in the merge; renamed
// apart here.
#define MainBuffer MainLoop_MainBuffer
