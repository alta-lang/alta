#define _ALTA_RUNTIME_EXPORTING
#include "common.h"

_Alta_global_runtime_type _Alta_global_runtime;

_Alta_runtime_export void _Alta_init_global_runtime() {
  _Alta_object_stack_init(&_Alta_global_runtime.local);
  _Alta_object_stack_init(&_Alta_global_runtime.persistent);

  _Alta_global_runtime.lastError.value = NULL;
  _Alta_global_runtime.lastError.handlerStack = NULL;
  _Alta_global_runtime.lastError.handlerStackSize = 0;
  _Alta_global_runtime.lastError.isNative = _Alta_bool_true;
  _Alta_global_runtime.lastError.typeName = "";

  _Alta_global_runtime.inited = _Alta_bool_true;
};

_Alta_runtime_export void _Alta_unwind_global_runtime() {
  _Alta_global_runtime.inited = _Alta_bool_false;

  _Alta_object_stack_deinit(&_Alta_global_runtime.local);
  _Alta_object_stack_deinit(&_Alta_global_runtime.persistent);
  _Alta_generic_stack_deinit();
};

_Alta_runtime_export void _Alta_object_stack_init(_Alta_object_stack* stack) {
  stack->nodeList = NULL;
  stack->nodeCount = 0;
};

_Alta_runtime_export void _Alta_object_stack_deinit(_Alta_object_stack* stack) {
  _Alta_object_stack_unwind(stack, SIZE_MAX, _Alta_bool_false); // effectively unwinds the entire sta
};

_Alta_runtime_export void _Alta_object_stack_push(_Alta_object_stack* stack, _Alta_basic_class* object) {
  _Alta_object_stack_node* node = malloc(sizeof(_Alta_object_stack_node));

  node->object = object;
  node->isUnion = _Alta_bool_false;
  node->prev = stack->nodeList;
  stack->nodeList = node;
  ++stack->nodeCount;
};

_Alta_runtime_export void _Alta_object_stack_push_union(_Alta_object_stack* stack, _Alta_basic_union* object) {
  _Alta_object_stack_node* node = malloc(sizeof(_Alta_object_stack_node));

  node->object = object;
  node->isUnion = _Alta_bool_true;
  node->prev = stack->nodeList;
  stack->nodeList = node;
  ++stack->nodeCount;
};

_Alta_runtime_export void _Alta_object_stack_pop(_Alta_object_stack* stack) {
  if (stack->nodeList == NULL) {
    return;
  }

  _Alta_object_stack_node* node = stack->nodeList;
  if (node->isUnion) {
    _Alta_basic_union* uni = node->object;
    uni->destructor(uni);
  } else {
    _Alta_basic_class* obj = node->object;
    if (!obj->_Alta_class_info_struct.destroyed && obj->_Alta_class_info_struct.destructor != NULL) {
      obj->_Alta_class_info_struct.destructor(obj, _Alta_bool_false);
    }
  }

  stack->nodeList = node->prev;
  --stack->nodeCount;
  free(node);
};

