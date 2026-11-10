#ifndef OSS_TEST_ASSERTIONS_H
#define OSS_TEST_ASSERTIONS_H
/* These offline tests use assert for both checks and the operations under test. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#endif
