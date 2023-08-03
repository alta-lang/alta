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
#include <iostream>

bool AltaLL::init() {
	LLVMInitializeAllTargetInfos();
	LLVMInitializeAllTargets();
	LLVMInitializeAllTargetMCs();
	LLVMInitializeAllAsmPrinters();
	return true;
};

void AltaLL::finit() {
	// nothing for now
};

void AltaLL::compile(std::shared_ptr<AltaCore::AST::RootNode> root, AltaCore::Filesystem::Path binaryOutputPath, bool debug, std::string targetName, std::string cpu, std::string cpuFeatures) {
	auto llcontext = llwrap(LLVMContextCreate());
	auto llmod = llwrap(LLVMModuleCreateWithNameInContext("alta_product", llcontext.get()));
	std::string defaultTriple = wrapMessage(LLVMGetDefaultTargetTriple());
	std::string hostCPU = wrapMessage(LLVMGetHostCPUName());
	std::string hostCPUFeatures = wrapMessage(LLVMGetHostCPUFeatures());
	LLVMTargetRef rawTarget = NULL;
	auto outputPathStr = binaryOutputPath.absolutify().toString();
	auto outputBitcodePathStr = outputPathStr + ".bc";
	auto outputDisassemblyPathStr = outputPathStr + ".ll";
	auto outputObjectPathStr = outputPathStr + ".o";
	char* errorMessage = NULL;

	LLVMContextSetOpaquePointers(llcontext.get(), true);

	std::stack<std::pair<std::shared_ptr<AltaCore::AST::RootNode>, size_t>> rootStack;
	std::unordered_set<std::string> processedRoots;

	if (targetName.empty()) {
		targetName = defaultTriple;
		if (cpu.empty()) {
			cpu = hostCPU;
		}
		if (cpuFeatures.empty()) {
			cpuFeatures = hostCPUFeatures;
		}
	}

	std::string targetTriple = wrapMessage(LLVMNormalizeTargetTriple(targetName.c_str()));

	if (LLVMGetTargetFromTriple(targetTriple.c_str(), &rawTarget, &errorMessage)) {
		std::string err = wrapMessage(errorMessage);
		errorMessage = nullptr;
		throw std::runtime_error("Failed to get target: " + err);
	}

	auto targetMachine = llwrap(LLVMCreateTargetMachine(rawTarget, targetTriple.c_str(), cpu.c_str(), cpuFeatures.c_str(), debug ? LLVMCodeGenLevelNone : LLVMCodeGenLevelDefault, LLVMRelocPIC, LLVMCodeModelDefault));
	auto targetData = llwrap(LLVMCreateTargetDataLayout(targetMachine.get()));

	LLVMSetModuleDataLayout(llmod.get(), targetData.get());
	LLVMSetTarget(llmod.get(), targetTriple.c_str());

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
	//LLVMDumpModule(llmod.get());
	LLVMPrintModuleToFile(llmod.get(), outputDisassemblyPathStr.c_str(), NULL);

	// make sure our module is good
	for (LLVMValueRef func = LLVMGetFirstFunction(llmod.get()); func != NULL; func = LLVMGetNextFunction(func)) {
		if (LLVMVerifyFunction(func, LLVMPrintMessageAction)) {
			size_t nameLength = 0;
			auto name = LLVMGetValueName2(func, &nameLength);
			std::string_view view(name, nameLength);
			std::cerr << "Invalid function: " << view << std::endl;
			abort();
		}
	}

	LLVMVerifyModule(llmod.get(), LLVMAbortProcessAction, NULL);

	// the bitcode is not strictly necessary; ignore it if we fail
	LLVMWriteBitcodeToFile(llmod.get(), outputBitcodePathStr.c_str());

	// on some platforms, the LLVM headers are wrong and specify the filename argument to LLVMTargetMachineEmitToFile as `char*` instead of `const char*`.
	// just in case, let's duplicate the string data.
	auto dupstr = strdup(outputObjectPathStr.c_str());

	if (LLVMTargetMachineEmitToFile(targetMachine.get(), llmod.get(), dupstr, LLVMObjectFile, NULL)) {
		throw std::runtime_error("Failed to emit object file");
	}

	free(dupstr);
};
