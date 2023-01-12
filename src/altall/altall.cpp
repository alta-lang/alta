#include <altall/altall.hpp>
#include <llvm-c/Core.h>
#include <altall/util.hpp>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

static ALTACORE_MAP<std::string, bool> varargTable;
static ALTACORE_MAP<std::string, std::shared_ptr<AltaCore::DET::Type>> invalidValueExpressionTable;

bool AltaLL::init() {
	// nothing for now
	return true;
};

void AltaLL::finit() {
	// nothing for now
};

#define AC_ATTRIBUTE_FUNC [=](std::shared_ptr<AltaCore::AST::Node> _target, std::shared_ptr<AltaCore::DH::Node> _info, std::vector<AltaCore::Attributes::AttributeArgument> args) -> void
#define AC_ATTRIBUTE_CAST(x) auto target = std::dynamic_pointer_cast<AltaCore::AST::x>(_target);\
  auto info = std::dynamic_pointer_cast<AltaCore::DH::x>(_info);\
  if (!target || !info) throw std::runtime_error("this isn't supposed to happen");
#define AC_ATTRIBUTE(x, ...) AltaCore::Attributes::registerAttribute({ __VA_ARGS__ }, { AltaCore::AST::NodeType::x }, AC_ATTRIBUTE_FUNC {\
  AC_ATTRIBUTE_CAST(x);
#define AC_GENERAL_ATTRIBUTE(...) AltaCore::Attributes::registerAttribute({ __VA_ARGS__ }, {}, AC_ATTRIBUTE_FUNC {
#define AC_END_ATTRIBUTE }, modulePath.toString())

void AltaLL::registerAttributes(AltaCore::Filesystem::Path modulePath) {
	AC_ATTRIBUTE(Parameter, "native", "vararg");
		varargTable[target->id] = true;
	AC_END_ATTRIBUTE;
	AC_ATTRIBUTE(SpecialFetchExpression, "invalid");
		if (args.size() != 1) {
			throw std::runtime_error("expected a single type argument to special fetch expression @invalid attribute");
		}
		if (!args.front().isScopeItem) {
			throw std::runtime_error("expected a type argument for special fetch expression @invalid attribute");
		}
		auto type = std::dynamic_pointer_cast<AltaCore::DET::Type>(args.front().item);
		if (!type) {
			throw std::runtime_error("expected a type argument for special fetch expression @invalid attribute");
		}
		invalidValueExpressionTable[info->id] = type;
		info->items.push_back(type);
  AC_END_ATTRIBUTE;
};

void AltaLL::compile(std::shared_ptr<AltaCore::AST::RootNode> root, AltaCore::Filesystem::Path binaryOutputPath) {
	auto llcontext = llwrap(LLVMContextCreate());
	auto llmod = llwrap(LLVMModuleCreateWithNameInContext("alta_product", llcontext.get()));
	std::string defaultTriple = wrapMessage(LLVMGetDefaultTargetTriple());
	std::string hostCPU = wrapMessage(LLVMGetHostCPUName());
	std::string hostCPUFeatures = wrapMessage(LLVMGetHostCPUFeatures());
	LLVMTargetRef rawTarget = NULL;
	auto outputPathStr = binaryOutputPath.absolutify().toString();

	// TODO: compile

	LLVMInitializeNativeTarget();
	LLVMInitializeNativeAsmPrinter();

	if (LLVMGetTargetFromTriple(defaultTriple.c_str(), &rawTarget, NULL)) {
		throw std::runtime_error("Failed to get target");
	}

	auto targetMachine = llwrap(LLVMCreateTargetMachine(rawTarget, defaultTriple.c_str(), hostCPU.c_str(), hostCPUFeatures.c_str(), LLVMCodeGenLevelDefault, LLVMRelocDefault, LLVMCodeModelDefault));
	auto targetData = llwrap(LLVMCreateTargetDataLayout(targetMachine.get()));

	LLVMSetModuleDataLayout(llmod.get(), targetData.get());
	LLVMSetTarget(llmod.get(), defaultTriple.c_str());

	if (LLVMTargetMachineEmitToFile(targetMachine.get(), llmod.get(), outputPathStr.c_str(), LLVMObjectFile, NULL)) {
		throw std::runtime_error("Failed to emit object file");
	}
};
