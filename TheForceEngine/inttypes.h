#ifndef _INTTYPES_H_XBOX
#define _INTTYPES_H_XBOX
// Stub for Xbox / MSVC 2005
// Integer types are already available via stdint.h
#include <stdint.h>
#ifndef PRId64
#define PRId64 "I64d"
#endif
#ifndef PRIu64
#define PRIu64 "I64u"
#endif
#ifndef PRIx64
#define PRIx64 "I64x"
#endif
#endif
