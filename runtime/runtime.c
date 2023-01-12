#define _ALTA_RUNTIME_EXPORTING
#include "common.h"

#define __STDC_LIMIT_MACROS
#define __STDC_CONSTANT_MACROS

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

_Alta_global_runtime_type _Alta_global_runtime;

_Alta_runtime_export void _Alta_init_global_runtime() {
  _Alta_object_stack_init(&_Alta_global_runtime.local);

  _Alta_global_runtime.lastError.value = NULL;
  _Alta_global_runtime.lastError.handlerStack = NULL;
  _Alta_global_runtime.lastError.handlerStackSize = 0;
  _Alta_global_runtime.lastError.isNative = _Alta_bool_true;
  _Alta_global_runtime.lastError.typeName = "";

  _Alta_global_runtime.functionTable.keys = malloc(0);
  _Alta_global_runtime.functionTable.entries = malloc(0);
  _Alta_global_runtime.functionTable.count = 0;

  _Alta_global_runtime.symbolTable.symbols = malloc(0);
  _Alta_global_runtime.symbolTable.infos = malloc(0);
  _Alta_global_runtime.symbolTable.count = 0;

  _Alta_global_runtime.inited = _Alta_bool_true;

  _Alta_global_runtime.scheduler = _ALTA_SCHEDULER_CLASS_CONSTRUCTOR();
};

_Alta_runtime_export void _Alta_unwind_global_runtime() {
  if (!_Alta_global_runtime.inited) return;
  _Alta_global_runtime.inited = _Alta_bool_false;

  _ALTA_SCHEDULER_CLASS_DESTRUCTOR((_Alta_basic_class*)&_Alta_global_runtime.scheduler, _Alta_bool_false);

  _Alta_object_stack_deinit(&_Alta_global_runtime.local);

  free(_Alta_global_runtime.functionTable.keys);
  free(_Alta_global_runtime.functionTable.entries);
  _Alta_global_runtime.functionTable.count = 0;

  free(_Alta_global_runtime.symbolTable.symbols);
  free(_Alta_global_runtime.symbolTable.infos);
  _Alta_global_runtime.symbolTable.count = 0;
};

_Alta_runtime_export void _Alta_object_stack_init(_Alta_object_stack* stack) {
  stack->nodeList = NULL;
  stack->nodeCount = 0;
};

_Alta_runtime_export void _Alta_object_stack_deinit(_Alta_object_stack* stack) {
  if (!_Alta_global_runtime.inited) return;
  _Alta_object_stack_unwind(stack, SIZE_MAX, _Alta_bool_false); // effectively unwinds the entire stack
};

_Alta_runtime_export void _Alta_object_stack_push(_Alta_object_stack* stack, _Alta_object* object) {
  if (!_Alta_global_runtime.inited) return;
  _Alta_object_stack_node* node = malloc(sizeof(_Alta_object_stack_node));

  node->object = object;
  node->prev = stack->nodeList;
  stack->nodeList = node;
  ++stack->nodeCount;
};

_Alta_runtime_export void _Alta_object_stack_pop(_Alta_object_stack* stack) {
  if (!_Alta_global_runtime.inited) return;
  if (stack->nodeList == NULL) {
    return;
  }

  _Alta_object_stack_node* node = stack->nodeList;
  _Alta_object_destroy(node->object);

  stack->nodeList = node->prev;
  --stack->nodeCount;
  free(node);
};

_Alta_runtime_export _Alta_bool _Alta_object_stack_cherry_pick(_Alta_object_stack* stack, _Alta_object* object) {
  if (!_Alta_global_runtime.inited) return _Alta_bool_false;
  // this is the node that comes before the current node
  // in the linked list, NOT the node that corresponds to the
  // object created just before the object for the current node
  _Alta_object_stack_node* previousNode = NULL;
  _Alta_object_stack_node* node = stack->nodeList;
  while (node != NULL) {
    if (node->object == object) {
      _Alta_object* obj = node->object;

      _Alta_object_destroy(obj);

      if (previousNode != NULL) {
        previousNode->prev = node->prev;
      } else {
        stack->nodeList = node->prev;
      }

      --stack->nodeCount;
      free(node);

      return _Alta_bool_true;
    }

    previousNode = node;
    node = node->prev;
  }

  return _Alta_bool_false;
};