_Alta_runtime_export _Alta_bool _Alta_object_stack_cherry_pick(_Alta_object_stack* stack, _Alta_basic_class* object) {
  // this is the node that comes before the current node
  // in the linked list, NOT the node that corresponds to the
  // object created just before the object for the current node
  _Alta_object_stack_node* previousNode = NULL;
  _Alta_object_stack_node* node = stack->nodeList;
  while (node != NULL) {
    if (node->object == object) {
      _Alta_basic_class* obj = node->object;
      if (!obj->_Alta_class_info_struct.destroyed && obj->_Alta_class_info_struct.destructor != NULL) {
        obj->_Alta_class_info_struct.destructor(obj, _Alta_bool_false);
      }

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

_Alta_runtime_export void _Alta_generic_stack_init() {
  _Alta_generic_stack* stack = &_Alta_global_runtime.otherPersistent;

  stack->nodeList = NULL;
  stack->nodeCount = 0;
};

_Alta_runtime_export void _Alta_generic_stack_deinit() {
  _Alta_generic_stack* stack = &_Alta_global_runtime.otherPersistent;

  _Alta_generic_stack_unwind(SIZE_MAX, _Alta_bool_false); // effectively unwinds the entire stack
};

_Alta_runtime_export void _Alta_generic_stack_push(void* object, _Alta_memory_destructor dtor) {
  _Alta_generic_stack* stack = &_Alta_global_runtime.otherPersistent;

  _Alta_generic_stack_node* node = malloc(sizeof(_Alta_generic_stack_node));

  node->memory = object;
  node->dtor = dtor;
  node->prev = stack->nodeList;
  stack->nodeList = node;
  ++stack->nodeCount;
};

_Alta_runtime_export void _Alta_generic_stack_pop() {
  _Alta_generic_stack* stack = &_Alta_global_runtime.otherPersistent;

  if (stack->nodeList == NULL) {
    return;
  }

  _Alta_generic_stack_node* node = stack->nodeList;
  if (node->dtor != NULL) {
    node->dtor(node->memory);
  } else {
    free(node->memory);
  }

  stack->nodeList = node->prev;
  --stack->nodeCount;
  free(node);
};

_Alta_runtime_export void _Alta_generic_stack_cherry_pick(void* object) {
  _Alta_generic_stack* stack = &_Alta_global_runtime.otherPersistent;

  // this is the node that comes before the current node
  // in the linked list, NOT the node that corresponds to the
  // object created just before the object for the current node
  _Alta_generic_stack_node* previousNode = NULL;
  _Alta_generic_stack_node* node = stack->nodeList;
  while (node != NULL) {
    if (node->memory == object) {
      if (node->dtor != NULL) {
        node->dtor(node->memory);
      } else {
        free(node->memory);
      }

      if (previousNode != NULL) {
        previousNode->prev = node->prev;
      } else {
        stack->nodeList = node->prev;
      }

      --stack->nodeCount;
      free(node);

      break;
    }

    previousNode = node;
    node = node->prev;
  }
};

_Alta_runtime_export void _Alta_generic_stack_unwind(size_t count, _Alta_bool isPosition) {
  _Alta_generic_stack* stack = &_Alta_global_runtime.otherPersistent;

  if (isPosition) {
    while (stack->nodeCount > count) {
      _Alta_generic_stack_pop();
      if (stack->nodeList == NULL) break;
    }
  } else {
    size_t i;
    for (i = 0; i < count; i++) {
      _Alta_generic_stack_pop();
      if (stack->nodeList == NULL) break;
    }
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

_Alta_runtime_export void _Alta_reset_error(size_t index) {
  _Alta_error_container* err = &_Alta_global_runtime.lastError;
  if (
    err->typeName &&
    err->value &&
    strlen(err->typeName) > 0
  ) {
    if (!err->isNative) {
      _Alta_basic_class* klass = err->value;
      _Alta_basic_class* baseClass = (_Alta_basic_class*)(((char*)klass) - (klass->_Alta_class_info_struct.baseOffset));
      if (klass->_Alta_class_info_struct.destructor) {
        klass->_Alta_class_info_struct.destructor(baseClass, _Alta_bool_false);
      }
      free(baseClass);
    } else {
      free(err->value);
    }
    err->isNative = _Alta_bool_true;
    err->typeName = "";
    err->value = NULL;
  }

  while (err->handlerStackSize > index) {
    _Alta_pop_error_handler();
  }
};

_Alta_runtime_export jmp_buf* _Alta_push_error_handler(const char* type) {
  _Alta_error_container* err = &_Alta_global_runtime.lastError;

  _Alta_error_handler_node* node = malloc(sizeof(_Alta_error_handler_node));
  node->typeName = type;
  node->next = err->handlerStack;
  node->state = _Alta_save_state();
  err->handlerStack = node;

  return &node->jumpPoint;
};

_Alta_runtime_export size_t _Alta_pop_error_handler() {
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
    .persistentIndex = _Alta_global_runtime.persistent.nodeCount,
    .otherIndex = _Alta_global_runtime.otherPersistent.nodeCount,
  };
};

_Alta_runtime_export void _Alta_restore_state(const _Alta_runtime_state state) {
  _Alta_object_stack_unwind(&_Alta_global_runtime.local, state.localIndex, _Alta_bool_true);
  _Alta_object_stack_unwind(&_Alta_global_runtime.persistent, state.persistentIndex, _Alta_bool_true);
  _Alta_generic_stack_unwind(state.otherIndex, _Alta_bool_true);
};

_Alta_runtime_export void _Alta_uncaught_error(const char* typeName) {
#ifdef ALTA_FINAL_ERROR_HANDLER
  return ALTA_FINAL_ERROR_HANDLER(typeName);
#else
  printf("uncaught error thrown in Alta (with type \"%s\")\n", typeName);
  abort();
#endif
};
