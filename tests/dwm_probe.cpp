#include <windows.h>
#include <dwmapi.h>
#include <stdio.h>
int main() {
    DWM_TIMING_INFO ti{};
    ti.cbSize = sizeof(ti);
    printf("sizeof(DWM_TIMING_INFO) = %u\n", (unsigned)sizeof(ti));
    for (int i = 0; i < 5; i++) {
        DWM_TIMING_INFO t{};
        t.cbSize = sizeof(t);
        const HRESULT hr = DwmGetCompositionTimingInfo(nullptr, &t);
        printf("hr=0x%08X  cFrame=%llu  cFrameDisplayed=%llu  cFramesDisplayed=%llu\n",
               (unsigned)hr, (unsigned long long)t.cFrame,
               (unsigned long long)t.cFrameDisplayed,
               (unsigned long long)t.cFramesDisplayed);
        Sleep(120);
    }
    return 0;
}
