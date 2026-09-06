#include "Companion.h"

// Torch's CLI and web entry points each own this process-wide pointer. The
// embedded 3DS adapter is a third entry point, so it must provide the same
// definition without linking either host executable.
Companion* Companion::Instance = nullptr;