_Alta_runtime_export void _Alta_object_stack_unwind(_Alta_object_stack* stack, size_t count, _Alta_bool isPosition) {
  if (!_Alta_global_runtime.inited) return;
  if (isPosition) {
    while (stack->nodeCount > count) {
      _Alta_object_stack_pop(stack);
      if (stack->nodeList == NULL) break;
    }
  } else {
    size_t i;
    for (i = 0; i < count; i++) {
      _Alta_object_stack_pop(stack);
      if (stack->nodeList == NULL) break;
    }
  }
};

_Alta_runtime_export void _Alta_release_state(_Alta_lambda_state* state) {
  if (state->referenceCount) {
    --*state->referenceCount;
    if (*state->referenceCount == 0) {
      if (state->copies) {
        size_t i;
        for (i = 0; i < state->copyCount; i++) {
          _Alta_object_destroy(state->copies[i]);
          free(state->copies[i]);
        }
        free(state->copies);
      }
      if (state->references) {
        free(state->references);
      }
      free(state->referenceCount);
    }
    state->copies = NULL;
    state->references = NULL;
    state->referenceCount = NULL;
  }
};

_Alta_runtime_export void _Alta_object_destroy(_Alta_object* object) {
  if (object->objectType == _Alta_object_type_class) {
    _Alta_basic_class* klass = (_Alta_basic_class*)object;
    if (!klass->_Alta_class_info_struct.destroyed) {
      klass = (_Alta_basic_class*)(((char*)klass) - (klass->_Alta_class_info_struct.baseOffset));
      if (!klass->_Alta_class_info_struct.destroyed && klass->_Alta_class_info_struct.destructor != NULL) {
        klass->_Alta_class_info_struct.destructor(klass, _Alta_bool_false);
      }
    }
  } else if (object->objectType == _Alta_object_type_union) {
    _Alta_basic_union* uni = (_Alta_basic_union*)object;
    uni->destructor(uni);
  } else if (object->objectType == _Alta_object_type_optional) {
    _Alta_basic_optional* opt = (_Alta_basic_optional*)object;
    opt->destructor(opt);
  } else if (object->objectType == _Alta_object_type_wrapper) {
    _Alta_wrapper* wrapper = (_Alta_wrapper*)object;
    if (wrapper->destructor) {
      wrapper->destructor(wrapper);
    }
    free(wrapper->value);
  } else if (object->objectType == _Alta_object_type_function) {
    _Alta_basic_function* func = (_Alta_basic_function*)object;
    if (func->state.referenceCount) {
      _Alta_release_state(&func->state);
      func->plain = NULL;
      func->lambda = NULL;
      func->proxy = NULL;
    }
  } else {
    abort();
  }
};

_Alta_runtime_export void _Alta_common_dtor(_Alta_basic_class* klass, _Alta_bool isPersistent) {
  _Alta_basic_class* base = (_Alta_basic_class*)((char*)klass - klass->_Alta_class_info_struct.baseOffset);
  if (base->_Alta_class_info_struct.destructor != NULL) {
    base->_Alta_class_info_struct.destructor(base, isPersistent);
  } else if (isPersistent || base->_Alta_class_info_struct.persistent) {
    free(base);
  }
};

_Alta_runtime_export _Alta_basic_class* _Alta_get_child(_Alta_basic_class* klass, size_t count, ...) {
  va_list parentNames;
  va_start(parentNames, count);

  size_t i;
  for (i = 0; i < count; i++) {
    char* parentName = va_arg(parentNames, char*);
    while (klass != NULL) {
      if (strcmp(klass->_Alta_class_info_struct.parentTypeName, parentName) == 0) {
        break;
      } else if (klass->_Alta_class_info_struct.nextOffset < PTRDIFF_MAX) {
        klass = (_Alta_basic_class*)((char*)klass + klass->_Alta_class_info_struct.nextOffset);
      } else {
        klass = NULL;
      }
    }
    if (klass == NULL) break;
    klass = (_Alta_basic_class*)((char*)klass - klass->_Alta_class_info_struct.parentOffset);
  }

  va_end(parentNames);

  return klass;
};

_Alta_runtime_export _Alta_basic_class* _Alta_get_real_version(_Alta_basic_class* klass) {
  // condensed as a single ternary so that it's easier for compilers to inline
  // besides, this is perfectly readable
  return (klass->_Alta_class_info_struct.realOffset < PTRDIFF_MAX) ? (_Alta_basic_class*)((char*)klass - klass->_Alta_class_info_struct.realOffset) : klass;
};

_Alta_runtime_export _Alta_basic_class* _Alta_get_root_instance(_Alta_basic_class* klass) {
  return (_Alta_basic_class*)((char*)klass - klass->_Alta_class_info_struct.baseOffset);
};

