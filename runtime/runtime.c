#define _ALTA_RUNTIME_EXPORTING
#include "common.h"

_Alta_global_runtime_type _Alta_global_runtime;

_Alta_runtime_export void _Alta_init_global_runtime() {
  _Alta_global_runtime.inited = _Alta_bool_true;
};
_Alta_runtime_export void _Alta_unwind_global_runtime() {
  _Alta_global_runtime.inited = _Alta_bool_false;
};
