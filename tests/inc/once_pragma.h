/* the C23 operator form of #pragma once; gcc (>= 8) honors it the
 * same way, so a repeat include is skipped */

_Pragma("once")

int once_pragma_hits = 3;