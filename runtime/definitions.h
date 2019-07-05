#ifndef _ALTA_RUNTIME_DEFINITIONS_H
#define _ALTA_RUNTIME_DEFINITIONS_H

#define __STDC_LIMIT_MACROS
#define __STDC_CONSTANT_MACROS

#include <stdint.h>
#include <stddef.h>

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
typedef void (*_Alta_memory_destructor)(void*);

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
  size_t parentOffset;
} _Alta_class_info;

struct __Alta_basic_class {
  _Alta_class_info _Alta_class_info_struct;
};

typedef struct __Alta_basic_union _Alta_basic_union;
typedef void (*_Alta_union_destructor)(_Alta_basic_union*);
struct __Alta_basic_union {
  char* typeName;
  _Alta_union_destructor destructor;
};

// <object-stack>
typedef struct __Alta_object_stack_node {
  _Alta_bool isUnion;
  void* object;
  
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
} _Alta_object_stack;
// </object-stack>

// <generic-stack>
typedef struct __Alta_generic_stack_node {
  void* memory;
  _Alta_memory_destructor dtor;
  struct __Alta_generic_stack_node* prev;
} _Alta_generic_stack_node;

typedef struct __Alta_generic_stack {
  _Alta_generic_stack_node* nodeList;
  size_t nodeCount;
} _Alta_generic_stack;
// </generic-stack>

typedef struct __Alta_runtime_state {
  size_t localIndex;
  size_t persistentIndex;
  size_t otherIndex;
} _Alta_runtime_state;

#endif // _ALTA_RUNTIME_DEFINITIONS_H
