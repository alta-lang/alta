#define _ALTA_RUNTIME_EXPORTING
#include "common.h"

_Alta_global_runtime_type _Alta_global_runtime;

_Alta_runtime_export void _Alta_init_global_runtime() {
  _Alta_object_stack_init(&_Alta_global_runtime.local);
  _Alta_object_stack_init(&_Alta_global_runtime.persistent);

  _Alta_global_runtime.inited = _Alta_bool_true;
};

_Alta_runtime_export void _Alta_unwind_global_runtime() {
  _Alta_global_runtime.inited = _Alta_bool_false;

  _Alta_object_stack_deinit(&_Alta_global_runtime.local);
  _Alta_object_stack_deinit(&_Alta_global_runtime.persistent);
  _Alta_generic_stack_deinit();
};

// <private-object-stack-methods>
void _Alta_object_stack_free(_Alta_object_stack* stack, _Alta_object_stack_node* node) {
  if (stack->freeCount >= stack->freeSize) {
    size_t newSize = stack->freeSize + 10;
    _Alta_object_stack_node** tmp = realloc(stack->freeList, newSize * sizeof(_Alta_object_stack_node*));
    if (tmp == NULL) {
      newSize = stack->freeSize + 1;
      tmp = realloc(stack->freeList, newSize * sizeof(_Alta_object_stack_node*));
      if (tmp == NULL) {
        // what are we supposed to do here?
        // aborting is probably the best idea
        // since by now there is something seriously wrong
        abort();
        // TODO: figure out if there's an alternative to aborting.
        //       of course, there's always the option to waste memory
        //       by not reusing the node, but that's a bad idea
      }
    }
    stack->freeList = tmp;
    stack->freeSize = newSize;
  }

  node->object = NULL;
  node->prev = NULL;

  stack->freeList[stack->freeCount] = node;
  stack->freeCount++;

  stack->nodeCount--;
};

void _Alta_object_stack_expand(_Alta_object_stack* stack) {
  size_t newSize = stack->blockSize + 10;
  _Alta_object_stack_node* tmp = realloc(stack->nodeBlock, newSize * sizeof(_Alta_object_stack_node));
  if (tmp == NULL) {
    newSize = stack->blockSize + 1;
    _Alta_object_stack_node* tmp = realloc(stack->nodeBlock, newSize * sizeof(_Alta_object_stack_node));
    if (tmp == NULL) {
      // same reasoning as the `abort()` call in `_Alta_object_stack_free()`
      abort();
    }
  }

  size_t oldSize = stack->blockSize;
  stack->blockSize = newSize;

  size_t i;
  for (i = oldSize; i < stack->blockSize; i++) {
    _Alta_object_stack_node* node = &(stack->nodeBlock[i]);
    _Alta_object_stack_free(stack, node);
  }
};
// </private-object-stack-methods>

_Alta_runtime_export void _Alta_object_stack_init(_Alta_object_stack* stack) {
  stack->nodeList = NULL;
  stack->nodeCount = 10;
  stack->blockSize = 10;
  stack->nodeBlock = malloc(stack->blockSize * sizeof(_Alta_object_stack_node));
  stack->freeList = malloc(stack->blockSize * sizeof(_Alta_object_stack_node*));
  stack->freeSize = stack->blockSize;
  stack->freeCount = 0;

  size_t i;
  for (i = 0; i < stack->blockSize; i++) {
    _Alta_object_stack_free(stack, &(stack->nodeBlock[i]));
  }
};

_Alta_runtime_export void _Alta_object_stack_deinit(_Alta_object_stack* stack) {
  _Alta_object_stack_unwind(stack, SIZE_MAX, _Alta_bool_false); // effectively unwinds the entire stack
  free(stack->freeList);
  free(stack->nodeBlock);
};

_Alta_runtime_export void _Alta_object_stack_push(_Alta_object_stack* stack, _Alta_basic_class* object) {
  if (stack->freeCount == 0) {
    _Alta_object_stack_expand(stack);
  }

  _Alta_object_stack_node* node = stack->freeList[stack->freeCount - 1];
  stack->freeCount--;

  node->object = object;
  node->prev = stack->nodeList;
  stack->nodeList = node;
  stack->nodeCount++;
};

_Alta_runtime_export void _Alta_object_stack_pop(_Alta_object_stack* stack) {
  if (stack->nodeList == NULL) {
    return;
  }

  _Alta_object_stack_node* node = stack->nodeList;
  if (!node->object->_Alta_class_info_struct.destroyed && node->object->_Alta_class_info_struct.destructor != NULL) {
    node->object->_Alta_class_info_struct.destructor(node->object, _Alta_bool_false);
  }

  stack->nodeList = node->prev;
  _Alta_object_stack_free(stack, node);
};