_Alta_runtime_export void _Alta_reset_error(size_t index) {
  if (!_Alta_global_runtime.inited) return;
  _Alta_error_container* err = &_Alta_global_runtime.lastError;
  if (
    err->typeName &&
    err->value &&
    strlen(err->typeName) > 0
  ) {
    if (!err->isNative) {
      _Alta_object_destroy((_Alta_object*)err->value);
    }
    free(_Alta_get_root_instance(err->value));
    err->isNative = _Alta_bool_true;
    err->typeName = "";
    err->value = NULL;
  }

  while (err->handlerStackSize > index) {
    _Alta_pop_error_handler();
  }
};

_Alta_runtime_export jmp_buf* _Alta_push_error_handler(const char* type) {
  if (!_Alta_global_runtime.inited) return NULL;
  _Alta_error_container* err = &_Alta_global_runtime.lastError;

  _Alta_error_handler_node* node = malloc(sizeof(_Alta_error_handler_node));
  node->typeName = type;
  node->next = err->handlerStack;
  node->state = _Alta_save_state();
  err->handlerStack = node;
  ++err->handlerStackSize;

  return &node->jumpPoint;
};

_Alta_runtime_export size_t _Alta_pop_error_handler() {
  if (!_Alta_global_runtime.inited) return 0;
  _Alta_error_container* err = &_Alta_global_runtime.lastError;

  if (err->handlerStack) {
    _Alta_error_handler_node* node = err->handlerStack;
    err->handlerStack = node->next;
    free(node);
    --err->handlerStackSize;
  }
  
  return err->handlerStackSize;
};

_Alta_runtime_export _Alta_runtime_state _Alta_save_state() {
  return (_Alta_runtime_state) {
    .localIndex = _Alta_global_runtime.local.nodeCount,
  };
};

_Alta_runtime_export void _Alta_restore_state(const _Alta_runtime_state state) {
  if (!_Alta_global_runtime.inited) return;
  _Alta_object_stack_unwind(&_Alta_global_runtime.local, state.localIndex, _Alta_bool_true);
};

_Alta_runtime_export void _Alta_uncaught_error(const char* typeName) {
#ifdef ALTA_FINAL_ERROR_HANDLER
  return ALTA_FINAL_ERROR_HANDLER(typeName);
#else
  const char* friendly = _Alta_symbol_to_full_Alta_name(typeName);
  printf("uncaught error thrown in Alta (with type \"%s\")\n", friendly ? friendly : typeName);
  abort();
#endif
};

_Alta_runtime_export void _Alta_bad_cast(const char* from, const char* to) {
#if defined(ALTA_BAD_CAST_HANDLER)
  return ALTA_BAD_CAST_HANDLER(from, to);
#elif defined(ALTA_RUNTIME_FREESTANDING)
  return _Alta_uncaught_error("@BadCast@");
#else
  printf("bad cast from %s to %s\n", from, to);
  abort();
#endif
};

_Alta_runtime_export void* _Alta_lookup_virtual_function(const char* className, const char* signature) {
  _Alta_function_table* table = &_Alta_global_runtime.functionTable;

  size_t firstLen = strlen(className);
  size_t secondLen = strlen(signature);
  char* key = malloc((firstLen + secondLen + 2) * sizeof (char));

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)
#endif // _MSC_VER

  strncpy(key, className, firstLen);
  strncpy(key + firstLen + 1, signature, secondLen);

#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

  key[firstLen] = '$';

  key[firstLen + secondLen + 1] = '\0';

  for (size_t i = 0; i < table->count; ++i) {
    if (strcmp(table->keys[i], key) == 0) {
      free(key);
      return table->entries[i];
    }
  }

  free(key);
  return NULL;
};

_Alta_runtime_export void _Alta_register_virtual_function(const char* classNameAndSignature, void* functionPointer) {
  _Alta_function_table* table = &_Alta_global_runtime.functionTable;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4090)
#endif // _MSC_VER

  table->keys = realloc(table->keys, (++table->count) * sizeof(const char*));

#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

  table->entries = realloc(table->entries, table->count * sizeof(void*));

  table->keys[table->count - 1] = classNameAndSignature;
  table->entries[table->count - 1] = functionPointer;
};

_Alta_runtime_export void _Alta_invalid_return_value() {
  printf("ERROR: invalid/absent return value\n");
  abort();
};

_Alta_runtime_export void _Alta_register_symbol(const char* symbol, _Alta_symbol_info info) {
  _Alta_symbol_table* table = &_Alta_global_runtime.symbolTable;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4090)
