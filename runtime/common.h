#ifndef _ALTA_RUNTIME_COMMON_H
#define _ALTA_RUNTIME_COMMON_H

#if _ALTA_RUNTIME_BUILT_AS_SHARED_LIB
#if defined(_WIN32) || defined(_WIN64)
#if _ALTA_RUNTIME_EXPORTING
#define _Alta_runtime_export __declspec(dllexport)
#else
#define _Alta_runtime_export __declspec(dllimport)
#endif
#else
#define _Alta_runtime_export
#endif
#else
#define _Alta_runtime_export
#endif

#include <stddef.h>
#include <stdlib.h>
#include <setjmp.h>

typedef unsigned char _Alta_bool;
enum _Alta_bool_value {
  _Alta_bool_false = 0,
  _Alta_bool_true = 1
};

typedef struct __Alta_class_info {
  const char* typeName;
} _Alta_class_info;

typedef struct {
  _Alta_bool inited;
} _Alta_global_runtime_type;

_Alta_runtime_export void _Alta_init_global_runtime();
_Alta_runtime_export void _Alta_unwind_global_runtime();

#endif /* _ALTA_RUNTIME_COMMON_H */
