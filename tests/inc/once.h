/* exercises #pragma once: this file deliberately has no include
 * guard. the initialized global would be a redefinition on a second
 * pass, so this compiling at all proves the file ran once */

#pragma once

int once_hits = 41;

#define ONCE_COMPILED 1