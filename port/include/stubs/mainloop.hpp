#pragma once

// mainloop.cpp redeclares MainBuffer static after a header declares it extern, so MWCC
// gives this unit its own copy (MainBuffer__2 in the retail table). clang's -fms-extensions
// keeps it global, where it collides with dng_main.cpp's MainBuffer in the merge; renamed
// apart here.
#define MainBuffer MainLoop_MainBuffer
#define NextLoopNo MainLoop_NextLoopNo
#define LoopNo MainLoop_LoopNo
#define PrevLoopNo MainLoop_PrevLoopNo
#define CaptureScreen MainLoop_CaptureScreen
#define CaptureMode MainLoop_CaptureMode
#define PlayTimeCountFlag MainLoop_PlayTimeCountFlag
#define ActiveSaveData MainLoop_ActiveSaveData
#define SubGameSaveData MainLoop_SubGameSaveData
#define SystemSeBuff MainLoop_SystemSeBuff
#define InfoBuff MainLoop_InfoBuff
#define InitPadTable MainLoop_InitPadTable
#define VSyncCallBack MainLoop_VSyncCallBack
