#include "charset.h"
#include "codec.h"

/* The v0.1 fallback is implemented in codec.c. A future build may load
 * codesets.library dynamically without making it a mandatory dependency. */
int amg_charset_module_present(void){return 1;}