_Alta_runtime_export _Alta_bool _Alta_object_stack_cherry_pick(_Alta_object_stack* stack, _Alta_basic_class* object) {
  // this is the node that comes before the current node
  // in the linked list, NOT the node that corresponds to the
  // object created just before the object for the current node
  _Alta_object_stack_node* previousNode = NULL;
  _Alta_object_stack_node* node = stack->nodeList;
  while (node != NULL) {
    if (node->object == object) {
      if (!node->object->_Alta_class_info_struct.destroyed && node->object->_Alta_class_info_struct.destructor != NULL) {
        node->object->_Alta_class_info_struct.destructor(node->object, _Alta_bool_false);
      }

      if (previousNode != NULL) {
        previousNode->prev = node->prev;
      } else {
        stack->nodeList = node->prev;
      }

      _Alta_object_stack_free(stack, node);

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

// <private-generic-stack-methods>
void _Alta_generic_stack_free(_Alta_generic_stack_node* node) {
  _Alta_generic_stack* stack = &_Alta_global_runtime.otherPersistent;

  if (stack->freeCount >= stack->freeSize) {
    size_t newSize = stack->freeSize + 10;
    _Alta_generic_stack_node** tmp = realloc(stack->freeList, newSize * sizeof(_Alta_generic_stack_node*));
    if (tmp == NULL) {
      newSize = stack->freeSize + 1;
      tmp = realloc(stack->freeList, newSize * sizeof(_Alta_generic_stack_node*));
      if (tmp == NULL) {
        // what are we supposed to do here?
        // aborting is probably the best idea
        // since by now there is something seriously wrong
        abort();
        // TODO: figure out if there's an alternative to aborting.
        //       of course, there's always the option to waste memory
        //       by not reusing the node, but that's a bad idea
      }
    }
    stack->freeList = tmp;
    stack->freeSize = newSize;
  }

  node->memory = NULL;
  node->dtor = NULL;
  node->prev = NULL;

  stack->freeList[stack->freeCount] = node;
  stack->freeCount++;

  stack->nodeCount--;
};

void _Alta_generic_stack_expand() {
  _Alta_generic_stack* stack = &_Alta_global_runtime.otherPersistent;

  size_t newSize = stack->blockSize + 10;
  _Alta_generic_stack_node* tmp = realloc(stack->nodeBlock, newSize * sizeof(_Alta_generic_stack_node));
  if (tmp == NULL) {
    newSize = stack->blockSize + 1;
    _Alta_generic_stack_node* tmp = realloc(stack->nodeBlock, newSize * sizeof(_Alta_generic_stack_node));
    if (tmp == NULL) {
      // same reasoning as the `abort()` call in `_Alta_generic_stack_free()`
      abort();
    }
  }

  size_t oldSize = stack->blockSize;
  stack->blockSize = newSize;

  size_t i;
  for (i = oldSize; i < stack->blockSize; i++) {
    _Alta_generic_stack_node* node = &(stack->nodeBlock[i]);
    _Alta_generic_stack_free(node);
  }
};
// </private-generic-stack-methods>

_Alta_runtime_export void _Alta_generic_stack_init() {
  _Alta_generic_stack* stack = &_Alta_global_runtime.otherPersistent;

  stack->nodeList = NULL;
  stack->nodeCount = 10;
  stack->blockSize = 10;
  stack->nodeBlock = malloc(stack->blockSize * sizeof(_Alta_generic_stack_node));
  stack->freeList = malloc(stack->blockSize * sizeof(_Alta_generic_stack_node*));
  stack->freeSize = stack->blockSize;
  stack->freeCount = 0;

  size_t i;
  for (i = 0; i < stack->blockSize; i++) {
    _Alta_generic_stack_free(&(stack->nodeBlock[i]));
  }
};

_Alta_runtime_export void _Alta_generic_stack_deinit() {
  _Alta_generic_stack* stack = &_Alta_global_runtime.otherPersistent;

  _Alta_generic_stack_unwind(SIZE_MAX, _Alta_bool_false); // effectively unwinds the entire stack
  free(stack->freeList);
  free(stack->nodeBlock);
};

_Alta_runtime_export void _Alta_generic_stack_push(void* object, _Alta_memory_destructor dtor) {
  _Alta_generic_stack* stack = &_Alta_global_runtime.otherPersistent;

  if (stack->freeCount == 0) {
    _Alta_generic_stack_expand();
  }

  _Alta_generic_stack_node* node = stack->freeList[stack->freeCount - 1];
  stack->freeCount--;

  node->memory = object;
  node->dtor = dtor;
  node->prev = stack->nodeList;
  stack->nodeList = node;
  stack->nodeCount++;
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
  _Alta_generic_stack_free(node);
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

      _Alta_generic_stack_free(node);

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

_Alta_runtime_export void _Alta_reset_error() {
  _Alta_error_container* err = &_Alta_global_runtime.lastError;
  if (
    err->typeName &&
    err->value &&
    strlen(err->typeName) > 0
  ) {
    if (!err->isNative) {
      _Alta_basic_class* klass = err->value;
      if (klass->_Alta_class_info_struct.destructor) {
        klass->_Alta_class_info_struct.destructor(klass, _Alta_bool_false);
      }
    }
    free(err->value);
    err->isNative = _Alta_bool_true;
    err->typeName = "";
    err->value = NULL;
  }
};
