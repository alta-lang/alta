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

typedef enum __Alta_object_type {
  _Alta_object_type_class = 0,
  _Alta_object_type_union = 1,
  _Alta_object_type_optional = 2,
  _Alta_object_type_wrapper = 3,
  _Alta_object_type_function = 4,
} _Alta_object_type;

typedef struct __Alta_object {
  _Alta_object_type objectType;
} _Alta_object;

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
  _Alta_bool isCaptureClass;
} _Alta_class_info;

struct __Alta_basic_class {
  _Alta_object_type objectType;
  _Alta_class_info _Alta_class_info_struct;
};

typedef struct __Alta_basic_optional _Alta_basic_optional;
typedef void (*_Alta_optional_destructor)(_Alta_basic_optional*);

struct __Alta_basic_optional {
  _Alta_object_type objectType;
  _Alta_bool present;
  _Alta_optional_destructor destructor;
};

typedef struct __Alta_basic_union _Alta_basic_union;
typedef void (*_Alta_union_destructor)(_Alta_basic_union*);
struct __Alta_basic_union {
  _Alta_object_type objectType;
  char* typeName;
  _Alta_union_destructor destructor;
};

typedef struct __Alta_wrapper _Alta_wrapper;
typedef void (*_Alta_wrapper_destructor)(_Alta_wrapper*);
struct __Alta_wrapper {
  _Alta_object_type objectType;
  void* value;
  _Alta_wrapper_destructor destructor;
};

typedef struct __Alta_lambda_state {
  // pointer to heap-allocated counter of how many references to this state object are currently stored
  size_t* referenceCount;

  // number of objects in `copies`
  size_t copyCount;

  // number of pointers stored in the `references` array/block
  size_t referenceBlockCount;

  // array of pointers to heap-allocated copies of objects from the lambda's creation point
  _Alta_object** copies;

  // array/block of pointers to variables in the lambda's creation point
  void** references;
} _Alta_lambda_state;

typedef void (*_Alta_basic_plain_function)();
typedef void (*_Alta_basic_lambda_function)(_Alta_lambda_state);

typedef struct __Alta_basic_function {
  _Alta_object_type objectType;
  _Alta_lambda_state state;
  void* plain;
  void* lambda;
  void* proxy;
} _Alta_basic_function;

typedef struct __Alta_basic_capture_class {
  _Alta_object_type objectType;
  _Alta_class_info _Alta_class_info_struct;
  _Alta_lambda_state _Alta_capture_class_state;
} _Alta_basic_capture_class;

// <object-stack>
typedef struct __Alta_object_stack_node {
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

typedef struct __Alta_runtime_state {
  size_t localIndex;
} _Alta_runtime_state;

typedef struct __Alta_function_table {
  size_t count;
  const char** keys;
  void** entries;
} _Alta_function_table;

// <symbol-table>
typedef struct __Alta_symbol_info {
  // a developer-friendly name for the symbol
  const char* friendlyName;

  // a developer-friendly name for the symbol that includes
  // all the information from the Alta scope item it represents
  const char* fullAltaName;
} _Alta_symbol_info;

typedef struct __Alta_symbol_table {
  size_t count;
  const char** symbols;
  _Alta_symbol_info* infos;
} _Alta_symbol_table;
// </symbol-table>

typedef struct __Alta_floating_stack _Alta_floating_stack;

struct __Alta_floating_stack {
  _Alta_floating_stack* parent_stack;
  size_t size;
  size_t offset;
  char stack[];
};

typedef struct __Alta_generator_reload_context {
  _Alta_floating_stack* stack;
  size_t offset;
} _Alta_generator_reload_context;

typedef struct __Alta_basic_generator_state {
  _Alta_floating_stack* stack;
  void* input;
  size_t index;
  _Alta_bool done;
  void* parameters;
  _Alta_basic_class* self;
} _Alta_basic_generator_state;

typedef struct __Alta_basic_coroutine_state _Alta_basic_coroutine_state;
typedef void (*_Alta_basic_coroutine_runner)(_Alta_basic_coroutine_state* coroutineState);

struct __Alta_basic_coroutine_state {
  _Alta_object_type objectType;
  _Alta_class_info _Alta_class_info_struct;
  _Alta_basic_generator_state generator;
  void* value;
  _Alta_basic_coroutine_state* waitingFor;
  _Alta_basic_coroutine_runner next;
  uint64_t id;
};

#endif // _ALTA_RUNTIME_DEFINITIONS_H
