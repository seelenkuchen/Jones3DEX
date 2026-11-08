// Added: the windowed/headless test shared by the Babylon column's game code (M7.17a). See
// JonesBabylonWindow.h and the M7.10 screens, which use the same pWindow check.
#include <Jones3D/Platform/Babylon/JonesBabylonWindow.h>

#include <std/Babylon/stdBabylon.h>

extern "C" bool JonesBabylon_HasWindow(void)
{
    StdBabylonContext* pContext = stdBabylon_GetContext();
    return pContext && pContext->pWindow;
}
