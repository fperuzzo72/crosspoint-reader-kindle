#pragma once
// Arduino's filesystem umbrella header. Everything the tree wants from it is
// already provided by the SdFat shim over POSIX, so this just forwards.
#include "SdFat.h"
#include "Stream.h"
