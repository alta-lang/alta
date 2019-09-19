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

#ifdef ALTA_RUNTIME_INITIALIZER
#include ALTA_RUNTIME_INITIALIZER
#endif

#ifndef ALTA_RUNTIME_FREESTANDING
#define __STDC_LIMIT_MACROS
#define __STDC_CONSTANT_MACROS

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#endif

#include "definitions.h"

typedef struct __Alta_error_handler_node {
  const char* typeName;
  jmp_buf jumpPoint;
  _Alta_runtime_state state;
  struct __Alta_error_handler_node* next;
} _Alta_error_handler_node;

typedef struct __Alta_error_container {
  _Alta_bool isNative;
  const char* typeName;
  void* value;
  _Alta_error_handler_node* handlerStack;
  size_t handlerStackSize;
} _Alta_error_container;

typedef struct __Alta_global_runtime_type {
  _Alta_bool inited;

  // object stack for "function stack" (i.e. local) variables
  _Alta_object_stack local;

  // object stack for "malloc-ed" (i.e. persistent) variables
  _Alta_object_stack persistent;

  // memory stack for other persistent values
  _Alta_generic_stack otherPersistent;

  _Alta_error_container lastError;

  // function table used to store virtual function addresses
  _Alta_function_table functionTable;
} _Alta_global_runtime_type;

extern _Alta_global_runtime_type _Alta_global_runtime;

_Alta_runtime_export void _Alta_init_global_runtime();
_Alta_runtime_export void _Alta_unwind_global_runtime();

_Alta_runtime_export void _Alta_object_stack_init(_Alta_object_stack* stack);
_Alta_runtime_export void _Alta_object_stack_deinit(_Alta_object_stack* stack);
_Alta_runtime_export void _Alta_object_stack_push(_Alta_object_stack* stack, _Alta_object* object);
_Alta_runtime_export void _Alta_object_stack_pop(_Alta_object_stack* stack);
_Alta_runtime_export _Alta_bool _Alta_object_stack_cherry_pick(_Alta_object_stack* stack, _Alta_object* object);
_Alta_runtime_export void _Alta_object_stack_unwind(_Alta_object_stack* stack, size_t count, _Alta_bool isPosition);
_Alta_runtime_export void _Alta_object_destroy(_Alta_object* object);

_Alta_runtime_export void _Alta_generic_stack_init();
_Alta_runtime_export void _Alta_generic_stack_deinit();
_Alta_runtime_export void _Alta_generic_stack_push(void* memory, _Alta_memory_destructor dtor);
_Alta_runtime_export void _Alta_generic_stack_pop();
_Alta_runtime_export void _Alta_generic_stack_cherry_pick(void* memory);
_Alta_runtime_export void _Alta_generic_stack_unwind(size_t count, _Alta_bool isPosition);

_Alta_runtime_export void _Alta_common_dtor(_Alta_basic_class* klass, _Alta_bool isPersistent);

_Alta_runtime_export _Alta_basic_class* _Alta_get_child(_Alta_basic_class* klass, size_t count, ...);
_Alta_runtime_export _Alta_basic_class* _Alta_get_real_version(_Alta_basic_class* klass);
_Alta_runtime_export _Alta_basic_class* _Alta_get_root_instance(_Alta_basic_class* klass);

_Alta_runtime_export void _Alta_reset_error(size_t index);
_Alta_runtime_export jmp_buf* _Alta_push_error_handler(const char* type);
_Alta_runtime_export size_t _Alta_pop_error_handler();

_Alta_runtime_export _Alta_runtime_state _Alta_save_state();
_Alta_runtime_export void _Alta_restore_state(const _Alta_runtime_state state);

_Alta_runtime_export void _Alta_uncaught_error(const char* typeName);
_Alta_runtime_export void _Alta_bad_cast(const char* from, const char* to);

_Alta_runtime_export void* _Alta_lookup_virtual_function(const char* className, const char* signature);
_Alta_runtime_export void _Alta_register_virtual_function(const char* classNameAndSignature, void* functionPointer);

#endif /* _ALTA_RUNTIME_COMMON_H */
