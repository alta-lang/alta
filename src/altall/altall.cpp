#include <altall/altall.hpp>
#include <llvm-c/Core.h>
#include <altall/util.hpp>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <altall/compiler.hpp>
#include <stack>
#include <string.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Analysis.h>
#include <altall/mangle.hpp>

bool AltaLL::init() {
	// nothing for now
	return true;
};

void AltaLL::finit() {
	// nothing for now
};

void AltaLL::compile(std::shared_ptr<AltaCore::AST::RootNode> root, AltaCore::Filesystem::Path binaryOutputPath) {
	auto llcontext = llwrap(LLVMContextCreate());
	auto llmod = llwrap(LLVMModuleCreateWithNameInContext("alta_product", llcontext.get()));
	std::string defaultTriple = wrapMessage(LLVMGetDefaultTargetTriple());
	std::string hostCPU = wrapMessage(LLVMGetHostCPUName());
	std::string hostCPUFeatures = wrapMessage(LLVMGetHostCPUFeatures());
	LLVMTargetRef rawTarget = NULL;
	auto outputPathStr = binaryOutputPath.absolutify().toString();
	auto outputBitcodePathStr = outputPathStr + ".bc";

	LLVMContextSetOpaquePointers(llcontext.get(), true);

	std::stack<std::pair<std::shared_ptr<AltaCore::AST::RootNode>, size_t>> rootStack;
	std::unordered_set<std::string> processedRoots;

	LLVMInitializeNativeTarget();
	LLVMInitializeNativeAsmPrinter();

	if (LLVMGetTargetFromTriple(defaultTriple.c_str(), &rawTarget, NULL)) {
		throw std::runtime_error("Failed to get target");
	}

	auto targetMachine = llwrap(LLVMCreateTargetMachine(rawTarget, defaultTriple.c_str(), hostCPU.c_str(), hostCPUFeatures.c_str(), LLVMCodeGenLevelDefault, LLVMRelocDefault, LLVMCodeModelDefault));
	auto targetData = llwrap(LLVMCreateTargetDataLayout(targetMachine.get()));

	LLVMSetModuleDataLayout(llmod.get(), targetData.get());
	LLVMSetTarget(llmod.get(), defaultTriple.c_str());

	Compiler altaCompiler(llcontext, llmod, targetMachine, targetData);

	// iterate through all the root nodes and compile them
	// (not recursively, to reduce stack usage and avoid stack overflows)
	rootStack.push(std::make_pair(root, 0));
	while (!rootStack.empty()) {
		auto& [curr, idx] = rootStack.top();

		if (idx == 0) {
			processedRoots.insert(curr->id);
		}

		bool shouldContinue = false;

		while (idx < curr->info->dependencyASTs.size()) {
			if (processedRoots.find(curr->info->dependencyASTs[idx]->id) == processedRoots.end()) {
				auto depIdx = idx++;
				rootStack.push(std::make_pair(curr->info->dependencyASTs[depIdx], 0));
				shouldContinue = true;
				break;
			}
			++idx;
		}

		if (shouldContinue) {
			continue;
		}

		altaCompiler.compile(curr);

		rootStack.pop();
	}

	altaCompiler.finalize();

	auto mappingMD = LLVMGetOrInsertNamedMetadata(llmod.get(), "alta.mapping", sizeof("alta.mapping") - 1);

	for (const auto& [mangled, fullName]: mangledToFullMapping) {
		auto mangledStr = LLVMMDStringInContext2(llcontext.get(), mangled.c_str(), mangled.size());
		auto fullNameStr = LLVMMDStringInContext2(llcontext.get(), fullName.c_str(), fullName.size());
		std::array<LLVMMetadataRef, 2> refs {
			mangledStr,
			fullNameStr,
		};
		auto node = LLVMMDNodeInContext2(llcontext.get(), refs.data(), refs.size());
		LLVMAddNamedMetadataOperand(llmod.get(), "alta.mapping", LLVMMetadataAsValue(llcontext.get(), node));
	}

	// for debugging
	LLVMDumpModule(llmod.get());

	// make sure our module is good
	LLVMVerifyModule(llmod.get(), LLVMAbortProcessAction, NULL);

	// the bitcode is not strictly necessary; ignore it if we fail
	LLVMWriteBitcodeToFile(llmod.get(), outputBitcodePathStr.c_str());

	// on some platforms, the LLVM headers are wrong and specify the filename argument to LLVMTargetMachineEmitToFile as `char*` instead of `const char*`.
	// just in case, let's duplicate the string data.
	auto dupstr = strdup(outputPathStr.c_str());

	if (LLVMTargetMachineEmitToFile(targetMachine.get(), llmod.get(), dupstr, LLVMObjectFile, NULL)) {
		throw std::runtime_error("Failed to emit object file");
	}

	free(dupstr);
};
