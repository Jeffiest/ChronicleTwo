#pragma once

// Retail Exit__2 is a separate symbol from Exit; the port terminates through the SDK's Exit.
#define Exit__2 Exit

int VSyncCallBack(int field);
void WaitVSync(int start, int frames);
void mgTimeFrameBegin();
float mgTimeFrameFields();
