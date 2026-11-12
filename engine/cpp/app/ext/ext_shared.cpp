// The DLL body of an extension: nothing of its own, it only references the C calls so the linker
// keeps them from the program's static library.
#include "ext.h"
namespace { const void* const keep[] = {reinterpret_cast<const void*>(&amfb_ext_name), reinterpret_cast<const void*>(&amfb_ext_start),
                                        reinterpret_cast<const void*>(&amfb_ext_running), reinterpret_cast<const void*>(&amfb_ext_status),
                                        reinterpret_cast<const void*>(&amfb_ext_output), reinterpret_cast<const void*>(&amfb_ext_stop)}; }
extern "C" const void* const* amfb_ext_keep(void) { return keep; }