#endif // _MSC_VER

  table->symbols = realloc(table->symbols, (++table->count) * sizeof(const char*));

#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

  table->infos = realloc(table->infos, table->count * sizeof(_Alta_symbol_info));

  table->symbols[table->count - 1] = symbol;
  table->infos[table->count - 1] = info;
};

_Alta_runtime_export _Alta_symbol_info* _Alta_find_info_for_symbol(const char* symbol) {
  _Alta_symbol_table* table = &_Alta_global_runtime.symbolTable;

  for (size_t i = 0; i < table->count; ++i) {
    if (strcmp(table->symbols[i], symbol) == 0) {
      return &table->infos[i];
    }
  }

  return NULL;
};

_Alta_runtime_export const char* _Alta_symbol_to_full_Alta_name(const char* symbol) {
  _Alta_symbol_table* table = &_Alta_global_runtime.symbolTable;

  _Alta_symbol_info* info = _Alta_find_info_for_symbol(symbol);

  if (!info) {
    return NULL;
  }

  return info->fullAltaName;
};

_Alta_runtime_export void _Alta_release_capture_class_state_cache(_Alta_wrapper* wrapper) {
  _Alta_lambda_state* state = (_Alta_lambda_state*)wrapper->value;
  --*state->referenceCount;
  if (*state->referenceCount == 0) {
    *state->referenceCount = 1;
    _Alta_release_state(state);
  }
};

_Alta_runtime_export void* _Alta_generator_push(_Alta_basic_generator_state* generator, size_t size) {
  // we store the stack backwards, just like the native platform does
  return (void*)((char*)generator->stack->stack + generator->stack->size - (generator->stack->offset += size));
};

_Alta_runtime_export void _Alta_no_op_optional_destructor(_Alta_basic_optional* optional) {};
_Alta_runtime_export void _Alta_no_op_union_destructor(_Alta_basic_union* uni) {};
_Alta_runtime_export void _Alta_no_op_class_destructor(_Alta_basic_class* class, _Alta_bool persistent) {};

_Alta_runtime_export void _Alta_failed_assertion(const char* expression, const char* file, size_t line, size_t column) {
#if defined(ALTA_FAILED_ASSERTION_HANDLER)
  return ALTA_FAILED_ASSERTION_HANDLER(expression, file, line, column);
#elif defined(ALTA_RUNTIME_FREESTANDING)
  return _Alta_uncaught_error("@FailedAssertion@");
#else
  printf("failed assertion at %s:%zu:%zu: %s\n", (file[0] == '\0') ? "<unknown>" : file, line, column, (expression[0] == '\0') ? "<unknown>" : expression);
  abort();
#endif
};

_Alta_runtime_export void _Alta_generator_create_stack(_Alta_basic_generator_state* generator, size_t size) {
  _Alta_floating_stack* parent = generator->stack;
  generator->stack = malloc(sizeof(_Alta_floating_stack) + size);

  // we don't zero out the stack, since the native platform doesn't do that either (so it's unnecessary work)
  memset(generator->stack, 0, sizeof(_Alta_floating_stack));

  generator->stack->parent_stack = parent;
  generator->stack->size = size;
};

_Alta_runtime_export void _Alta_generator_release_stack(_Alta_basic_generator_state* generator) {
  _Alta_floating_stack* prev = generator->stack;
  generator->stack = generator->stack->parent_stack;
  free(prev);
};

_Alta_runtime_export _Alta_generator_reload_context _Alta_generator_reload(_Alta_basic_generator_state* generator) {
  return (_Alta_generator_reload_context) {
    .stack = generator->stack,
    .offset = generator->stack->offset,
  };
};

_Alta_runtime_export void* _Alta_generator_reload_next(_Alta_generator_reload_context* context, size_t size) {
  if (!context) {
    printf("_Alta_generator_reload_next: can't reload without a context\n");
    abort();
  } else if (!context->stack) {
    printf("_Alta_generator_reload_next: was given a finished context\n");
    abort();
  }

  void* ptr = (void*)((char*)context->stack->stack + context->stack->size - context->offset);
  context->offset -= size;
  return ptr;
};

_Alta_runtime_export void _Alta_generator_reload_next_scope(_Alta_generator_reload_context* context) {
  if (!context) {
    printf("_Alta_generator_reload_next_scope: can't reload without a context\n");
    abort();
  } else if (!context->stack) {
    printf("_Alta_generator_reload_next_scope: was given a finished context\n");
    abort();
  }

  context->stack = context->stack->parent_stack;
  context->offset = context->stack->offset;
};
