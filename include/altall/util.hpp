#ifndef ALTALL_UTIL_HPP
#define ALTALL_UTIL_HPP

#include <llvm-c/Core.h>
#include <llvm-c/TargetMachine.h>
#include <memory>
#include <string>

namespace AltaLL {

	#define ALTALL_LLWRAP_DEF_CUSTOM(_type, _dispose) \
		using LL ## _type = std::shared_ptr<std::remove_pointer_t<LLVM ## _type ## Ref>>; \
		static inline LL ## _type llwrap(LLVM ## _type ## Ref val) { \
			return LL ## _type(val, _dispose); \
		};

	#define ALTALL_LLWRAP_DEF(_type) ALTALL_LLWRAP_DEF_CUSTOM(_type, LLVMDispose ## _type)

	// using a template doesn't work properly (the compiler doesn't use the explicit instantiations in some cases)
	ALTALL_LLWRAP_DEF_CUSTOM(Context, LLVMContextDispose);
	ALTALL_LLWRAP_DEF(Module);
	ALTALL_LLWRAP_DEF(Builder);
	ALTALL_LLWRAP_DEF(TargetMachine);
	ALTALL_LLWRAP_DEF(TargetData);
	ALTALL_LLWRAP_DEF(PassManager);

	static inline std::string wrapMessage(char* msg) {
		std::string str(msg);
		LLVMDisposeMessage(msg);
		return str;
	};
};

#endif // ALTALL_UTIL_HPP