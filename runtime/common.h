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

#define __STDC_LIMIT_MACROS
#define __STDC_CONSTANT_MACROS

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>

/**
 * a little hack to grab the containing struct of another struct
 * e.g.
 * create a Foobar variable (a class with a Foo parent)
 * in C, the Foo parent struct is added as a member of the Foobar struct
 * when you have a pointer to a Foo struct, you can get a pointer to the
 * Foobar struct using this technique
 */
#define _ALTA_GET_PARENT_STRUCT_PTR(ITEM, STRUCT_TYPE, MEMBER_NAME) ((STRUCT_TYPE*)(((char*)ITEM) - offsetof(STRUCT_TYPE, MEMBER_NAME)))

typedef unsigned char _Alta_bool;
enum _Alta_bool_value {
  _Alta_bool_false = 0,
  _Alta_bool_true = 1
};

typedef struct __Alta_basic_class _Alta_basic_class;

typedef void (*_Alta_destructor)(_Alta_basic_class*, _Alta_bool);

typedef struct __Alta_class_info {
  const char* typeName;
  _Alta_bool destroyed;
  _Alta_bool persistent;
  _Alta_destructor destructor;
  _Alta_bool isBaseStruct;
  const char* parentTypeName;
  ptrdiff_t realOffset;
  ptrdiff_t nextOffset;
  size_t baseOffset;
} _Alta_class_info;

struct __Alta_basic_class {
  _Alta_class_info _Alta_class_info_struct;
};

// <object-stack>
typedef struct __Alta_object_stack_node {
  _Alta_basic_class* object;
  
  // named "prev" because it technically points to an older node,
  // but it actually points to the "next" item in the linked list
  struct __Alta_object_stack_node* prev;
} _Alta_object_stack_node;

typedef struct __Alta_object_stack {
  // our linked list for nodes, from newest/most recently created
  // as the first/root node, to oldest/first created at the bottom
  //
  // why use a linked list instead of an array? because we need to
  // be able to easily remove individual nodes from the list
  // (e.g. using `cherry_pick()`)
  _Alta_object_stack_node* nodeList;

  // the number of nodes currently in use
  size_t nodeCount;

  // our array of free linked list nodes
  _Alta_object_stack_node** freeList;

  // the size of the freeList
  size_t freeSize;

  // the number of free nodes in the freeList
  size_t freeCount;

  // the memory block allocated to linked list nodes
  //
  // why not just `malloc` each node individually?
  // because, often, `malloc` sucks at handling many
  // small allocations and it usually wastes more memory
  // keeping track of each individual allocation
  //
  // this way, we manage our own node memory efficiently
  // (since we also use a free list in order to reuse nodes)
  _Alta_object_stack_node* nodeBlock;

  // the size of the memory block allocated to linked list nodes
  size_t blockSize;
} _Alta_object_stack;
// </object-stack>

typedef struct __Alta_global_runtime_type {
  _Alta_bool inited;

  // object stack for "function stack" (i.e. local) variables
  _Alta_object_stack local;

  // object stack for "malloc-ed" (i.e. persistent) variables
  _Alta_object_stack persistent;
} _Alta_global_runtime_type;

extern _Alta_global_runtime_type _Alta_global_runtime;

_Alta_runtime_export void _Alta_init_global_runtime();
_Alta_runtime_export void _Alta_unwind_global_runtime();

_Alta_runtime_export void _Alta_object_stack_init(_Alta_object_stack* stack);
_Alta_runtime_export void _Alta_object_stack_deinit(_Alta_object_stack* stack);
_Alta_runtime_export void _Alta_object_stack_push(_Alta_object_stack* stack, _Alta_basic_class* object);
_Alta_runtime_export void _Alta_object_stack_pop(_Alta_object_stack* stack);
_Alta_runtime_export void _Alta_object_stack_cherry_pick(_Alta_object_stack* stack, _Alta_basic_class* object);
_Alta_runtime_export void _Alta_object_stack_unwind(_Alta_object_stack* stack, size_t count, _Alta_bool isPosition);

void _Alta_common_dtor(_Alta_basic_class* klass);

#endif /* _ALTA_RUNTIME_COMMON_H */
