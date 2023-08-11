#include "altacore/ast-shared.hpp"
#include "altacore/det-shared.hpp"
#include "altacore/det/function.hpp"
#include "altacore/det/type.hpp"
#include "altacore/optional.hpp"
#include "altacore/shared.hpp"
#include "altacore/util.hpp"
#include <altall/compiler.hpp>
#include <altall/mangle.hpp>
#include <altall/altall.hpp>
#include <array>
#include <bit>
#include <cstdint>
#include <llvm-c/Core.h>
#include <llvm-c/DebugInfo.h>
#include <llvm-c/Target.h>
#include <llvm-c/Types.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Metadata.h>
#include <memory>
#include <regex>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/IR/DIBuilder.h>

// DEBUGGING/DEVELOPMENT
#include <iostream>

#define ALTA_COMPILER_NAME "altac_llvm"

using namespace std::literals;

template<typename T> T* unwrapDI(LLVMMetadataRef node) {
	return (T*)llvm::unwrap<llvm::MDNode>(node);
};

static ALTACORE_MAP<std::string, bool> varargTable;
ALTACORE_MAP<std::string, std::shared_ptr<AltaCore::DET::Type>> AltaLL::Compiler::invalidValueExpressionTable;

#define AC_ATTRIBUTE_FUNC [=](std::shared_ptr<AltaCore::AST::Node> _target, std::shared_ptr<AltaCore::DH::Node> _info, std::vector<AltaCore::Attributes::AttributeArgument> args) -> void
#define AC_ATTRIBUTE_CAST(x) auto target = std::dynamic_pointer_cast<AltaCore::AST::x>(_target);\
	auto info = std::dynamic_pointer_cast<AltaCore::DH::x>(_info);\
	if (!target || !info) throw std::runtime_error("this isn't supposed to happen");
#define AC_ATTRIBUTE(x, ...) AltaCore::Attributes::registerAttribute({ __VA_ARGS__ }, { AltaCore::AST::NodeType::x }, AC_ATTRIBUTE_FUNC {\
	AC_ATTRIBUTE_CAST(x);
#define AC_GENERAL_ATTRIBUTE(...) AltaCore::Attributes::registerAttribute({ __VA_ARGS__ }, {}, AC_ATTRIBUTE_FUNC {
#define AC_END_ATTRIBUTE }, modulePath.toString())

static void dieOnError(AltaCore::Errors::ValidationError&) {
	std::terminate();
};

AltaLL::ValidationErrorHandler AltaLL::validationErrorHandler = dieOnError;

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
		Compiler::invalidValueExpressionTable[info->id] = type;
		info->items.push_back(type);
	AC_END_ATTRIBUTE;
};

AltaLL::Compiler::Coroutine<void> AltaLL::Compiler::ScopeStack::endBranch(LLVMBasicBlockRef mergeBlock, std::vector<LLVMBasicBlockRef> mergingBlocks, bool allowStateChange) {
	auto size = sizesDuringBranching.top();
	sizesDuringBranching.pop();

	auto suspendableState = suspendableStateDuringBranching.top();
	suspendableStateDuringBranching.pop();

	if (!allowStateChange) {
		auto currentSuspendableState = compiler._suspendableStateIndex.empty() ? ALTACORE_NULLOPT : ALTACORE_MAKE_OPTIONAL(compiler._suspendableStateIndex.top());

		if (suspendableState != currentSuspendableState) {
			throw std::runtime_error("Assumption violated: state must not change during stack branching");
		}
	}

	std::vector<LLVMValueRef> thePHIs;

	for (size_t i = size; i < items.size(); ++i) {
		auto& item = items[i];
		auto lltype = co_await compiler.translateType(item.type->destroyReferences()->reference());
		auto nullVal = LLVMConstNull(lltype);
		auto tmpIdx = compiler.nextTemp();
		auto tmpIdxStr = std::to_string(tmpIdx);
		auto countNullVal = item.countType ? LLVMConstNull(item.countType) : nullptr;

		auto phiType = lltype;
		auto phiCountType = item.countType;
		LLVMValueRef trueVal = LLVMConstInt(LLVMInt1TypeInContext(compiler._llcontext.get()), 1, false);

		if (suspendableContext) {
			// suspendable functions use "solid" PHIs, so the actual PHI instruction we use simply produces a bool (which we use later on)
			phiType = LLVMInt1TypeInContext(compiler._llcontext.get());
			phiCountType = phiType;
			nullVal = LLVMConstNull(phiType);
			countNullVal = nullVal;
		}

		auto merged = LLVMBuildPhi(compiler._builders.top().get(), phiType, ("@stack_branch_phi_" + tmpIdxStr + "_item_" + std::to_string(i)).c_str());
		LLVMValueRef countMerged = nullptr;

		if (item.count) {
			countMerged = LLVMBuildPhi(compiler._builders.top().get(), phiCountType, ("@stack_branch_phi_" + tmpIdxStr + "_count_" + std::to_string(i)).c_str());
		}

		bool found = false;

		for (size_t j = 0; j < mergingBlocks.size(); ++j) {
			if (mergingBlocks[j] == item.sourceBlock) {
				if (found) {
					throw std::runtime_error("Assumption violated: stack variables should only come from a single incoming block during a stack branch");
				}
				found = true;

				LLVMAddIncoming(merged, suspendableContext ? &trueVal : &item.source, &mergingBlocks[j], 1);
				if (item.count) {
					LLVMAddIncoming(countMerged, suspendableContext ? &trueVal : &item.count, &mergingBlocks[j], 1);
				}
			} else {
				LLVMAddIncoming(merged, &nullVal, &mergingBlocks[j], 1);
				if (item.count) {
					LLVMAddIncoming(countMerged, &countNullVal, &mergingBlocks[j], 1);
				}
			}
		}

		// we currently assume the variables are only declared (or PHIed) in the immediate blocks being merged.
		// just for sanity's sake, let's verify this assumption by making sure items pushed after the beginning of the branch only come from the blocks being merged.
		if (!found) {
			throw std::runtime_error("Assumption violated: when stack branching, stack variables must only be pushed within the blocks being merged");
		}

		if (suspendableContext) {
			thePHIs.push_back(merged);
			if (countMerged) {
				thePHIs.push_back(countMerged);
			}
		} else {
			item.sourceBlock = mergeBlock;
			item.source = merged;
			item.count = countMerged;
			item.branched = true;
		}
	}

	if (suspendableContext) {
		// we're in a suspendable function, so let's "solidify" the PHIs.
		// we do this so that we can refer to them even after a state change.
		//
		// TODO: avoid solidifying all PHIs; only solidify PHIs as necessary.
		size_t thePHIsIndex = 0;
		for (size_t i = size; i < items.size(); ++i) {
			auto& item = items[i];
			auto lltype = co_await compiler.translateType(item.type->destroyReferences()->reference());
			auto nullVal = LLVMConstNull(lltype);
			auto tmpIdx = compiler.nextTemp();
			auto tmpIdxStr = std::to_string(tmpIdx);
			auto countNullVal = item.countType ? LLVMConstNull(item.countType) : nullptr;

			auto merged = thePHIs[thePHIsIndex++];
			auto countMerged = item.count ? thePHIs[thePHIsIndex++] : nullptr;

			if (item.branched) {
				// we already solidified this item earlier, so let's just update that variable appropriately
				auto prev = LLVMBuildLoad2(compiler._builders.top().get(), lltype, item.source, ("@stack_branch_solid_phi_" + tmpIdxStr + "_load@" + std::to_string(i)).c_str());
				auto newVal = LLVMBuildSelect(compiler._builders.top().get(), merged, prev, nullVal, ("@stack_branch_solid_phi_" + tmpIdxStr + "_select@" + std::to_string(i)).c_str());
				LLVMBuildStore(compiler._builders.top().get(), newVal, item.source);

				if (countMerged) {
					auto prev = LLVMBuildLoad2(compiler._builders.top().get(), item.countType, item.count, ("@stack_branch_solid_phi_" + tmpIdxStr + "_load_count@" + std::to_string(i)).c_str());
					auto newVal = LLVMBuildSelect(compiler._builders.top().get(), countMerged, prev, countNullVal, ("@stack_branch_solid_phi_" + tmpIdxStr + "_select_count@" + std::to_string(i)).c_str());
					LLVMBuildStore(compiler._builders.top().get(), newVal, item.count);
				}
			} else {
				auto solid = buildAlloca(lltype);
				auto solidCount = countMerged ? buildAlloca(item.countType) : nullptr;

				auto newVal = LLVMBuildSelect(compiler._builders.top().get(), merged, item.source, nullVal, ("@stack_branch_solid_phi_" + tmpIdxStr + "_select@" + std::to_string(i)).c_str());
				LLVMBuildStore(compiler._builders.top().get(), newVal, solid);

				if (countMerged) {
					auto newVal = LLVMBuildSelect(compiler._builders.top().get(), countMerged, item.count, countNullVal, ("@stack_branch_solid_phi_" + tmpIdxStr + "_select_count@" + std::to_string(i)).c_str());
					LLVMBuildStore(compiler._builders.top().get(), newVal, solidCount);
				}

				item.source = solid;
				item.count = solidCount;
			}

			item.sourceBlock = mergeBlock;
			item.branched = true;
		}
	}

	co_return;
};

AltaLL::Compiler::Coroutine<void> AltaLL::Compiler::ScopeStack::cleanup(bool stateChange) {
	auto llblock = LLVMGetInsertBlock(compiler._builders.top().get());
	auto llfunc = stateChange ? compiler._suspendableDestructor.top().function : LLVMGetBasicBlockParent(llblock);

	if (!stateChange && LLVMGetBasicBlockTerminator(llblock)) {
		// we already have a terminator, so it's very likely that this block contains a return or an unreachable instruction.
		// in those cases, we've already performed the necessary cleanup (or don't need it), so go ahead and ignore this request.
		co_return;
	}

	for (auto rit = items.rbegin(); rit != items.rend(); ++rit) {
		const auto& item = *rit;
		LLVMBasicBlockRef doneBlock = NULL;

		if (item.type->referenceLevel() > 0) {
			continue;
		}

		size_t allocaIndex = SIZE_MAX;

		if (stateChange) {
			for (size_t i = 0; i < suspendableAllocations.size(); ++i) {
				if (suspendableAllocations[i] == item.source) {
					allocaIndex = i;
					break;
				}
			}

			if (allocaIndex == SIZE_MAX) {
				continue;
			}
		}

		auto tmpIdx = compiler.nextTemp();
		auto tmpIdxStr = std::to_string(tmpIdx);

		LLVMValueRef source = item.source;

		if (stateChange) {
			source = compiler.buildSuspendableAlloca(compiler._builders.top(), compiler.currentSuspendableContext());
			suspendableAllocationUpdateRequests[allocaIndex].push_back(source);
		}

		if (item.branched) {
			if (suspendableContext) {
				// this is actually a pointer to a pointer; let's load it so we have just a pointer
				auto lltype = co_await compiler.translateType(item.type->destroyReferences()->reference());
				source = LLVMBuildLoad2(compiler._builders.top().get(), lltype, source, ("@stack_cleanup_solid_phi_deref_" + tmpIdxStr).c_str());
			}

			// we need to check if it's non-null
			auto isNotNull = LLVMBuildIsNotNull(compiler._builders.top().get(), source, ("@stack_cleanup_is_not_null_" + tmpIdxStr).c_str());
			auto notNullBlock = LLVMAppendBasicBlockInContext(compiler._llcontext.get(), llfunc, ("@stack_cleanup_not_null_" + tmpIdxStr).c_str());

			doneBlock = LLVMAppendBasicBlockInContext(compiler._llcontext.get(), llfunc, ("@stack_cleanup_done_" + tmpIdxStr).c_str());

			LLVMBuildCondBr(compiler._builders.top().get(), isNotNull, notNullBlock, doneBlock);

			LLVMPositionBuilderAtEnd(compiler._builders.top().get(), notNullBlock);
		}

		if (item.count) {
			if (!doneBlock) {
				doneBlock = LLVMAppendBasicBlockInContext(compiler._llcontext.get(), llfunc, ("@stack_cleanup_done_" + tmpIdxStr).c_str());
			}

			auto counter = LLVMBuildAlloca(compiler._builders.top().get(), item.countType, ("@stack_cleanup_" + tmpIdxStr + "_counter").c_str());
			LLVMBuildStore(compiler._builders.top().get(), LLVMConstInt(item.countType, 0, false), counter);

			auto condBlock = LLVMAppendBasicBlockInContext(compiler._llcontext.get(), llfunc, ("@stack_cleanup_" + tmpIdxStr + "_cond").c_str());
			auto bodyBlock = LLVMAppendBasicBlockInContext(compiler._llcontext.get(), llfunc, ("@stack_cleanup_" + tmpIdxStr + "_body").c_str());

			LLVMBuildBr(compiler._builders.top().get(), condBlock);

			LLVMPositionBuilderAtEnd(compiler._builders.top().get(), condBlock);

			auto counterVal = LLVMBuildLoad2(compiler._builders.top().get(), item.countType, counter, ("@stack_cleanup_" + tmpIdxStr + "_counter_val").c_str());
			auto shouldDoIter = LLVMBuildICmp(compiler._builders.top().get(), LLVMIntULT, counterVal, item.count, ("@stack_cleanup_" + tmpIdxStr + "_should_do_iter").c_str());

			LLVMBuildCondBr(compiler._builders.top().get(), shouldDoIter, bodyBlock, doneBlock);

			LLVMPositionBuilderAtEnd(compiler._builders.top().get(), bodyBlock);

			std::array<LLVMValueRef, 1> gepIndices {
				counterVal,
			};

			assert(item.type->pointerLevel() > 0);
			auto arrayTarget = item.type->follow();

			auto elmRef = LLVMBuildGEP2(compiler._builders.top().get(), co_await compiler.translateType(arrayTarget), source, gepIndices.data(), gepIndices.size(), ("@stack_cleanup_" + tmpIdxStr + "_elm_ref").c_str());

			co_await compiler.doDtor(elmRef, arrayTarget, nullptr, false);

			auto inc = LLVMBuildAdd(compiler._builders.top().get(), counterVal, LLVMConstInt(item.countType, 1, false), ("@stack_cleanup_" + tmpIdxStr + "_counter_inc").c_str());
			LLVMBuildStore(compiler._builders.top().get(), inc, counter);

			LLVMBuildBr(compiler._builders.top().get(), condBlock);
		} else {
			co_await compiler.doDtor(source, item.type, nullptr, false);
		}

		if (item.branched) {
			if (!item.count) {
				LLVMBuildBr(compiler._builders.top().get(), doneBlock);
			}
		}

		if (doneBlock) {
			LLVMPositionBuilderAtEnd(compiler._builders.top().get(), doneBlock);
		}
	}

	if (suspendableContext) {
		compiler.buildSuspendablePopStack(compiler._builders.top(), compiler.currentSuspendableContext());
	}

	co_return;
};

void AltaLL::Compiler::ScopeStack::pushItem(LLVMValueRef memory, std::shared_ptr<AltaCore::DET::Type> type) {
	pushItem(memory, type, LLVMGetInsertBlock(compiler._builders.top().get()));
};

void AltaLL::Compiler::ScopeStack::pushItem(LLVMValueRef memory, std::shared_ptr<AltaCore::DET::Type> type, LLVMBasicBlockRef sourceBlock) {
	items.emplace_back(ScopeItem { sourceBlock, memory, type, nullptr, nullptr });
};

void AltaLL::Compiler::ScopeStack::pushRuntimeArray(LLVMValueRef array, LLVMValueRef count, std::shared_ptr<AltaCore::DET::Type> elementType, LLVMTypeRef countType) {
	pushRuntimeArray(array, count, elementType, countType, LLVMGetInsertBlock(compiler._builders.top().get()));
};

void AltaLL::Compiler::ScopeStack::pushRuntimeArray(LLVMValueRef array, LLVMValueRef count, std::shared_ptr<AltaCore::DET::Type> elementType, LLVMTypeRef countType, LLVMBasicBlockRef sourceBlock) {
	items.emplace_back(ScopeItem { sourceBlock, array, elementType, count, countType });
};

AltaLL::Compiler::ScopeStack::ScopeStack(Compiler& _compiler, Type _type):
	compiler(_compiler),
	type(_type)
{
	if (!compiler.inSuspendableFunction()) {
		// we don't need to do anything fancy
		return;
	}

	auto insertBlock = LLVMGetInsertBlock(compiler._builders.top().get());
	suspendableContext = compiler.currentSuspendableContext();
	suspendableStackPush = compiler.buildSuspendablePushStack(compiler._builders.top(), suspendableContext, static_cast<size_t>(0));

	auto func = LLVMGetBasicBlockParent(insertBlock);

	suspendableReloadBlock = LLVMAppendBasicBlockInContext(compiler._llcontext.get(), func, "@reload_stack");
	suspendableReloadBuilder = llwrap(LLVMCreateBuilderInContext(compiler._llcontext.get()));
	suspendableClass = compiler._suspendableClass.top();
	LLVMPositionBuilderAtEnd(suspendableReloadBuilder.get(), suspendableReloadBlock);

	compiler.setBuilderDebugLocation(suspendableReloadBuilder, 0, 0);

	compiler.buildSuspendableReloadContinue(suspendableReloadBuilder, suspendableContext);

	LLVMBuildBr(compiler._builders.top().get(), suspendableReloadBlock);

	auto continuation = LLVMAppendBasicBlockInContext(compiler._llcontext.get(), func, "@reload_stack_continue");
	suspendableContinuations.emplace_back(compiler._suspendableStateIndex.top(), continuation);
	LLVMPositionBuilderAtEnd(_compiler._builders.top().get(), continuation);
};

AltaLL::Compiler::Coroutine<void> AltaLL::Compiler::ScopeStack::popping() {
	if (!suspendableContext) {
		co_return;
	}

	std::vector<LLVMTypeRef> tmpStackTypes {
		compiler._definedTypes["_Alta_basic_suspendable_stack"],
	};
	tmpStackTypes.insert(tmpStackTypes.end(), suspendableAllocationTypes.begin(), suspendableAllocationTypes.end());

	auto stackStruct = LLVMStructTypeInContext(compiler._llcontext.get(), tmpStackTypes.data(), tmpStackTypes.size(), false);
	auto totalStackSize = LLVMStoreSizeOfType(compiler._targetData.get(), stackStruct);

	for (size_t i = 0; i < suspendableAllocationTypes.size(); ++i) {
		compiler.updateSuspendableAlloca(suspendableAllocations[i], LLVMOffsetOfElement(compiler._targetData.get(), stackStruct, i + 1));
	}

	for (const auto& [index, allocas]: suspendableAllocationUpdateRequests) {
		for (const auto& alloca: allocas) {
			compiler.updateSuspendableAlloca(alloca, LLVMOffsetOfElement(compiler._targetData.get(), stackStruct, index + 1));
		}
	}

	auto builder = llwrap(LLVMCreateBuilderInContext(compiler._llcontext.get()));

	compiler.setBuilderDebugLocation(builder, 0, 0);

	compiler.updateSuspendablePushStack(suspendableStackPush, totalStackSize);

	auto func = LLVMGetBasicBlockParent(suspendableReloadBlock);
	auto badBlock = LLVMAppendBasicBlockInContext(compiler._llcontext.get(), func, "@bad_state_index");
	LLVMPositionBuilderAtEnd(suspendableReloadBuilder.get(), badBlock);
	compiler.buildBadSuspendableState(suspendableReloadBuilder, suspendableContext);
	LLVMPositionBuilderAtEnd(suspendableReloadBuilder.get(), suspendableReloadBlock);

	auto gepIndexType = LLVMInt64TypeInContext(compiler._llcontext.get());
	auto gepStructIndexType = LLVMInt32TypeInContext(compiler._llcontext.get());

	std::array<LLVMValueRef, 4> gepIndices {
		LLVMConstInt(gepIndexType, 0, false), // the first element of the "array"
		LLVMConstInt(gepStructIndexType, 0, false), // @Suspendable@::basic_context
		LLVMConstInt(gepStructIndexType, 0, false), // _Alta_basic_{coroutine,generator}::basic_suspendable
		LLVMConstInt(gepStructIndexType, 1, false), // _Alta_basic_suspendable::state
	};
	auto stateIndexGEP = LLVMBuildGEP2(suspendableReloadBuilder.get(), co_await compiler.defineClassType(suspendableClass), suspendableContext, gepIndices.data(), gepIndices.size(), "@stack_reload_state_index_ref");
	auto stateIndexLoad = LLVMBuildLoad2(suspendableReloadBuilder.get(), LLVMInt64TypeInContext(compiler._llcontext.get()), stateIndexGEP, "@stack_reload_state_index");

	auto switchInstr = LLVMBuildSwitch(suspendableReloadBuilder.get(), stateIndexLoad, badBlock, suspendableContinuations.size());

	for (const auto& [stateIndex, returnBlock]: suspendableContinuations) {
		LLVMAddCase(switchInstr, LLVMConstInt(LLVMInt64TypeInContext(compiler._llcontext.get()), stateIndex, false), returnBlock);
	}
};

LLVMValueRef AltaLL::Compiler::ScopeStack::buildAlloca(LLVMTypeRef lltype, const std::string& name) {
	if (suspendableContext) {
		auto alloca = compiler.buildSuspendableAlloca(suspendableReloadBuilder, suspendableContext);
		suspendableAllocationTypes.push_back(lltype);
		suspendableAllocations.push_back(alloca);
		return alloca;
	} else {
		return LLVMBuildAlloca(compiler._builders.top().get(), lltype, name.c_str());
	}
};

LLVMValueRef AltaLL::Compiler::ScopeStack::buildArrayAlloca(LLVMTypeRef lltype, size_t elementCount, const std::string& name) {
	if (suspendableContext) {
		auto alloca = compiler.buildSuspendableAlloca(suspendableReloadBuilder, suspendableContext);
		suspendableAllocationTypes.push_back(LLVMArrayType(lltype, elementCount));
		suspendableAllocations.push_back(alloca);
		return alloca;
	} else {
		return LLVMBuildArrayAlloca(compiler._builders.top().get(), lltype, LLVMConstInt(LLVMInt64TypeInContext(compiler._llcontext.get()), elementCount, false), name.c_str());
	}
};

AltaLL::Compiler::Coroutine<LLVMTypeRef> AltaLL::Compiler::translateType(std::shared_ptr<AltaCore::DET::Type> type, bool usePointersToFunctions, bool useStructForVoid) {
	auto mangled = "_non_native_" + mangleType(type);

	if (usePointersToFunctions && type->isFunction && type->isRawFunction) {
		mangled = "_ptr_" + mangled;
	}

	if (!useStructForVoid) {
		mangled = "_real_void_" + mangled;
	}

	if (_definedTypes[mangled.c_str()]) {
		co_return _definedTypes[mangled.c_str()];
	}

	LLVMTypeRef result = NULL;

	if (type->isNative && !type->isFunction) {
		switch (type->nativeTypeName) {
			case AltaCore::DET::NativeType::Integer: {
				uint8_t bits = 32;

				for (const auto& modifier: type->modifiers) {
					if (modifier & static_cast<uint8_t>(AltaCore::DET::TypeModifierFlag::Long)) {
						if (bits < 64) {
							bits *= 2;
						}
					}

					if (modifier & static_cast<uint8_t>(AltaCore::DET::TypeModifierFlag::Short)) {
						if (bits > 8) {
							bits /= 2;
						}
					}

					if (modifier & (static_cast<uint8_t>(AltaCore::DET::TypeModifierFlag::Pointer) | static_cast<uint8_t>(AltaCore::DET::TypeModifierFlag::Reference))) {
						break;
					}
				}

				result = LLVMIntTypeInContext(_llcontext.get(), bits);
			} break;
			case AltaCore::DET::NativeType::Byte:        result = LLVMInt8TypeInContext(_llcontext.get()); break;
			case AltaCore::DET::NativeType::Bool:        result = LLVMInt1TypeInContext(_llcontext.get()); break;
			case AltaCore::DET::NativeType::Void:
				if (useStructForVoid) {
					result = LLVMStructTypeInContext(_llcontext.get(), NULL, 0, false);
				} else {
					result = LLVMVoidTypeInContext(_llcontext.get());
				}
				break;
			case AltaCore::DET::NativeType::Double:      result = LLVMDoubleTypeInContext(_llcontext.get()); break;
			case AltaCore::DET::NativeType::Float:       result = LLVMFloatTypeInContext(_llcontext.get()); break;
			case AltaCore::DET::NativeType::UserDefined: result = NULL; break;
		}
	} else if (type->isFunction) {
		if (type->isRawFunction) {
			auto retType = co_await translateType(type->returnType, true, false);
			std::vector<LLVMTypeRef> params;
			bool isVararg = false;

			if (type->isMethod) {
				params.push_back(co_await translateType(std::make_shared<AltaCore::DET::Type>(type->methodParent)->reference()));
			}

			for (const auto& [name, paramType, isVariable, id]: type->parameters) {
				auto lltype = co_await translateType(paramType);

				if (isVariable) {
					if (varargTable[id]) {
						isVararg = true;
					} else {
						params.push_back(LLVMInt64TypeInContext(_llcontext.get()));
						params.push_back(LLVMPointerType(lltype, 0));
					}
				} else {
					params.push_back(lltype);
				}
			}

			result = LLVMFunctionType(retType, params.data(), params.size(), isVararg);

			if (usePointersToFunctions) {
				result = LLVMPointerType(result, 0);
			}
		} else {
			result = _definedTypes["_Alta_basic_function"];
		}
	} else if (type->isUnion()) {
		std::array<LLVMTypeRef, 2> members;
		size_t unionSize = 0;

		members[0] = LLVMIntTypeInContext(_llcontext.get(), std::bit_width(type->unionOf.size() - 1));

		auto tagSize = LLVMStoreSizeOfType(_targetData.get(), members[0]);

		for (const auto& component: type->unionOf) {
			auto lltype = co_await translateType(component);

			members[1] = lltype;
			auto tmpStruct = LLVMStructTypeInContext(_llcontext.get(), members.data(), members.size(), false);
			members[1] = nullptr;

			auto lltotalSize = LLVMStoreSizeOfType(_targetData.get(), tmpStruct);

			if (lltotalSize > unionSize) {
				unionSize = lltotalSize;
			}
		}

		members[1] = LLVMArrayType(LLVMInt8TypeInContext(_llcontext.get()), unionSize - tagSize);

		result = LLVMStructTypeInContext(_llcontext.get(), members.data(), members.size(), false);
	} else if (type->isOptional) {
		std::array<LLVMTypeRef, 2> members;

		bool isVoid = *type->optionalTarget == AltaCore::DET::Type(AltaCore::DET::NativeType::Void);

		members[0] = LLVMInt1TypeInContext(_llcontext.get());

		if (isVoid) {
			members[1] = LLVMStructTypeInContext(_llcontext.get(), NULL, 0, false);
		} else {
			members[1] = co_await translateType(type->optionalTarget);
		}

		result = LLVMStructTypeInContext(_llcontext.get(), members.data(), members.size(), false);
	} else if (type->bitfield) {
		result = co_await translateType(type->bitfield->underlyingBitfieldType.lock());
	} else if (type->klass) {
		result = co_await defineClassType(type->klass);
	}

	if (!result) {
		co_return result;
	}

	for (const auto& modifier: type->modifiers) {
		if (modifier & (static_cast<uint8_t>(AltaCore::DET::TypeModifierFlag::Pointer) | static_cast<uint8_t>(AltaCore::DET::TypeModifierFlag::Reference))) {
			result = LLVMPointerType(result, 0);
		}
	}

	_definedTypes[mangled.c_str()] = result;

	co_return result;
};

const AltaLL::Compiler::CopyInfo AltaLL::Compiler::defaultCopyInfo = std::make_pair(false, false);

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::cast(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType, std::shared_ptr<AltaCore::DET::Type> dest, bool copy, CopyInfo additionalCopyInfo, bool manual, AltaCore::Errors::Position* position) {
	namespace DET = AltaCore::DET;

	auto tmpIdx = nextTemp();
	auto tmpIdxStr = std::to_string(tmpIdx);

	if (dest->isExactlyCompatibleWith(*exprType)) {
		if (
			copy &&
			additionalCopyInfo.first &&
			(
				dest->indirectionLevel() < 1 ||
				(!dest->isUnion() && exprType->isUnion())
			)
		) {
			expr = co_await doCopyCtor(expr, exprType, additionalCopyInfo);
		}
		co_return expr;
	}

	// native type (e.g. integer) -> bool
	//
	// done instead of simple coercion because simple coercion might truncate/overflow the value
	// and end up with zero (which is falsy) instead of the proper truthy value
	if (*dest == DET::Type(DET::NativeType::Bool) && (exprType->isNative || exprType->pointerLevel() > 0)) {
		auto llintType = co_await translateType(exprType->destroyReferences());
		expr = co_await loadRef(expr, exprType);
		if (exprType->pointerLevel() > 0) {
			llintType = LLVMInt64TypeInContext(_llcontext.get());
			expr = LLVMBuildPtrToInt(_builders.top().get(), expr, llintType, ("@cast_ptr_to_int_for_bool_" + tmpIdxStr).c_str());
		}
		if (exprType->pointerLevel() > 0 || !(*exprType == DET::Type(DET::NativeType::Bool))) {
			expr = LLVMBuildICmp(_builders.top().get(), LLVMIntNE, expr, LLVMConstInt(llintType, 0, false), ("@cast_to_bool_" + tmpIdxStr).c_str());
		}
		co_return expr;
	}

	auto path = AltaCore::DET::Type::findCast(exprType, dest, manual);

	if (path.size() == 0) {
		std::string message = "no way to cast from (" + exprType->toString() + ") to (" + dest->toString() + ')';
		if (position) {
			throw AltaCore::Errors::ValidationError(message, *position);
		} else {
			throw std::runtime_error(message);
		}
	}

	LLVMValueRef result = expr;

	auto currentType = exprType;
	auto canCopy = [&](std::shared_ptr<DET::Type> type = nullptr) {
		if (!type) type = currentType;
		return (!type->isNative || !type->isRawFunction) && type->indirectionLevel() < 1 && (!type->klass || type->klass->copyConstructor);
	};

	for (size_t i = 0; i < path.size(); i++) {
		using CCT = AltaCore::DET::CastComponentType;
		using SCT = AltaCore::DET::SpecialCastType;
		auto& component = path[i];
		if (component.type == CCT::Destination) {
			if (component.special == SCT::OptionalPresent) {
				if (!result) {
					throw std::runtime_error("This should be impossible (cast:optional_present)");
				}
				result = co_await loadRef(result, currentType);
				if (currentType->indirectionLevel() == 0 && !additionalCopyInfo.first && !copy) {
					// it *should* be safe to tmpify whenever we can't copy
					co_await tmpify(result, currentType);
				}
				result = LLVMBuildExtractValue(_builders.top().get(), result, 0, ("@check_opt_present_" + tmpIdxStr).c_str());
				currentType = std::make_shared<DET::Type>(DET::NativeType::Bool);
				copy = false;
			} else if (component.special == SCT::EmptyOptional) {
				std::array<LLVMValueRef, 2> vals;
				vals[0] = LLVMConstInt(LLVMInt1TypeInContext(_llcontext.get()), 0, false);
				vals[1] = LLVMGetPoison(co_await translateType(dest->optionalTarget));
				result = LLVMConstNamedStruct(co_await translateType(dest), vals.data(), vals.size());
				currentType = dest;
				copy = false;
			} else if (component.special == SCT::WrapFunction) {
				auto wrappedCopy = exprType->copy();
				wrappedCopy->isRawFunction = false;
				currentType = dest;
				copy = false;

				auto wrapped = LLVMConstNull(co_await translateType(wrappedCopy));
				result = LLVMBuildInsertValue(_builders.top().get(), wrapped, result, 0, ("@function_wrap_" + tmpIdxStr).c_str());
			}
			bool didCopy = false;
			if (copy && additionalCopyInfo.first && canCopy()) {
				result = co_await doCopyCtor(result, currentType, additionalCopyInfo, &didCopy);
			}
		} else if (component.type == CCT::SimpleCoercion) {
			if (currentType->referenceLevel() > 0 || component.target->referenceLevel() > 0) {
				throw std::runtime_error("Unsupported cast (cannot simply coerce with references)");
			}

			auto lltarget = co_await translateType(component.target);
			bool currentIsFP = currentType->nativeTypeName == DET::NativeType::Float || currentType->nativeTypeName == DET::NativeType::Double;
			bool targetIsFP = component.target->nativeTypeName == DET::NativeType::Float || component.target->nativeTypeName == DET::NativeType::Double;
			bool currentIsPointer = currentType->pointerLevel() > 0 || (currentType->isFunction && currentType->isRawFunction);
			bool targetIsPointer = component.target->pointerLevel() > 0 || (component.target->isFunction && component.target->isRawFunction);
			bool currentIsIntegral = currentType->isNative && !currentType->isFunction;
			bool targetIsIntegral = component.target->isNative && !component.target->isFunction;

			if (currentIsPointer && targetIsIntegral) {
				result = LLVMBuildPtrToInt(_builders.top().get(), result, lltarget, ("@cast_coerce_" + tmpIdxStr).c_str());
			} else if (currentIsIntegral && targetIsPointer) {
				result = LLVMBuildIntToPtr(_builders.top().get(), result, lltarget, ("@cast_coerce_" + tmpIdxStr).c_str());
			} else if (currentIsPointer && targetIsPointer) {
				result = LLVMBuildBitCast(_builders.top().get(), result, lltarget, ("@cast_coerce_" + tmpIdxStr).c_str());
			} else if (currentIsFP && targetIsFP) {
				result = LLVMBuildFPCast(_builders.top().get(), result, lltarget, ("@cast_coerce_" + tmpIdxStr).c_str());
			} else if (currentIsFP) {
				result = (component.target->isSigned() ? LLVMBuildFPToSI : LLVMBuildFPToUI)(_builders.top().get(), result, lltarget, ("@cast_coerce_" + tmpIdxStr).c_str());
			} else if (targetIsFP) {
				result = (currentType->isSigned() ? LLVMBuildSIToFP : LLVMBuildUIToFP)(_builders.top().get(), result, lltarget, ("@cast_coerce_" + tmpIdxStr).c_str());
			} else {
				result = LLVMBuildIntCast2(_builders.top().get(), result, lltarget, component.target->isSigned(), ("@cast_coerce_" + tmpIdxStr).c_str());
			}

			currentType = component.target;
			copy = false;
			additionalCopyInfo = std::make_pair(false, true);
		} else if (component.type == CCT::Upcast) {
			bool didRetrieval = false;
			auto nextType = std::make_shared<DET::Type>(component.klass);

			if (currentType->pointerLevel() > 0) {
				nextType = nextType->point();
			} else if (currentType->referenceLevel() > 0) {
				nextType = nextType->reference();
			}

			if (currentType->pointerLevel() > 1) {
				throw std::runtime_error("too much indirection for upcast");
			}

			result = co_await doParentRetrieval(result, currentType, nextType, &didRetrieval);
			if (!didRetrieval) {
				throw std::runtime_error("supposed to be able to do parent retrieval");
			}

			currentType = nextType;
			additionalCopyInfo = std::make_pair(true, false);
		} else if (component.type == CCT::Downcast) {
			bool didRetrieval = false;
			auto nextType = std::make_shared<DET::Type>(component.klass);

			if (currentType->pointerLevel() > 0) {
				nextType = nextType->point();
			} else if (currentType->referenceLevel() > 0) {
				nextType = nextType->reference();
			}

			if (currentType->pointerLevel() > 1) {
				throw std::runtime_error("too much indirection for downcast");
			}

			if (currentType->indirectionLevel() == 0) {
				throw std::runtime_error("not enough indirection for downcast");
			}

			result = co_await doChildRetrieval(result, currentType, nextType, &didRetrieval);
			if (!didRetrieval) {
				throw std::runtime_error("supposed to be able to do child retrieval");
			}

			currentType = nextType;
			additionalCopyInfo = std::make_pair(false, false);
		} else if (component.type == CCT::Reference) {
			if (additionalCopyInfo.second) {
				// it *should* be safe to tmpify whenever we can't copy
				result = co_await tmpify(result, currentType, !additionalCopyInfo.first && !copy);
			}

			currentType = currentType->reference();
			copy = false;
			additionalCopyInfo = std::make_pair(false, true);
		} else if (component.type == CCT::Dereference) {
			result = LLVMBuildLoad2(_builders.top().get(), co_await translateType(currentType->dereference()), result, ("@cast_deref_" + tmpIdxStr).c_str());
			currentType = currentType->dereference();
			additionalCopyInfo = std::make_pair(true, true);
		} else if (component.type == CCT::Wrap) {
			bool didCopy = false;

			if (copy && additionalCopyInfo.first && canCopy()) {
				result = co_await doCopyCtor(result, currentType, additionalCopyInfo, &didCopy);
				copy = false;
			}

			currentType = std::make_shared<DET::Type>(true, currentType);
			auto lltype = co_await translateType(currentType);
			auto tmp = LLVMBuildInsertValue(_builders.top().get(), LLVMGetUndef(lltype), LLVMConstInt(LLVMInt1TypeInContext(_llcontext.get()), 1, false), 0, ("@cast_wrap_start_" + tmpIdxStr).c_str());
			result = LLVMBuildInsertValue(_builders.top().get(), tmp, result, 1, ("@cast_wrap_" + tmpIdxStr).c_str());

			copy = false;
			additionalCopyInfo = std::make_pair(false, true);
		} else if (component.type == CCT::Unwrap) {
			result = co_await loadRef(result, currentType);
			result = LLVMBuildExtractValue(_builders.top().get(), result, 1, ("@cast_unwrap_" + tmpIdxStr).c_str());
			currentType = currentType->optionalTarget;
			additionalCopyInfo = std::make_pair(true, true);
		} else if (component.type == CCT::Widen) {
			auto memberType = component.via;
			size_t memberIndex = 0;

			for (; memberIndex < component.target->unionOf.size(); ++memberIndex) {
				if (*component.target->unionOf[memberIndex] == *memberType) {
					memberIndex = memberIndex;
					break;
				}
			}

			if (memberIndex == component.target->unionOf.size()) {
				throw std::runtime_error("impossible");
			}

			auto lltarget = co_await translateType(component.target);
			auto allocated = LLVMBuildAlloca(_builders.top().get(), lltarget, ("@cast_widen_tmp_" + tmpIdxStr).c_str());

			std::array<LLVMTypeRef, 2> members;
			members[0] = LLVMIntTypeInContext(_llcontext.get(), std::bit_width(component.target->unionOf.size() - 1));
			members[1] = co_await translateType(component.via);
			auto llactual = LLVMStructTypeInContext(_llcontext.get(), members.data(), members.size(), false);

			auto casted = LLVMBuildBitCast(_builders.top().get(), allocated, LLVMPointerType(llactual, 0), ("@cast_widen_tmp_fake_" + tmpIdxStr).c_str());

			auto tmp = LLVMBuildInsertValue(_builders.top().get(), LLVMGetUndef(llactual), LLVMConstInt(members[0], memberIndex, false), 0, ("@cast_widen_val_start_" + tmpIdxStr).c_str());
			result = LLVMBuildInsertValue(_builders.top().get(), tmp, result, 1, ("@cast_widen_val_" + tmpIdxStr).c_str());

			LLVMBuildStore(_builders.top().get(), result, casted);

			result = LLVMBuildLoad2(_builders.top().get(), lltarget, allocated, ("@cast_widen_" + tmpIdxStr).c_str());

			currentType = component.target;
			copy = false;
			additionalCopyInfo = std::make_pair(false, true);
		} else if (component.type == CCT::Narrow) {
			result = co_await loadRef(result, currentType);

			size_t memberIdx = 0;

			for (size_t i = 0; i < currentType->unionOf.size(); i++) {
				auto& item = currentType->unionOf[i];
				if (*item == *component.target) {
					memberIdx = i;
					break;
				}
			}

			if (memberIdx == currentType->unionOf.size()) {
				throw std::runtime_error("impossible");
			}

			auto llcurrent = co_await translateType(currentType->destroyReferences());
			auto allocated = LLVMBuildAlloca(_builders.top().get(), llcurrent, ("@cast_narrow_tmp_" + tmpIdxStr).c_str());

			LLVMBuildStore(_builders.top().get(), result, allocated);

			std::array<LLVMTypeRef, 2> members;
			members[0] = LLVMIntTypeInContext(_llcontext.get(), std::bit_width(currentType->unionOf.size() - 1));
			members[1] = co_await translateType(component.target);
			auto llactual = LLVMStructTypeInContext(_llcontext.get(), members.data(), members.size(), false);

			auto casted = LLVMBuildBitCast(_builders.top().get(), allocated, LLVMPointerType(llactual, 0), "");

			auto tmp = LLVMBuildLoad2(_builders.top().get(), llactual, casted, ("@cast_narrow_load_" + tmpIdxStr).c_str());
			result = LLVMBuildExtractValue(_builders.top().get(), tmp, 1, ("@cast_narrow_" + tmpIdxStr).c_str());

			additionalCopyInfo = std::make_pair(true, true);
			currentType = component.target;
			bool didCopy = false;

			if (copy && additionalCopyInfo.first && canCopy()) {
				result = co_await doCopyCtor(result, currentType, additionalCopyInfo, &didCopy);
				copy = false;
			}
		} else if (component.type == CCT::From) {
			// this one is a little different, because we need to make sure we always copy if we can
			// because we're passing the value into a function that assumes it receives a copy it can
			// push onto the stack and destroy, so we need to make sure that's what it gets
			if (/*copy &&*/ additionalCopyInfo.first && canCopy(component.method->parameterVariables[0]->type)) {
				result = co_await doCopyCtor(result, currentType, additionalCopyInfo);
				copy = false;
			}

			auto [lltype, llfunc] = co_await declareFunction(component.method);

			std::array<LLVMValueRef, 1> args { result };
			result = LLVMBuildCall2(_builders.top().get(), lltype, llfunc, args.data(), args.size(), ("@cast_from_" + tmpIdxStr).c_str());

			copy = false;
			additionalCopyInfo = std::make_pair(false, true);
			currentType = std::make_shared<DET::Type>(component.method->parentScope.lock()->parentClass.lock());
		} else if (component.type == CCT::To) {
			auto to = component.method;
			auto classType = std::make_shared<DET::Type>(component.method->parentScope.lock()->parentClass.lock());

			if (currentType->referenceLevel() == 0) {
				// XXX: not sure if this is safe to place on the stack in all cases
				result = co_await tmpify(result, currentType);
			} else if (currentType->indirectionLevel() > 0) {
				result = co_await loadIndirection(result, currentType, 1);
			}

			auto [lltype, llfunc] = co_await declareFunction(component.method);

			std::array<LLVMValueRef, 1> args { result };
			result = LLVMBuildCall2(_builders.top().get(), lltype, llfunc, args.data(), args.size(), ("@cast_to_" + tmpIdxStr).c_str());

			currentType = component.method->returnType;
			copy = false;
			additionalCopyInfo = std::make_pair(false, true);
		} else if (component.type == CCT::Multicast) {
			bool didCopy = false;

			if (copy && additionalCopyInfo.first && canCopy()) {
				result = co_await doCopyCtor(result, currentType, additionalCopyInfo, &didCopy);
				copy = false;
			}

			auto entryBlock = LLVMGetInsertBlock(_builders.top().get());
			auto parentFunc = LLVMGetBasicBlockParent(entryBlock);

			// first, add the default (bad cast) block
			auto badCastBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), parentFunc, ("@cast_multicast_bad_cast_" + tmpIdxStr).c_str());

			// now move the builder to the bad cast block and build it
			LLVMPositionBuilderAtEnd(_builders.top().get(), badCastBlock);
			buildBadCast(_builders.top(), currentType->toString(), component.target->toString());

			LLVMPositionBuilderAtEnd(_builders.top().get(), entryBlock);

			if (!result) {
				throw std::runtime_error("This should be impossible (cast:multicast)");
			}
			result = co_await forEachUnionMember(result, currentType, co_await translateType(component.target), [&](LLVMValueRef memberRef, std::shared_ptr<AltaCore::DET::Type> memberType, size_t memberIndex) -> LLCoroutine {
				if (component.multicasts[i].second.size() == 0) {
					LLVMBuildBr(_builders.top().get(), badCastBlock);
					co_return NULL;
				} else {
					auto llmemberType = co_await translateType(memberType);
					auto member = LLVMBuildLoad2(_builders.top().get(), llmemberType, memberRef, ("@cast_multicast_" + tmpIdxStr + "_load_member_" + std::to_string(memberIndex)).c_str());

					auto casted = co_await cast(member, memberType, component.target, false, std::make_pair(false, true), false, position);
					co_return casted;
				}
			});

			currentType = component.target;
			copy = false;
			additionalCopyInfo = std::make_pair(false, true);
		}
	}

	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::doCopyCtorInternal(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType, CopyInfo additionalCopyInfo, bool* didCopy) {
	auto result = expr;
	auto tmpIdx = nextTemp();
	auto tmpIdxStr = std::to_string(tmpIdx);
	bool didCopyInternal = false;

	if (exprType->isNative) {
		if (exprType->isFunction && !exprType->isRawFunction) {
			// for now, lambdas always have retain-release semantics, so we just have to increment the reference count within the lambda state (if we have a lambda).

			result = co_await loadRef(result, exprType);

			auto lambdaState = LLVMBuildExtractValue(_builders.top().get(), result, 1, ("@copy_lambda_state_" + tmpIdxStr).c_str());
			auto lambdaStateIsNotNull = LLVMBuildIsNotNull(_builders.top().get(), lambdaState, ("@copy_lambda_state_not_null_" + tmpIdxStr).c_str());

			auto llfunc = LLVMGetBasicBlockParent(LLVMGetInsertBlock(_builders.top().get()));
			auto doIncRefBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@copy_lambda_inc_ref_" + tmpIdxStr).c_str());
			auto doneBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@copy_lambda_done_" + tmpIdxStr).c_str());

			LLVMBuildCondBr(_builders.top().get(), lambdaStateIsNotNull, doIncRefBlock, doneBlock);

			LLVMPositionBuilderAtEnd(_builders.top().get(), doIncRefBlock);

			auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
			auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());
			auto refCountType = LLVMInt64TypeInContext(_llcontext.get());

			std::array<LLVMValueRef, 2> gepIndices {
				LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
				LLVMConstInt(gepStructIndexType, 0, false), // _Alta_basic_lambda_state::reference_count
			};

			auto refCountPtr = LLVMBuildGEP2(_builders.top().get(), _definedTypes["_Alta_basic_lambda_state"], lambdaState, gepIndices.data(), gepIndices.size(), ("@copy_lambda_state_ref_ptr_" + tmpIdxStr).c_str());
			LLVMBuildAtomicRMW(_builders.top().get(), LLVMAtomicRMWBinOpAdd, refCountPtr, LLVMConstInt(refCountType, 1, false), LLVMAtomicOrderingRelease, false);
			LLVMBuildBr(_builders.top().get(), doneBlock);

			LLVMPositionBuilderAtEnd(_builders.top().get(), doneBlock);
		}
	} else {
		if (additionalCopyInfo.second && exprType->indirectionLevel() < 1) {
			// XXX: check this; not sure if we should put it on the stack or not
			result = co_await tmpify(result, exprType, false);
			exprType = exprType->reference();
		}

		LLVMValueRef copyFunc = NULL;
		LLVMTypeRef copyFuncType = NULL;
		auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
		auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());

		if (exprType->isUnion()) {
			auto llunionType = co_await translateType(exprType->destroyReferences());

			auto idxType = LLVMIntTypeInContext(_llcontext.get(), std::bit_width(exprType->unionOf.size() - 1));

			result = co_await forEachUnionMember(result, exprType, llunionType, [&](LLVMValueRef memberRef, std::shared_ptr<AltaCore::DET::Type> memberType, size_t memberIndex) -> LLCoroutine {
				bool didCopyMember = false;
				auto copied = co_await doCopyCtor(memberRef, memberType->reference(), std::make_pair(true, true), &didCopyMember);
				if (!didCopyMember) {
					copied = LLVMBuildLoad2(_builders.top().get(), co_await translateType(memberType), copied, ("@copy_union_" + tmpIdxStr + "_member_" + std::to_string(memberIndex) + "_load").c_str());
				}
				auto tmp = LLVMBuildAlloca(_builders.top().get(), llunionType, ("@copy_union_" + tmpIdxStr + "_member_" + std::to_string(memberIndex) + "_tmp").c_str());

				std::array<LLVMTypeRef, 2> members;
				members[0] = idxType;
				members[1] = co_await translateType(memberType);
				auto llactual = LLVMStructTypeInContext(_llcontext.get(), members.data(), members.size(), false);

				auto llactualPtr = LLVMBuildPointerCast(_builders.top().get(), tmp, LLVMPointerType(llactual, 0), ("@copy_union_" + tmpIdxStr + "_member_" + std::to_string(memberIndex) + "_fake").c_str());

				std::array<LLVMValueRef, 2> idxGEPIndices {
					LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
					LLVMConstInt(gepStructIndexType, 0, false), // the index within the union
				};
				auto memberIdxPtr = LLVMBuildGEP2(_builders.top().get(), llactual, llactualPtr, idxGEPIndices.data(), idxGEPIndices.size(), ("@copy_union_" + tmpIdxStr + "_member_idx_" + std::to_string(memberIndex) + "_ref").c_str());

				LLVMBuildStore(_builders.top().get(), LLVMConstInt(idxType, memberIndex, false), memberIdxPtr);

				std::array<LLVMValueRef, 2> gepIndices {
					LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
					LLVMConstInt(gepStructIndexType, 1, false), // the member within the union
				};
				auto memberTmpPtr = LLVMBuildGEP2(_builders.top().get(), llactual, llactualPtr, gepIndices.data(), gepIndices.size(), ("@copy_union_" + tmpIdxStr + "_member_" + std::to_string(memberIndex) + "_ref").c_str());

				LLVMBuildStore(_builders.top().get(), copied, memberTmpPtr);

				co_return LLVMBuildLoad2(_builders.top().get(), llunionType, tmp, ("@copy_union_" + tmpIdxStr + "_member_" + std::to_string(memberIndex)).c_str());
			});

			didCopyInternal = true;
		} else if (exprType->isOptional && *exprType->optionalTarget == AltaCore::DET::Type(AltaCore::DET::NativeType::Void)) {
			// this is basically just a boolean
			result = co_await loadRef(result, exprType);
			didCopyInternal = true;
		} else if (exprType->isOptional) {
			auto lloptionalType = co_await translateType(exprType->destroyReferences());

			auto entryBlock = LLVMGetInsertBlock(_builders.top().get());
			auto parentFunc = LLVMGetBasicBlockParent(entryBlock);
			auto presentBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), parentFunc, ("@copy_opt_" + tmpIdxStr + "_present").c_str());
			auto doneBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), parentFunc, ("@copy_opt_" + tmpIdxStr + "_done").c_str());

			auto isPresent = LLVMBuildExtractValue(_builders.top().get(), co_await loadRef(result, exprType), 0, ("@copy_opt_" + tmpIdxStr + "_is_present").c_str());

			currentStack().beginBranch();

			LLVMBuildCondBr(_builders.top().get(), isPresent, presentBlock, doneBlock);

			LLVMPositionBuilderAtEnd(_builders.top().get(), presentBlock);
			std::array<LLVMValueRef, 2> gepIndices {
				LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
				LLVMConstInt(gepStructIndexType, 1, false), // the value within the optional
			};
			auto valRef = LLVMBuildGEP2(_builders.top().get(), lloptionalType, result, gepIndices.data(), gepIndices.size(), ("@copy_opt_" + tmpIdxStr + "_val_ref").c_str());
			if (exprType->optionalTarget->referenceLevel() > 0) {
				valRef = LLVMBuildLoad2(_builders.top().get(), co_await translateType(exprType->optionalTarget), valRef, ("@copy_opt_" + tmpIdxStr + "_val_ref_inner").c_str());
			}
			bool didCopy = false;
			auto copied = co_await doCopyCtor(valRef, exprType->optionalTarget->reference(), std::make_pair(true, true), &didCopy);
			if (!didCopy) {
				copied = LLVMBuildLoad2(_builders.top().get(), co_await translateType(exprType->optionalTarget), valRef, ("@copy_opt_" + tmpIdxStr + "_val_deref").c_str());
			}
			std::array<LLVMValueRef, 2> members {
				LLVMConstInt(LLVMInt1TypeInContext(_llcontext.get()), 1, false),
				LLVMGetPoison(co_await translateType(exprType->optionalTarget)),
			};
			auto wrapped = LLVMBuildInsertValue(_builders.top().get(), LLVMConstNamedStruct(lloptionalType, members.data(), members.size()), copied, 1, ("@copy_opt_" + tmpIdxStr + "_copied").c_str());

			LLVMBuildBr(_builders.top().get(), doneBlock);
			auto exitBlock = LLVMGetInsertBlock(_builders.top().get());

			LLVMPositionBuilderAtEnd(_builders.top().get(), doneBlock);

			std::array<LLVMValueRef, 2> nullMembers {
				LLVMConstInt(LLVMInt1TypeInContext(_llcontext.get()), 0, false),
				LLVMGetPoison(co_await translateType(exprType->optionalTarget)),
			};
			auto constNull = LLVMConstNamedStruct(lloptionalType, nullMembers.data(), nullMembers.size());

			result = LLVMBuildPhi(_builders.top().get(), lloptionalType, ("@copy_opt_" + tmpIdxStr).c_str());
			LLVMAddIncoming(result, &wrapped, &exitBlock, 1);
			LLVMAddIncoming(result, &constNull, &entryBlock, 1);

			co_await currentStack().endBranch(doneBlock, { entryBlock, exitBlock });

			didCopyInternal = true;
		} else if (
			// make sure we have a copy constructor,
			// otherwise, there's no point in continuing
			exprType->klass->copyConstructor
		) {
			auto [lltype, llfunc] = co_await declareFunction(exprType->klass->copyConstructor);
			copyFuncType = lltype;
			copyFunc = llfunc;

			didCopyInternal = true;
		}

		if (copyFunc && copyFuncType) {
			std::array<LLVMValueRef, 1> args { result };
			result = LLVMBuildCall2(_builders.top().get(), copyFuncType, copyFunc, args.data(), args.size(), ("@copy_instance_" + tmpIdxStr).c_str());
		}

		if (!didCopyInternal) {
			result = co_await loadRef(result, exprType);
		}
	}

	if (didCopy) {
		*didCopy = didCopyInternal;
	}

	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::doCopyCtor(LLVMValueRef compiled, std::shared_ptr<AltaCore::AST::ExpressionNode> expr, std::shared_ptr<AltaCore::DH::ExpressionNode> info, bool* didCopy) {
	auto exprType = AltaCore::DET::Type::getUnderlyingType(info.get());
	auto nodeType = expr->nodeType();

	if (didCopy) {
		*didCopy = false;
	}

	if (
		// pointers are copied by value
		//
		// note that this does not include references,
		// those do need to be copied
		exprType->pointerLevel() < 1 &&
		// check that the expression isn't a class instantiation
		// or a function call, since:
		//   a) for class insts., we *just* created the class. there's
		//      no need to copy it
		//   b) for function calls, returned expressions are automatically
		//      copied if necessary (via this same method, `doCopyCtor`)
		nodeType != AltaCore::AST::NodeType::ClassInstantiationExpression &&
		nodeType != AltaCore::AST::NodeType::FunctionCallExpression &&
		nodeType != AltaCore::AST::NodeType::AwaitExpression &&
		nodeType != AltaCore::AST::NodeType::YieldExpression
	) {
		compiled = co_await doCopyCtorInternal(compiled, exprType, additionalCopyInfo(expr, info), didCopy);
	}

	co_return compiled;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::doCopyCtor(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType, CopyInfo additionalCopyInfo, bool* didCopy) {
	if (didCopy) {
		*didCopy = false;
	}

	if (
		// pointers are copied by value
		//
		// note that this does not include references,
		// those do need to be copied
		exprType->pointerLevel() < 1
	) {
		expr = co_await doCopyCtorInternal(expr, exprType, additionalCopyInfo, didCopy);
	}

	co_return expr;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::doDtor(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType, bool* didDtor, bool force) {
	auto result = expr;

	if (didDtor) {
		*didDtor = false;
	}

	if (canDestroy(exprType, force)) {
		auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
		auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());
		auto tmpIdx = nextTemp();
		auto tmpIdxStr = std::to_string(tmpIdx);

		if (!exprType->isRawFunction) {
			result = LLVMBuildLoad2(_builders.top().get(), co_await translateType(exprType), result, ("@dtor_fat_func_" + tmpIdxStr + "_load").c_str());
			result = co_await loadRef(result, exprType);

			auto lambdaState = LLVMBuildExtractValue(_builders.top().get(), result, 1, ("@dtor_fat_func_" + tmpIdxStr + "_lambda_state").c_str());
			auto lambdaStateIsNotNull = LLVMBuildIsNotNull(_builders.top().get(), lambdaState, ("@dtor_fat_func_" + tmpIdxStr + "_lambda_state_is_not_null").c_str());

			auto llfunc = LLVMGetBasicBlockParent(LLVMGetInsertBlock(_builders.top().get()));
			auto doDtorBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@dtor_fat_func_" + tmpIdxStr + "_do_lambda_dtor").c_str());
			auto doneBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@dtor_fat_func_" + tmpIdxStr + "_done").c_str());

			LLVMBuildCondBr(_builders.top().get(), lambdaStateIsNotNull, doDtorBlock, doneBlock);

			LLVMPositionBuilderAtEnd(_builders.top().get(), doDtorBlock);

			auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
			auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());
			auto refCountType = LLVMInt64TypeInContext(_llcontext.get());

			std::array<LLVMValueRef, 2> gepIndices {
				LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
				LLVMConstInt(gepStructIndexType, 0, false), // _Alta_basic_lambda_state::reference_count
			};

			auto refCountPtr = LLVMBuildGEP2(_builders.top().get(), _definedTypes["_Alta_basic_lambda_state"], lambdaState, gepIndices.data(), gepIndices.size(), "@dtor_fat_func_refcnt_ptr");

			// UDecWrap is not available in my current version of LLVM (nor in any version of the C API I can currently find)
			//auto old = LLVMBuildAtomicRMW(_builders.top().get(),  LLVMAtomicRMWBinOpUDecWrap, refCountPtr, LLVMConstInt(refCountType, 0, false), LLVMAtomicOrderingRelease, false);
			auto old = LLVMBuildAtomicRMW(_builders.top().get(), LLVMAtomicRMWBinOpSub, refCountPtr, LLVMConstInt(refCountType, 1, false), LLVMAtomicOrderingRelease, false);

			LLVMSetValueName(old, ("@dtor_fat_func_" + tmpIdxStr + "_old_refcnt").c_str());

			auto oldIsOne = LLVMBuildICmp(_builders.top().get(), LLVMIntEQ, old, LLVMConstInt(refCountType, 1, false), ("@dtor_fat_func_" + tmpIdxStr + "_should_cleanup").c_str());

			auto cleanupBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@dtor_fat_func_" + tmpIdxStr + "_cleanup").c_str());

			LLVMBuildCondBr(_builders.top().get(), oldIsOne, cleanupBlock, doneBlock);

			LLVMPositionBuilderAtEnd(_builders.top().get(), cleanupBlock);

			gepIndices[1] = LLVMConstInt(gepStructIndexType, 1, false); // _Alta_basic_lambda_state::destructor
			auto dtorPtr = LLVMBuildGEP2(_builders.top().get(), _definedTypes["_Alta_basic_lambda_state"], lambdaState, gepIndices.data(), gepIndices.size(), "@dtor_fat_func_dtor_ptr");
			auto dtor = LLVMBuildLoad2(_builders.top().get(), LLVMPointerType(_definedTypes["_Alta_lambda_destructor"], 0), dtorPtr, "@dtor_fat_func_dtor");

			std::array<LLVMValueRef, 1> dtorArgs {
				lambdaState,
			};

			LLVMBuildCall2(_builders.top().get(), _definedTypes["_Alta_lambda_destructor"], dtor, dtorArgs.data(), dtorArgs.size(), "");

			LLVMBuildBr(_builders.top().get(), doneBlock);

			LLVMPositionBuilderAtEnd(_builders.top().get(), doneBlock);
		} else if (exprType->isUnion()) {
			result = co_await loadRef(result, exprType);

			co_await forEachUnionMember(result, exprType->destroyReferences()->reference(), NULL, [&](LLVMValueRef memberRef, std::shared_ptr<AltaCore::DET::Type> memberType, size_t memberIndex) -> LLCoroutine {
				return doDtor(memberRef, memberType, nullptr, force);
			});
		} else if (exprType->isOptional) {
			auto loaded = co_await loadRef(LLVMBuildLoad2(_builders.top().get(), co_await translateType(exprType), result, ("@dtor_opt_" + tmpIdxStr + "_deref").c_str()), exprType);
			LLVMValueRef contained = NULL;
			auto containedType = exprType->optionalTarget;

			auto present = LLVMBuildExtractValue(_builders.top().get(), loaded, 0, ("@dtor_opt_" + tmpIdxStr + "_is_present").c_str());

			std::array<LLVMValueRef, 2> gepIndices {
				LLVMConstInt(gepIndexType, 0, false), // the first entry in the "array"
				LLVMConstInt(gepStructIndexType, 1, false), // the contained value
			};

			contained = LLVMBuildGEP2(_builders.top().get(), co_await translateType(exprType->destroyReferences()), result, gepIndices.data(), gepIndices.size(), ("@dtor_opt_" + tmpIdxStr + "_val_ref").c_str());
			//containedType = containedType->reference();

			auto llfunc = LLVMGetBasicBlockParent(LLVMGetInsertBlock(_builders.top().get()));
			auto doDtorBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@dtor_opt_" + tmpIdxStr + "_do_dtor").c_str());
			auto doneBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@dtor_opt_" + tmpIdxStr + "_done").c_str());

			currentStack().beginBranch();

			auto originBlock = LLVMGetInsertBlock(_builders.top().get());
			LLVMBuildCondBr(_builders.top().get(), present, doDtorBlock, doneBlock);

			LLVMPositionBuilderAtEnd(_builders.top().get(), doDtorBlock);
			co_await doDtor(contained, containedType, nullptr, false);
			auto exitDtorBlock = LLVMGetInsertBlock(_builders.top().get());
			LLVMBuildBr(_builders.top().get(), doneBlock);

			LLVMPositionBuilderAtEnd(_builders.top().get(), doneBlock);
			co_await currentStack().endBranch(doneBlock, {
				originBlock,
				exitDtorBlock,
			});

			result = NULL;
		} else {
			auto singleRef = co_await loadRef(result, exprType);

			auto initialBasicClass = LLVMBuildPointerCast(_builders.top().get(), singleRef, LLVMPointerType(_definedTypes["_Alta_basic_class"], 0), ("@dtor_instance_" + tmpIdxStr + "_basic_class").c_str());

			std::array<LLVMValueRef, 3> gepIndices {
				LLVMConstInt(gepIndexType, 0, false), // the first entry in the "array"
				LLVMConstInt(gepStructIndexType, 0, false), // _Alta_basic_class::instance_info
				LLVMConstInt(gepStructIndexType, 0, false), // _Alta_instance_info::class_info
			};

			auto classInfoPtr = LLVMPointerType(_definedTypes["_Alta_class_info"], 0);
			auto classInfoPtrPtr = LLVMPointerType(classInfoPtr, 0);

			auto initialClassInfoPtr = LLVMBuildGEP2(_builders.top().get(), _definedTypes["_Alta_basic_class"], initialBasicClass, gepIndices.data(), gepIndices.size(), ("@dtor_instance_" + tmpIdxStr + "_class_info_ptr_ptr").c_str());
			auto initialClassInfo = LLVMBuildLoad2(_builders.top().get(), classInfoPtr, initialClassInfoPtr, ("@dtor_instance_" + tmpIdxStr + "_class_info_ptr").c_str());

			auto isNotNull = LLVMBuildIsNotNull(_builders.top().get(), initialClassInfo, ("@dtor_instance_" + tmpIdxStr + "_class_info_ptr_is_not_null").c_str());

			auto entryBlock = LLVMGetInsertBlock(_builders.top().get());
			auto llfunc = LLVMGetBasicBlockParent(entryBlock);
			auto notNullBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@dtor_instance_" + tmpIdxStr + "_nonnull_class_info").c_str());
			auto doneBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@dtor_instance_" + tmpIdxStr + "_done").c_str());

			currentStack().beginBranch();

			LLVMBuildCondBr(_builders.top().get(), isNotNull, notNullBlock, doneBlock);

			LLVMPositionBuilderAtEnd(_builders.top().get(), notNullBlock);

			result = co_await getRootInstance(singleRef, exprType->reference());
			result = LLVMBuildPointerCast(_builders.top().get(), result, LLVMPointerType(_definedTypes["_Alta_basic_class"], 0), ("@dtor_instance_" + tmpIdxStr + "_root_basic_class").c_str());

			auto dtor = LLVMBuildGEP2(_builders.top().get(), _definedTypes["_Alta_basic_class"], result, gepIndices.data(), gepIndices.size(), ("@dtor_instance_" + tmpIdxStr + "_root_class_info_ptr_ptr").c_str());
			dtor = LLVMBuildLoad2(_builders.top().get(), classInfoPtr, dtor, ("@dtor_instance_" + tmpIdxStr + "_root_class_info_ptr").c_str());

			std::array<LLVMValueRef, 2> gepIndices2 {
				LLVMConstInt(gepIndexType, 0, false), // the first entry in the "array"
				LLVMConstInt(gepStructIndexType, 1, false), // _Alta_class_info::destructor
			};

			auto dtorType = _definedTypes["_Alta_class_destructor"];
			auto dtorPtr = LLVMPointerType(dtorType, 0);
			auto dtorPtrPtr = LLVMPointerType(dtorPtr, 0);

			dtor = LLVMBuildGEP2(_builders.top().get(), _definedTypes["_Alta_class_info"], dtor, gepIndices2.data(), gepIndices2.size(), ("@dtor_instance_" + tmpIdxStr + "_root_dtor_ptr").c_str());
			dtor = LLVMBuildLoad2(_builders.top().get(), dtorPtr, dtor, ("@dtor_instance_" + tmpIdxStr + "_root_dtor").c_str());

			auto dtorIsNotNull = LLVMBuildIsNotNull(_builders.top().get(), dtor, ("@dtor_instance_" + tmpIdxStr + "_dtor_is_not_null").c_str());

			auto notNullDtorBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@dtor_instance_" + tmpIdxStr + "_nonnull_dtor").c_str());
			auto doneDtorBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@dtor_instance_" + tmpIdxStr + "_dtor_done").c_str());

			currentStack().beginBranch();

			LLVMBuildCondBr(_builders.top().get(), dtorIsNotNull, notNullDtorBlock, doneDtorBlock);

			LLVMPositionBuilderAtEnd(_builders.top().get(), notNullDtorBlock);

			std::array<LLVMValueRef, 1> args {
				LLVMBuildPointerCast(_builders.top().get(), result, LLVMPointerType(LLVMVoidTypeInContext(_llcontext.get()), 0), ("@dtor_instance_" + tmpIdxStr + "_root_instance").c_str()),
			};
			result = LLVMBuildCall2(_builders.top().get(), dtorType, dtor, args.data(), args.size(), "");

			auto exitBlockDtor = LLVMGetInsertBlock(_builders.top().get());
			LLVMBuildBr(_builders.top().get(), doneDtorBlock);

			LLVMPositionBuilderAtEnd(_builders.top().get(), doneDtorBlock);

			co_await currentStack().endBranch(doneDtorBlock, { notNullBlock, exitBlockDtor });
			auto exitBlock = LLVMGetInsertBlock(_builders.top().get());
			LLVMBuildBr(_builders.top().get(), doneBlock);

			LLVMPositionBuilderAtEnd(_builders.top().get(), doneBlock);

			co_await currentStack().endBranch(doneBlock, { entryBlock, exitBlock });
		}

		if (didDtor) {
			*didDtor = true;
		}
	}

	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::loadRef(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType, size_t finalRefLevel) {
	for (size_t i = finalRefLevel; i < exprType->referenceLevel(); ++i) {
		exprType = exprType->dereference();
		expr = LLVMBuildLoad2(_builders.top().get(), co_await translateType(exprType), expr, "");
	}
	co_return expr;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::loadIndirection(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType, size_t finalIndirLevel) {
	while (exprType->indirectionLevel() > finalIndirLevel) {
		exprType = exprType->followBlindly();
		expr = LLVMBuildLoad2(_builders.top().get(), co_await translateType(exprType), expr, "");
	}
	co_return expr;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::getRealInstance(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType) {
	auto result = expr;

	if (
		!exprType->isNative &&
		!exprType->isUnion() &&
		exprType->klass &&
		!(
			exprType->klass->isStructure ||
			exprType->klass->isBitfield
		)
	) {
		auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
		auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());
		auto lltype = co_await translateType(exprType);
		auto lltypeRef = co_await translateType(exprType->destroyReferences()->reference());
		auto tmpIdx = nextTemp();
		auto tmpIdxStr = std::to_string(tmpIdx);

		// it should be impossible to get the real instance of a raw class instance
		assert(exprType->indirectionLevel() != 0);

		if (exprType->indirectionLevel() > 1) {
			result = co_await loadIndirection(result, exprType, 1);
			exprType = exprType->destroyIndirection()->reference();
		}

		std::array<LLVMValueRef, 3> accessGEP {
			LLVMConstInt(gepIndexType, 0, false), // first element in "array"
			LLVMConstInt(gepStructIndexType, 0, false), // instance_info member within class structure
			LLVMConstInt(gepStructIndexType, 0, false), // class_info pointer within instance info
		};
		auto classInfoPtrType = LLVMPointerType(_definedTypes["_Alta_class_info"], 0);
		auto tmp = LLVMBuildGEP2(_builders.top().get(), co_await defineClassType(exprType->klass), result, accessGEP.data(), accessGEP.size(), ("@real_inst_" + tmpIdxStr + "_class_info_ptr_ptr").c_str());
		auto classInfoPointer = LLVMBuildLoad2(_builders.top().get(), classInfoPtrType, tmp, ("@real_inst_" + tmpIdxStr + "_class_info_ptr").c_str());

		std::array<LLVMValueRef, 2> offsetGEP {
			LLVMConstInt(gepIndexType, 0, false), // first element in "array"
			LLVMConstInt(gepStructIndexType, 3, false), // offset_from_real member within class_info
		};
		auto i64Type = LLVMInt64TypeInContext(_llcontext.get());
		auto offsetFromRealPointer = LLVMBuildGEP2(_builders.top().get(), _definedTypes["_Alta_class_info"], classInfoPointer, offsetGEP.data(), offsetGEP.size(), ("@real_inst_" + tmpIdxStr + "_offset_from_real_ptr").c_str());
		auto offsetFromReal = LLVMBuildLoad2(_builders.top().get(), i64Type, offsetFromRealPointer, ("@real_inst_" + tmpIdxStr + "_offset_from_real").c_str());

		auto classPointerAsInt = LLVMBuildPtrToInt(_builders.top().get(), result, i64Type, ("@real_inst_" + tmpIdxStr + "_addr_start").c_str());
		auto subtracted = LLVMBuildSub(_builders.top().get(), classPointerAsInt, offsetFromReal, ("@real_inst_" + tmpIdxStr + "_addr_subbed").c_str());
		result = LLVMBuildIntToPtr(_builders.top().get(), subtracted, lltypeRef, ("@real_inst_" + tmpIdxStr + "_ref").c_str());

		if (exprType->indirectionLevel() == 0) {
			result = LLVMBuildLoad2(_builders.top().get(), lltype, result, ("@real_inst_" + tmpIdxStr).c_str());
		}
	}

	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::getRootInstance(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType) {
	auto result = expr;

	if (
		!exprType->isNative &&
		!exprType->isUnion() &&
		exprType->klass &&
		!(
			exprType->klass->isStructure ||
			exprType->klass->isBitfield
		)
	) {
		auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
		auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());
		auto tmpIdx = nextTemp();
		auto tmpIdxStr = std::to_string(tmpIdx);

		// it should be impossible to get the root instance of a raw class instance
		assert(exprType->indirectionLevel() != 0);

		if (exprType->indirectionLevel() > 1) {
			result = co_await loadIndirection(result, exprType, 1);
			exprType = exprType->destroyIndirection()->reference();
		}

		std::array<LLVMValueRef, 3> accessGEP {
			LLVMConstInt(gepIndexType, 0, false), // first element in "array"
			LLVMConstInt(gepStructIndexType, 0, false), // instance_info member within class structure
			LLVMConstInt(gepStructIndexType, 0, false), // class_info pointer within instance info
		};
		auto classInfoPtrType = LLVMPointerType(_definedTypes["_Alta_class_info"], 0);
		auto tmp = LLVMBuildGEP2(_builders.top().get(), co_await defineClassType(exprType->klass), result, accessGEP.data(), accessGEP.size(), ("@root_inst_" + tmpIdxStr + "_class_info_ptr_ptr").c_str());
		auto classInfoPointer = LLVMBuildLoad2(_builders.top().get(), classInfoPtrType, tmp, ("@root_inst_" + tmpIdxStr + "_class_info_ptr").c_str());

		std::array<LLVMValueRef, 2> offsetGEP {
			LLVMConstInt(gepIndexType, 0, false), // first element in "array"
			LLVMConstInt(gepStructIndexType, 4, false), // offset_from_base member within class_info
		};
		auto i64Type = LLVMInt64TypeInContext(_llcontext.get());
		auto offsetFromBasePointer = LLVMBuildGEP2(_builders.top().get(), _definedTypes["_Alta_class_info"], classInfoPointer, offsetGEP.data(), offsetGEP.size(), ("@root_inst_" + tmpIdxStr + "_offset_from_base_ptr").c_str());
		auto offsetFromBase = LLVMBuildLoad2(_builders.top().get(), i64Type, offsetFromBasePointer, ("@root_inst_" + tmpIdxStr + "_offset_from_base").c_str());

		auto classPointerAsInt = LLVMBuildPtrToInt(_builders.top().get(), result, i64Type, ("@root_inst_" + tmpIdxStr + "_addr_start").c_str());
		auto subtracted = LLVMBuildSub(_builders.top().get(), classPointerAsInt, offsetFromBase, ("@root_inst_" + tmpIdxStr + "_addr_subbed").c_str());
		result = LLVMBuildIntToPtr(_builders.top().get(), subtracted, LLVMPointerType(LLVMVoidTypeInContext(_llcontext.get()), 0), ("@root_inst_" + tmpIdxStr + "_ref").c_str());
	}

	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::doParentRetrieval(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType, std::shared_ptr<AltaCore::DET::Type> targetType, bool* didRetrieval) {
	auto result = expr;

	if (didRetrieval != nullptr) {
		*didRetrieval = false;
	}

	if (
		!exprType->isNative &&
		!exprType->isUnion() &&
		!targetType->isNative &&
		!targetType->isUnion() &&
		exprType->klass &&
		targetType->klass &&
		exprType->klass->id != targetType->klass->id &&
		exprType->klass->hasParent(targetType->klass)
	) {
		std::vector<std::shared_ptr<AltaCore::DET::Class>> parentAccessors;
		std::vector<LLVMValueRef> gepIndices;
		std::stack<size_t> idxs;
		auto tmpIdx = nextTemp();
		auto tmpIdxStr = std::to_string(tmpIdx);

		auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
		auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());

		parentAccessors.push_back(exprType->klass);
		gepIndices.push_back(LLVMConstInt(gepIndexType, 0, false));
		idxs.push(0);

		while (parentAccessors.size() > 0) {
			auto& parents = parentAccessors.back()->parents;
			bool cont = false;
			bool done = false;
			for (size_t i = idxs.top(); i < parents.size(); i++) {
				auto& parent = parents[i];
				parentAccessors.push_back(parent);
				gepIndices.push_back(LLVMConstInt(gepStructIndexType, i + 1, false));
				if (parent->id == targetType->klass->id) {
					done = true;
					break;
				} else if (parent->parents.size() > 0) {
					cont = true;
					idxs.push(0);
					break;
				} else {
					parentAccessors.pop_back();
					gepIndices.pop_back();
				}
			}
			if (done) break;
			if (cont) continue;
			parentAccessors.pop_back();
			gepIndices.pop_back();
			idxs.pop();
			if (idxs.size() > 0) {
				++idxs.top();
			}
		}

		if (exprType->indirectionLevel() == 0) {
			result = co_await tmpify(result, exprType);
		} else if (exprType->indirectionLevel() > 1) {
			result = co_await loadIndirection(result, exprType, 1);
			exprType = exprType->destroyIndirection()->reference();
		}

		auto refTargetType = targetType->indirectionLevel() > 0 ? targetType : targetType->reference();
		auto lltype = co_await translateType(refTargetType);
		result = LLVMBuildGEP2(_builders.top().get(), co_await defineClassType(exprType->klass), result, gepIndices.data(), gepIndices.size(), ("@upcast_" + tmpIdxStr + "_gep").c_str());
		result = co_await getRealInstance(result, refTargetType);

		if (targetType->indirectionLevel() == 0) {
			result = LLVMBuildLoad2(_builders.top().get(), co_await translateType(targetType), result, ("@upcast_" + tmpIdxStr).c_str());
		}

		if (didRetrieval != nullptr) {
			*didRetrieval = true;
		}
	}

	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::doChildRetrieval(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType, std::shared_ptr<AltaCore::DET::Type> targetType, bool* didRetrieval) {
	auto result = expr;

	if (didRetrieval != nullptr) {
		*didRetrieval = false;
	}

	if (
		!exprType->isNative &&
		!exprType->isUnion() &&
		!targetType->isNative &&
		!targetType->isUnion() &&
		exprType->klass &&
		targetType->klass &&
		exprType->klass->id != targetType->klass->id &&
		targetType->klass->hasParent(exprType->klass) &&
		exprType->indirectionLevel() > 0
	) {
		std::vector<std::shared_ptr<AltaCore::DET::Class>> parentAccessors;
		std::stack<size_t> idxs;
		auto tmpIdx = nextTemp();
		auto tmpIdxStr = std::to_string(tmpIdx);

		parentAccessors.push_back(targetType->klass);
		idxs.push(0);

		while (parentAccessors.size() > 0) {
			auto& parents = parentAccessors.back()->parents;
			bool cont = false;
			bool done = false;
			for (size_t i = idxs.top(); i < parents.size(); i++) {
				auto& parent = parents[i];
				parentAccessors.push_back(parent);
				if (parent->id == exprType->klass->id) {
					done = true;
					break;
				} else if (parent->parents.size() > 0) {
					cont = true;
					idxs.push(0);
					break;
				} else {
					parentAccessors.pop_back();
				}
			}
			if (done) break;
			if (cont) continue;
			parentAccessors.pop_back();
			idxs.pop();
			if (idxs.size() > 0) {
				++idxs.top();
			}
		}

		// remove the last component (that's the current parent)
		parentAccessors.pop_back();

		// reverse the components so that the first component is the first class we need to expand up through
		std::reverse(parentAccessors.begin(), parentAccessors.end());

		std::vector<std::string> names;
		for (const auto& parentAccessor: parentAccessors) {
			names.push_back(mangleName(parentAccessor));
		}

		if (exprType->indirectionLevel() > 1) {
			result = co_await loadIndirection(result, exprType, 1);
			exprType = exprType->destroyIndirection()->reference();
		}

		result = buildGetChild(_builders.top(), result, names);
		LLVMSetValueName(result, ("@downcast_" + tmpIdxStr + "_ref").c_str());

		if (targetType->referenceLevel() > 0) {
			// references cannot be checked for nullity in user code, so we must check them here
			auto llfunc = LLVMGetBasicBlockParent(LLVMGetInsertBlock(_builders.top().get()));
			auto isNull = LLVMBuildIsNull(_builders.top().get(), result, ("@downcast_" + tmpIdxStr + "_ref_is_null").c_str());
			auto isNullBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@downcast_" + tmpIdxStr + "_is_null").c_str());
			auto isNotNullBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@downcast_" + tmpIdxStr + "_is_not_null").c_str());

			LLVMBuildCondBr(_builders.top().get(), isNull, isNullBlock, isNotNullBlock);

			LLVMPositionBuilderAtEnd(_builders.top().get(), isNullBlock);

			buildBadCast(_builders.top(), mangleName(exprType->klass), mangleName(targetType->klass));

			LLVMPositionBuilderAtEnd(_builders.top().get(), isNotNullBlock);
		}

		if (didRetrieval != nullptr) {
			*didRetrieval = true;
		}
	}

	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::tmpify(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> type, bool withStack, bool tmpifyRef) {
	auto result = expr;

	if (type->referenceLevel() == 0 || tmpifyRef) {
		auto adjustedType = type->deconstify();
		auto lltype = co_await translateType(adjustedType);

		auto var = currentStack().buildAlloca(lltype);

		if (withStack && canPush(type) && !_stacks.empty()) {
			currentStack().pushItem(var, adjustedType);
		}

		LLVMBuildStore(_builders.top().get(), result, var);

		result = var;
	}

	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::tmpify(std::shared_ptr<AltaCore::AST::ExpressionNode> expr, std::shared_ptr<AltaCore::DH::ExpressionNode> info) {
	auto result = co_await compileNode(expr, info);
	auto type = AltaCore::DET::Type::getUnderlyingType(info.get());

	if (
		!type->isNative &&
		type->indirectionLevel() == 0 &&
		(
			expr->nodeType() == AltaCore::AST::NodeType::FunctionCallExpression ||
			expr->nodeType() == AltaCore::AST::NodeType::ClassInstantiationExpression ||
			expr->nodeType() == AltaCore::AST::NodeType::ConditionalExpression ||
			expr->nodeType() == AltaCore::AST::NodeType::YieldExpression ||
			expr->nodeType() == AltaCore::AST::NodeType::AwaitExpression
		)
	) {
		result = co_await tmpify(result, type, true);
	}

	co_return result;
};

AltaLL::Compiler::Coroutine<void> AltaLL::Compiler::processArgs(std::vector<ALTACORE_VARIANT<std::pair<std::shared_ptr<AltaCore::AST::ExpressionNode>, std::shared_ptr<AltaCore::DH::ExpressionNode>>, std::vector<std::pair<std::shared_ptr<AltaCore::AST::ExpressionNode>, std::shared_ptr<AltaCore::DH::ExpressionNode>>>>> adjustedArguments, std::vector<std::tuple<std::string, std::shared_ptr<AltaCore::DET::Type>, bool, std::string>> parameters, AltaCore::Errors::Position* position, std::vector<LLVMValueRef>& outputArgs) {
	size_t firstOutputIdx = outputArgs.size();

	std::vector<bool> stored;

	for (size_t i = 0; i < adjustedArguments.size(); ++i) {
		auto& arg = adjustedArguments[i];
		auto [name, targetType, isVariable, id] = parameters[i];

		if (auto solo = ALTACORE_VARIANT_GET_IF<std::pair<std::shared_ptr<AltaCore::AST::ExpressionNode>, std::shared_ptr<AltaCore::DH::ExpressionNode>>>(&arg)) {
			auto& [arg, info] = *solo;
			auto compiled = co_await compileNode(arg, info);
			auto exprType = AltaCore::DET::Type::getUnderlyingType(info.get());
			auto result = co_await cast(compiled, exprType, targetType, true, additionalCopyInfo(arg, info), false, position);
			if (inSuspendableFunction() && !LLVMIsConstant(result)) {
				// we might have a state change while evaluating other arguments, so let's save the value for this argument in a temporary alloca
				auto alloca = currentStack().buildAlloca(co_await translateType(targetType));
				LLVMBuildStore(_builders.top().get(), result, alloca);
				result = alloca;
				stored.push_back(true);
			} else {
				stored.push_back(false);
			}
			outputArgs.push_back(result);
		} else if (auto multi = ALTACORE_VARIANT_GET_IF<std::vector<std::pair<std::shared_ptr<AltaCore::AST::ExpressionNode>, std::shared_ptr<AltaCore::DH::ExpressionNode>>>>(&arg)) {
			if (varargTable[id]) {
				for (auto& [arg, info]: *multi) {
					auto compiled = co_await compileNode(arg, info);
					auto exprType = AltaCore::DET::Type::getUnderlyingType(info.get());
					auto result = co_await cast(compiled, exprType, targetType, true, additionalCopyInfo(arg, info), false, position);
					if (inSuspendableFunction() && !LLVMIsConstant(result)) {
						// we might have a state change while evaluating other arguments, so let's save the value for this argument in a temporary alloca
						auto alloca = currentStack().buildAlloca(co_await translateType(targetType));
						LLVMBuildStore(_builders.top().get(), result, alloca);
						result = alloca;
						stored.push_back(true);
					} else {
						stored.push_back(false);
					}
					outputArgs.push_back(result);
				}
			} else {
				auto tmpIdx = nextTemp();
				auto tmpIdxStr = std::to_string(tmpIdx);

				auto i64Type = LLVMInt64TypeInContext(_llcontext.get());

				auto lltype = co_await translateType(targetType);
				auto llcount = LLVMConstInt(i64Type, multi->size(), false);
				auto result = currentStack().buildArrayAlloca(lltype, multi->size(), "@arg_array_" + tmpIdxStr);

				size_t argIdx = 0;
				for (auto& [arg, info]: *multi) {
					auto compiled = co_await compileNode(arg, info);
					auto exprType = AltaCore::DET::Type::getUnderlyingType(info.get());
					auto argResult = co_await cast(compiled, exprType, targetType, true, additionalCopyInfo(arg, info), false, position);
					std::array<LLVMValueRef, 1> gepIndices {
						LLVMConstInt(i64Type, argIdx, false),
					};
					auto gep = LLVMBuildGEP2(_builders.top().get(), lltype, result, gepIndices.data(), gepIndices.size(), ("@arg_array_" + tmpIdxStr + "_elm_" + std::to_string(argIdx)).c_str());
					LLVMBuildStore(_builders.top().get(), argResult, gep);
					++argIdx;
				}

				// unlike other argument types, we don't have to worry about having a state change.
				// we know for a fact that we stored the array on a suspendable stack and the count is a constant,
				// so we don't have to save and restore anything.

				outputArgs.push_back(llcount);
				outputArgs.push_back(result);

				stored.push_back(false);
				stored.push_back(false);
			}
		}
	}

	if (inSuspendableFunction()) {
		size_t outputArgIdx = firstOutputIdx;
		for (size_t i = 0; i < adjustedArguments.size(); ++i) {
			auto& arg = adjustedArguments[i];
			auto [name, targetType, isVariable, id] = parameters[i];

			size_t argCount = 1;

			if (auto multi = ALTACORE_VARIANT_GET_IF<std::vector<std::pair<std::shared_ptr<AltaCore::AST::ExpressionNode>, std::shared_ptr<AltaCore::DH::ExpressionNode>>>>(&arg)) {
				if (!varargTable[id]) {
					// as noted above, we don't have to worry about saving and restoring arguments for standard variable-length parameters in suspendable functions.
					++outputArgIdx;
					++outputArgIdx;
					continue;
				}

				argCount = multi->size();
			}

			for (size_t i = 0; i < argCount; ++i) {
				if (stored[outputArgIdx - firstOutputIdx]) {
					outputArgs[outputArgIdx] = LLVMBuildLoad2(_builders.top().get(), co_await translateType(targetType), outputArgs[outputArgIdx], "");
				}
				++outputArgIdx;
			}
		}
	}

	co_return;
};

AltaLL::Compiler::Coroutine<LLVMTypeRef> AltaLL::Compiler::defineClassType(std::shared_ptr<AltaCore::DET::Class> klass) {
	if (_definedTypes[klass->id]) {
		co_return _definedTypes[klass->id];
	}

	auto mangled = mangleName(klass);

	_definedTypes[klass->id] = LLVMStructCreateNamed(_llcontext.get(), mangled.c_str());

	std::vector<LLVMTypeRef> members;

	bool isGenerator = klass->name == "@Generator@";
	bool isCoroutine = klass->name == "@Coroutine@";
	bool suspendable = isGenerator || isCoroutine;

	if (!klass->isStructure && !klass->isBitfield && !suspendable) {
		// every class requires an instance info structure
		members.push_back(_definedTypes["_Alta_instance_info"]);

		// the parents are included as members from the start of the structure
		for (const auto& parent: klass->parents) {
			members.push_back(co_await defineClassType(parent));
		}
	}

	if (suspendable) {
		// suspendable contexts require suspendable context info
		members.push_back(_definedTypes[isCoroutine ? "_Alta_basic_coroutine" : "_Alta_basic_generator"]);

		// suspendable functions might require an input and output variable

		if (klass->suspendableInput) {
			members.push_back(co_await translateType(klass->suspendableInput->makeOptional()));
		}

		if (klass->suspendableOutput) {
			members.push_back(co_await translateType(klass->suspendableOutput->makeOptional()));
		}
	}

	for (const auto& member: klass->members) {
		members.push_back(co_await translateType(member->type));
	}

	LLVMStructSetBody(_definedTypes[klass->id], members.data(), members.size(), false);

	co_return _definedTypes[klass->id];
};

AltaLL::Compiler::Coroutine<std::pair<LLVMTypeRef, LLVMValueRef>> AltaLL::Compiler::declareFunction(std::shared_ptr<AltaCore::DET::Function> function, LLVMTypeRef llfunctionType) {
	LLVMTypeRef llfuncType = llfunctionType;
	LLVMValueRef llfunc = NULL;
	auto altaFuncType = AltaCore::DET::Type::getUnderlyingType(function);

	if (function->isConstructor) {
		altaFuncType->returnType = function->parentClassType->destroyReferences();
	}

	if (function->isLambda) {
		altaFuncType->isRawFunction = true;
		altaFuncType->parameters.insert(altaFuncType->parameters.begin(), { "@lambda_state", std::make_shared<AltaCore::DET::Type>(AltaCore::DET::NativeType::Void)->point(), false, "" });
	}

	if (!llfuncType) {
		llfuncType = co_await translateType(altaFuncType, false);
	}

	auto funcID = function->isLiteral ? function->name : function->id;

	llfunc = _definedFunctions[funcID];
	if (!llfunc) {
		auto mangled = mangleName(function);
		llfunc = LLVMAddFunction(_llmod.get(), mangled.c_str(), llfuncType);
		_definedFunctions[funcID] = llfunc;
	}

	co_return std::make_pair(llfuncType, llfunc);
};

LLVMValueRef AltaLL::Compiler::enterInitFunction() {
	if (!_initFunction) {
		auto opaquePtr = LLVMPointerTypeInContext(_llcontext.get(), 0);
		auto i32Type = LLVMInt32TypeInContext(_llcontext.get());

		if (!_definedTypes["@llvm.global_ctors.entry"]) {
			std::array<LLVMTypeRef, 3> members {
				i32Type,
				opaquePtr,
				opaquePtr,
			};
			_definedTypes["@llvm.global_ctors.entry"] = LLVMStructTypeInContext(_llcontext.get(), members.data(), members.size(), false);
		}

		if (!_definedTypes["@llvm.global_ctor"]) {
			_definedTypes["@llvm.global_ctor"] = LLVMFunctionType(LLVMVoidTypeInContext(_llcontext.get()), nullptr, 0, false);
		}

		_initFunction = LLVMAddFunction(_llmod.get(), "_Alta_module_constructor", _definedTypes["@llvm.global_ctor"]);
		_definedFunctions["_Alta_module_constructor"] = _initFunction;

		auto globalCtors = LLVMAddGlobal(_llmod.get(), LLVMArrayType(_definedTypes["@llvm.global_ctors.entry"], 1), "llvm.global_ctors");
		LLVMSetLinkage(globalCtors, LLVMAppendingLinkage);

		std::array<LLVMValueRef, 3> globalCtorsEntry {
			LLVMConstInt(i32Type, 65535, false),
			_initFunction,
			LLVMConstNull(opaquePtr),
		};

		std::array<LLVMValueRef, 1> globalCtorsEntries {
			LLVMConstNamedStruct(_definedTypes["@llvm.global_ctors.entry"], globalCtorsEntry.data(), globalCtorsEntry.size()),
		};

		LLVMSetInitializer(globalCtors, LLVMConstArray(_definedTypes["@llvm.global_ctors.entry"], globalCtorsEntries.data(), globalCtorsEntries.size()));
	}

	if (!_initFunctionBuilder) {
		auto entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), _initFunction, "@entry");
		_initFunctionBuilder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
		LLVMPositionBuilderAtEnd(_initFunctionBuilder.get(), entryBlock);
	}

	_builders.push(_initFunctionBuilder);

	if (_initFunctionScopeStack) {
		_stacks.push_back(std::move(*_initFunctionScopeStack));
		_initFunctionScopeStack = ALTACORE_NULLOPT;
	} else {
		pushStack(ScopeStack::Type::Function);
	}

	return _initFunction;
};

void AltaLL::Compiler::exitInitFunction() {
	_builders.pop();
	_initFunctionScopeStack = std::move(_stacks.back());
	_stacks.pop_back();
};

LLVMValueRef AltaLL::Compiler::enterDeinitFunction() {
	if (!_deinitFunction) {
		auto opaquePtr = LLVMPointerTypeInContext(_llcontext.get(), 0);
		auto i32Type = LLVMInt32TypeInContext(_llcontext.get());

		if (!_definedTypes["@llvm.global_dtors.entry"]) {
			std::array<LLVMTypeRef, 3> members {
				i32Type,
				opaquePtr,
				opaquePtr,
			};
			_definedTypes["@llvm.global_dtors.entry"] = LLVMStructTypeInContext(_llcontext.get(), members.data(), members.size(), false);
		}

		if (!_definedTypes["@llvm.global_dtor"]) {
			_definedTypes["@llvm.global_dtor"] = LLVMFunctionType(LLVMVoidTypeInContext(_llcontext.get()), nullptr, 0, false);
		}

		_deinitFunction = LLVMAddFunction(_llmod.get(), "_Alta_module_destructor", _definedTypes["@llvm.global_dtor"]);
		_definedFunctions["_Alta_module_destructor"] = _deinitFunction;

		auto globalDtors = LLVMAddGlobal(_llmod.get(), LLVMArrayType(_definedTypes["@llvm.global_dtors.entry"], 1), "llvm.global_dtors");
		LLVMSetLinkage(globalDtors, LLVMAppendingLinkage);

		std::array<LLVMValueRef, 3> globalDtorsEntry {
			LLVMConstInt(i32Type, 65535, false),
			_deinitFunction,
			LLVMConstNull(opaquePtr),
		};

		std::array<LLVMValueRef, 1> globalDtorsEntries {
			LLVMConstNamedStruct(_definedTypes["@llvm.global_dtors.entry"], globalDtorsEntry.data(), globalDtorsEntry.size()),
		};

		LLVMSetInitializer(globalDtors, LLVMConstArray(_definedTypes["@llvm.global_dtors.entry"], globalDtorsEntries.data(), globalDtorsEntries.size()));
	}

	if (!_deinitFunctionBuilder) {
		auto entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), _deinitFunction, "@entry");
		_deinitFunctionBuilder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
		LLVMPositionBuilderAtEnd(_deinitFunctionBuilder.get(), entryBlock);
	}

	_builders.push(_deinitFunctionBuilder);

	if (_deinitFunctionScopeStack) {
		_stacks.push_back(std::move(*_deinitFunctionScopeStack));
		_deinitFunctionScopeStack = ALTACORE_NULLOPT;
	} else {
		pushStack(ScopeStack::Type::Function);
	}

	return _deinitFunction;
};

void AltaLL::Compiler::exitDeinitFunction() {
	_builders.pop();
	_deinitFunctionScopeStack = std::move(_stacks.back());
	_stacks.pop_back();
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::forEachUnionMember(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> type, LLVMTypeRef returnValueType, std::function<LLCoroutine(LLVMValueRef memberRef, std::shared_ptr<AltaCore::DET::Type> memberType, size_t memberIndex)> callback) {
	LLVMValueRef retVal = NULL;
	auto tmpIdx = nextTemp();
	auto tmpIdxStr = std::to_string(tmpIdx);

	auto i64Type = LLVMInt64TypeInContext(_llcontext.get());

	auto parentFunc = LLVMGetBasicBlockParent(LLVMGetInsertBlock(_builders.top().get()));
	std::vector<LLVMBasicBlockRef> phiBlocks;
	std::vector<LLVMValueRef> phiVals;

	currentStack().beginBranch();

	// first, add the default (bad enum) block
	auto badEnumBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), parentFunc, ("@foreach_union_member_" + tmpIdxStr + "_bad_tag").c_str());

	// now start creating the switch
	auto val = LLVMBuildExtractValue(_builders.top().get(), co_await loadRef(expr, type), 0, ("@foreach_union_member_" + tmpIdxStr + "_tag").c_str());
	auto tmpified = co_await tmpify(expr, type, false);
	auto switchInstr = LLVMBuildSwitch(_builders.top().get(), val, badEnumBlock, type->unionOf.size());

	// now move the builder to the bad enum block and build it
	LLVMPositionBuilderAtEnd(_builders.top().get(), badEnumBlock);
	buildBadEnum(_builders.top(), type->toString(), val);

	// now create the block we go to after we're done here
	auto doneBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), parentFunc, ("@foreach_union_member_" + tmpIdxStr + "_done").c_str());

	auto idxType = LLVMIntTypeInContext(_llcontext.get(), std::bit_width(type->unionOf.size() - 1));
	auto gepIndexType = i64Type;
	auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());

	for (size_t i = 0; i < type->unionOf.size(); ++i) {
		auto& unionMember = type->unionOf[i];
		auto thisBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), parentFunc, ("@foreach_union_member_" + tmpIdxStr + "_member_" + std::to_string(i)).c_str());
		LLVMAddCase(switchInstr, LLVMConstInt(idxType, i, false), thisBlock);
		LLVMPositionBuilderAtEnd(_builders.top().get(), thisBlock);

		std::array<LLVMTypeRef, 2> members;
		members[0] = idxType;
		members[1] = co_await translateType(unionMember);
		auto llactual = LLVMStructTypeInContext(_llcontext.get(), members.data(), members.size(), false);

		auto castedUnion = LLVMBuildBitCast(_builders.top().get(), tmpified, LLVMPointerType(llactual, 0), ("@foreach_union_member_" + tmpIdxStr + "_member_" + std::to_string(i) + "_cast").c_str());

		std::array<LLVMValueRef, 2> gepIndices {
			LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
			LLVMConstInt(gepStructIndexType, 1, false), // the union member
		};
		auto memberRef = LLVMBuildGEP2(_builders.top().get(), llactual, castedUnion, gepIndices.data(), gepIndices.size(), ("@foreach_union_member_" + tmpIdxStr + "_member_" + std::to_string(i) + "_ref").c_str());

		auto currVal = co_await callback(memberRef, unionMember, i);

		phiBlocks.push_back(LLVMGetInsertBlock(_builders.top().get()));

		if (returnValueType) {
			if (currVal) {
				phiVals.push_back(currVal);
			} else {
				phiVals.push_back(LLVMGetPoison(returnValueType));
			}
		}

		LLVMBuildBr(_builders.top().get(), doneBlock);
	}

	// now finish building the "done" block
	LLVMPositionBuilderAtEnd(_builders.top().get(), doneBlock);

	if (returnValueType) {
		retVal = LLVMBuildPhi(_builders.top().get(), returnValueType, ("@foreach_union_member_" + tmpIdxStr + "_result").c_str());
		LLVMAddIncoming(retVal, phiVals.data(), phiBlocks.data(), phiVals.size());
	}

	co_await currentStack().endBranch(doneBlock, phiBlocks);

	co_return retVal;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::returnNull() {
	co_return NULL;
};

LLVMMetadataRef AltaLL::Compiler::translateClassDebug(std::shared_ptr<AltaCore::DET::Class> klass) {
	if (_definedDebugTypes[klass->id]) {
		return _definedDebugTypes[klass->id];
	}

	auto co = defineClassType(klass);
	co.coroutine.resume();
	auto lltype = co.await_resume();

	auto mangled = mangleName(klass);

	auto tmpDebug = _definedDebugTypes[klass->id] = LLVMDIBuilderCreateReplaceableCompositeType(_debugBuilder.get(), _compositeCounter++, klass->name.c_str(), klass->name.size(), translateParentScope(klass), debugFileForScopeItem(klass), klass->position.line, 0, LLVMStoreSizeOfType(_targetData.get(), lltype) * 8, LLVMABIAlignmentOfType(_targetData.get(), lltype)  * 8, LLVMDIFlagZero, "", 0);

	std::vector<LLVMMetadataRef> members;

	bool isGenerator = klass->name == "@Generator@";
	bool isCoroutine = klass->name == "@Coroutine@";
	bool suspendable = isGenerator || isCoroutine;

	if (!klass->isStructure && !klass->isBitfield && !suspendable) {
		// every class requires an instance info structure
		auto debugInstanceInfo = _definedDebugTypes["_Alta_instance_info"];
		members.push_back(LLVMDIBuilderCreateMemberType(_debugBuilder.get(), tmpDebug, "__instance_info", sizeof("__instance_info") - 1, debugFileForScope(klass->scope), klass->position.line, LLVMDITypeGetSizeInBits(debugInstanceInfo), LLVMDITypeGetAlignInBits(debugInstanceInfo), LLVMOffsetOfElement(_targetData.get(), lltype, members.size()) * 8, LLVMDIFlagProtected, debugInstanceInfo));

		// the parents are included as members from the start of the structure
		for (const auto& parent: klass->parents) {
			auto parentType = translateClassDebug(parent);
			members.push_back(LLVMDIBuilderCreateInheritance(_debugBuilder.get(), tmpDebug, parentType, LLVMOffsetOfElement(_targetData.get(), lltype, members.size()) * 8, 0, LLVMDIFlagPublic));
		}
	}

	if (suspendable) {
		// suspendable contexts require suspendable context info
		auto basicSuspendable = _definedDebugTypes[isCoroutine ? "_Alta_basic_coroutine" : "_Alta_basic_generator"];
		members.push_back(LLVMDIBuilderCreateMemberType(_debugBuilder.get(), tmpDebug, "base", sizeof("base") - 1, debugFileForScope(klass->scope), klass->position.line, LLVMDITypeGetSizeInBits(basicSuspendable), LLVMDITypeGetAlignInBits(basicSuspendable), LLVMOffsetOfElement(_targetData.get(), lltype, members.size()) * 8, LLVMDIFlagPublic, basicSuspendable));

		// suspendable functions might require an input and output variable

		if (klass->suspendableInput) {
			auto debugType = translateTypeDebug(klass->suspendableInput->makeOptional());
			members.push_back(LLVMDIBuilderCreateMemberType(_debugBuilder.get(), tmpDebug, "suspendable_input", sizeof("suspendable_input") - 1, debugFileForScope(klass->scope), klass->position.line, LLVMDITypeGetSizeInBits(debugType), LLVMDITypeGetAlignInBits(debugType), LLVMOffsetOfElement(_targetData.get(), lltype, members.size()) * 8, LLVMDIFlagPublic, debugType));
		}

		if (klass->suspendableOutput) {
			auto debugType = translateTypeDebug(klass->suspendableOutput->makeOptional());
			members.push_back(LLVMDIBuilderCreateMemberType(_debugBuilder.get(), tmpDebug, "suspendable_output", sizeof("suspendable_output") - 1, debugFileForScope(klass->scope), klass->position.line, LLVMDITypeGetSizeInBits(debugType), LLVMDITypeGetAlignInBits(debugType), LLVMOffsetOfElement(_targetData.get(), lltype, members.size()) * 8, LLVMDIFlagPublic, debugType));
		}
	}

	for (const auto& member: klass->members) {
		auto memberType = translateTypeDebug(member->type);
		uint64_t flags = LLVMDIFlagZero;

		switch (member->visibility) {
			case AltaCore::Shared::Visibility::Public:
				flags |= LLVMDIFlagPublic;
			case AltaCore::Shared::Visibility::Protected:
				flags |= LLVMDIFlagProtected;
				break;
			case AltaCore::Shared::Visibility::Private:
				flags |= LLVMDIFlagPrivate;
				break;

			// there are no corresponding visibility attributes for LLVM, so just use `public` to avoid preventing valid accesses within the module/package
			case AltaCore::Shared::Visibility::Module:
				flags |= LLVMDIFlagPublic;
				break;
			case AltaCore::Shared::Visibility::Package:
				flags |= LLVMDIFlagPublic;
				break;
		}

		members.push_back(LLVMDIBuilderCreateMemberType(_debugBuilder.get(), tmpDebug, member->name.c_str(), member->name.size(), debugFileForScopeItem(member), member->position.line, LLVMDITypeGetSizeInBits(memberType), LLVMDITypeGetAlignInBits(memberType), LLVMOffsetOfElement(_targetData.get(), lltype, members.size()) * 8, static_cast<LLVMDIFlags>(flags), memberType));
	}

#if 0
	for (const auto& item: klass->scope->items) {
		if (item->nodeType() != AltaCore::DET::NodeType::Function) continue;
		auto func = std::dynamic_pointer_cast<AltaCore::DET::Function>(item);

		// `asDefinition = true` because we know that all methods within classes *must* be defined (we only support declarations for free functions)
		if (func->genericParameterCount > 0) {
			if (auto ast = func->ast.lock()) {
				if (auto info = func->info.lock()) {
					for (const auto& inst: info->genericInstantiations) {
						members.push_back(translateFunction(inst->function, true));
					}
				}
			}
		} else {
			members.push_back(translateFunction(func, true));
		}
	}
#endif

	LLVMMetadataRef templateParams = NULL;

	if (!klass->genericArguments.empty()) {
		std::vector<LLVMMetadataRef> generics;
		auto ast = klass->ast.lock();
		auto info = klass->info.lock();
		for (size_t i = 0; i < klass->genericArguments.size(); ++i) {
			auto scope = tmpDebug;
			auto type = translateTypeDebug(klass->genericArguments[i]);
			//LLVMDumpValue(LLVMMetadataAsValue(_llcontext.get(), scope));
			//LLVMDumpValue(LLVMMetadataAsValue(_llcontext.get(), type));
			auto unwrappedScope = unwrapDI<llvm::DIScope>(scope);
			auto unwrappedType = unwrapDI<llvm::DIType>(type);
			generics.push_back(llvm::wrap(llvm::unwrap(_debugBuilder.get())->createTemplateTypeParameter(unwrappedScope, info->genericDetails[i]->alias->name, unwrappedType, false)));
		}
		templateParams = LLVMDIBuilderGetOrCreateArray(_debugBuilder.get(), generics.data(), generics.size());
	}

	auto permaNode = _definedDebugTypes[klass->id] = LLVMDIBuilderCreateClassType(_debugBuilder.get(), translateParentScope(klass), klass->name.c_str(), klass->name.size(), debugFileForScopeItem(klass), klass->position.line, LLVMStoreSizeOfType(_targetData.get(), lltype) * 8, LLVMABIAlignmentOfType(_targetData.get(), lltype) * 8, 0, LLVMDIFlagZero, NULL, members.data(), members.size(), NULL, templateParams, "", 0);

	LLVMMetadataReplaceAllUsesWith(tmpDebug, permaNode);

	return permaNode;
};

LLVMMetadataRef AltaLL::Compiler::translateTypeDebug(std::shared_ptr<AltaCore::DET::Type> type, bool usePointersToFunctions) {
	auto mangled = "_non_native_" + mangleType(type);

	if (usePointersToFunctions && type->isFunction && type->isRawFunction) {
		mangled = "_ptr_" + mangled;
	}

	if (_definedDebugTypes[mangled.c_str()]) {
		return _definedDebugTypes[mangled.c_str()];
	}

	auto debugFile = debugFileForScopeItem(type);

	LLVMMetadataRef result = NULL;

	if (type->isNative && !type->isFunction) {
		switch (type->nativeTypeName) {
			case AltaCore::DET::NativeType::Integer: {
				uint8_t bits = 32;

				for (const auto& modifier: type->modifiers) {
					if (modifier & static_cast<uint8_t>(AltaCore::DET::TypeModifierFlag::Long)) {
						if (bits < 64) {
							bits *= 2;
						}
					}

					if (modifier & static_cast<uint8_t>(AltaCore::DET::TypeModifierFlag::Short)) {
						if (bits > 8) {
							bits /= 2;
						}
					}

					if (modifier & (static_cast<uint8_t>(AltaCore::DET::TypeModifierFlag::Pointer) | static_cast<uint8_t>(AltaCore::DET::TypeModifierFlag::Reference))) {
						break;
					}
				}

				auto name = (type->isSigned() ? "i" : "u") + std::to_string(bits);

				result = LLVMDIBuilderCreateBasicType(_debugBuilder.get(), name.c_str(), name.size(), bits, type->isSigned() ? llvm::dwarf::DW_ATE_signed : llvm::dwarf::DW_ATE_unsigned, LLVMDIFlagZero);
			} break;
			case AltaCore::DET::NativeType::Byte:        result = LLVMDIBuilderCreateBasicType(_debugBuilder.get(), "byte", 4, 8, llvm::dwarf::DW_ATE_unsigned, LLVMDIFlagZero); break;
			case AltaCore::DET::NativeType::Bool:        result = LLVMDIBuilderCreateBasicType(_debugBuilder.get(), "bool", 4, 8, llvm::dwarf::DW_ATE_boolean, LLVMDIFlagZero); break;
			case AltaCore::DET::NativeType::Void:        result = LLVMDIBuilderCreateUnspecifiedType(_debugBuilder.get(), "void", 4); break;
			case AltaCore::DET::NativeType::Double:      result = LLVMDIBuilderCreateBasicType(_debugBuilder.get(), "double", 6, 64, llvm::dwarf::DW_ATE_float, LLVMDIFlagZero); break;
			case AltaCore::DET::NativeType::Float:       result = LLVMDIBuilderCreateBasicType(_debugBuilder.get(), "float", 5, 32, llvm::dwarf::DW_ATE_float, LLVMDIFlagZero); break;
			case AltaCore::DET::NativeType::UserDefined: result = NULL; break;
		}
	} else if (type->isFunction) {
		if (type->isRawFunction) {
			std::vector<LLVMMetadataRef> params {
				translateTypeDebug(type->returnType),
			};
			bool isVararg = false;

			if (type->isMethod) {
				auto parentPtrType = translateTypeDebug(std::make_shared<AltaCore::DET::Type>(type->methodParent)->reference());
				params.push_back(LLVMDIBuilderCreateObjectPointerType(_debugBuilder.get(), parentPtrType));
			}

			for (const auto& [name, paramType, isVariable, id]: type->parameters) {
				auto lltype = translateTypeDebug(paramType);

				if (isVariable) {
					if (varargTable[id]) {
						isVararg = true;
					} else {
						params.push_back(LLVMDIBuilderCreateBasicType(_debugBuilder.get(), "u64", 3, 64, llvm::dwarf::DW_ATE_unsigned, LLVMDIFlagZero));
						params.push_back(LLVMDIBuilderCreateArrayType(_debugBuilder.get(), 0, LLVMDITypeGetAlignInBits(lltype), lltype, NULL, 0));
					}
				} else {
					params.push_back(lltype);
				}
			}

			result = LLVMDIBuilderCreateSubroutineType(_debugBuilder.get(), debugFile, params.data(), params.size(), LLVMDIFlagZero);

			if (usePointersToFunctions) {
				result = LLVMDIBuilderCreatePointerType(_debugBuilder.get(), result, _pointerBits, _pointerBits, 0, NULL, 0);
			}
		} else {
			result = _definedDebugTypes["_Alta_basic_function"];
		}
	} else if (type->isUnion()) {
		std::vector<LLVMMetadataRef> members;
		std::array<LLVMMetadataRef, 2> structMembers;
		std::array<LLVMTypeRef, 2> offsetMembers;

		auto tagBits = std::bit_width(type->unionOf.size() - 1);
		auto tagType = LLVMIntTypeInContext(_llcontext.get(), tagBits);
		auto tagDebugTypeName = "__tag" + std::to_string(tagBits);
		auto tagDebugType = LLVMDIBuilderCreateBasicType(_debugBuilder.get(), tagDebugTypeName.c_str(), tagDebugTypeName.size(), LLVMStoreSizeOfType(_targetData.get(), tagType) * 8, llvm::dwarf::DW_ATE_unsigned, LLVMDIFlagArtificial);

		uint64_t unionSize = 0;
		uint32_t unionAlign = 0;

		structMembers[0] = LLVMDIBuilderCreateMemberType(_debugBuilder.get(), translateParentScope(type), "__tag", sizeof("__tag") - 1, debugFile, type->position.line, tagBits, LLVMABIAlignmentOfType(_targetData.get(), tagType) * 8, 0, static_cast<LLVMDIFlags>(LLVMDIFlagPublic), tagDebugType);
		offsetMembers[0] = tagType;

		for (const auto& component: type->unionOf) {
			auto co = translateType(component);
			co.coroutine.resume();
			offsetMembers[1] = co.await_resume();

			auto debugType = translateTypeDebug(component);

			auto tmpStruct = LLVMStructTypeInContext(_llcontext.get(), offsetMembers.data(), offsetMembers.size(), false);

			auto offset = LLVMOffsetOfElement(_targetData.get(), tmpStruct, 1);
			structMembers[1] = LLVMDIBuilderCreateMemberType(_debugBuilder.get(), translateParentScope(type), "__body", sizeof("__body") - 1, debugFile, type->position.line, LLVMStoreSizeOfType(_targetData.get(), offsetMembers[1]) * 8, LLVMABIAlignmentOfType(_targetData.get(), offsetMembers[1]) * 8, offset * 8, static_cast<LLVMDIFlags>(LLVMDIFlagPublic), debugType);

			offsetMembers[1] = nullptr;

			auto size = LLVMStoreSizeOfType(_targetData.get(), tmpStruct);
			auto align = LLVMABIAlignmentOfType(_targetData.get(), tmpStruct);

			if (size > unionSize) {
				unionSize = size;
			}

			if (align > unionAlign) {
				unionAlign = align;
			}

			auto memberStruct = LLVMDIBuilderCreateStructType(_debugBuilder.get(), translateParentScope(type), "", 0, debugFile, type->position.line, size * 8, align * 8, LLVMDIFlagArtificial, NULL, structMembers.data(), structMembers.size(), 0, NULL, "", 0);
			auto mangledName = mangleType(component);
			members.push_back(LLVMDIBuilderCreateMemberType(_debugBuilder.get(), translateParentScope(type), mangledName.c_str(), mangledName.size(), debugFile, type->position.line, size * 8, align * 8, 0, static_cast<LLVMDIFlags>(LLVMDIFlagPublic), memberStruct));
			structMembers[1] = nullptr;
		}

		result = LLVMDIBuilderCreateUnionType(_debugBuilder.get(), translateParentScope(type), "", 0, debugFile, type->position.line, unionSize * 8, unionAlign * 8, LLVMDIFlagZero, members.data(), members.size(), 0, "", 0);
	} else if (type->isOptional) {
		std::array<LLVMTypeRef, 2> members;
		std::array<LLVMMetadataRef, 2> debugMembers;

		members[0] = LLVMInt1TypeInContext(_llcontext.get());

		bool isVoid = *type->optionalTarget == AltaCore::DET::Type(AltaCore::DET::NativeType::Void);

		if (isVoid) {
			members[1] = LLVMStructTypeInContext(_llcontext.get(), NULL, 0, false);
		} else {
			auto co = translateType(type->optionalTarget);
			co.coroutine.resume();
			members[1] = co.await_resume();
		}

		auto tmpStruct = LLVMStructTypeInContext(_llcontext.get(), members.data(), members.size(), false);

		auto boolType = LLVMDIBuilderCreateBasicType(_debugBuilder.get(), "bool", 4, 8, llvm::dwarf::DW_ATE_boolean, LLVMDIFlagZero);
		debugMembers[0] = LLVMDIBuilderCreateMemberType(_debugBuilder.get(), translateParentScope(type), "__present", sizeof("__present") - 1, debugFile, type->position.line, 8, 8, 0, static_cast<LLVMDIFlags>(LLVMDIFlagPublic), boolType);

		auto targetType = translateTypeDebug(type->optionalTarget);
		debugMembers[1] = LLVMDIBuilderCreateMemberType(_debugBuilder.get(), translateParentScope(type), "__body", sizeof("__body") - 1, debugFile, type->position.line, LLVMDITypeGetSizeInBits(targetType), LLVMDITypeGetAlignInBits(targetType), LLVMOffsetOfElement(_targetData.get(), tmpStruct, 1) * 8, static_cast<LLVMDIFlags>(LLVMDIFlagPublic), targetType);

		result = LLVMDIBuilderCreateStructType(_debugBuilder.get(), translateParentScope(type), "", 0, debugFile, type->position.line, LLVMStoreSizeOfType(_targetData.get(), tmpStruct) * 8, LLVMABIAlignmentOfType(_targetData.get(), tmpStruct) * 8, LLVMDIFlagZero, NULL, debugMembers.data(), debugMembers.size(), 0, NULL, "", 0);
	} else if (type->bitfield) {
		result = translateTypeDebug(type->bitfield->underlyingBitfieldType.lock());
	} else if (type->klass) {
		result = translateClassDebug(type->klass);
	}

	if (!result) {
		return result;
	}

	for (const auto& modifier: type->modifiers) {
		if (modifier & (static_cast<uint8_t>(AltaCore::DET::TypeModifierFlag::Pointer) | static_cast<uint8_t>(AltaCore::DET::TypeModifierFlag::Reference))) {
			result = LLVMDIBuilderCreatePointerType(_debugBuilder.get(), result, _pointerBits, _pointerBits, 0, "", 0);
		}
	}

	_definedDebugTypes[mangled.c_str()] = result;

	return result;
};

std::pair<LLVMValueRef, LLVMTypeRef> AltaLL::Compiler::defineRuntimeFunction(const std::string& name) {
	LLVMValueRef llfunc = NULL;
	LLVMTypeRef llfuncType = NULL;

	if (name == "_Alta_suspendable_push_stack") {
		auto basicSuspendablePtrType = LLVMPointerType(_definedTypes["_Alta_basic_suspendable"], 0);
		auto voidType = LLVMVoidTypeInContext(_llcontext.get());
		std::array<LLVMTypeRef, 2> params {
			basicSuspendablePtrType,
			LLVMInt64TypeInContext(_llcontext.get()),
		};
		llfuncType = LLVMFunctionType(voidType, params.data(), params.size(), false);
	} else if (name == "_Alta_suspendable_pop_stack" || name == "_Alta_suspendable_reload" || name == "_Alta_suspendable_reload_continue" || name == "_Alta_bad_suspendable_state") {
		auto basicSuspendablePtrType = LLVMPointerType(_definedTypes["_Alta_basic_suspendable"], 0);
		auto voidType = LLVMVoidTypeInContext(_llcontext.get());
		std::array<LLVMTypeRef, 1> params {
			basicSuspendablePtrType,
		};
		llfuncType = LLVMFunctionType(voidType, params.data(), params.size(), false);
	} else if (name == "_Alta_suspendable_alloca") {
		auto basicSuspendablePtrType = LLVMPointerType(_definedTypes["_Alta_basic_suspendable"], 0);
		auto voidType = LLVMVoidTypeInContext(_llcontext.get());
		auto voidPointerType = LLVMPointerType(voidType, 0);
		std::array<LLVMTypeRef, 2> params {
			basicSuspendablePtrType,
			LLVMInt64TypeInContext(_llcontext.get()),
		};
		llfuncType = LLVMFunctionType(voidPointerType, params.data(), params.size(), false);
	} else if (name == "_Alta_get_child") {
		auto voidType = LLVMStructTypeInContext(_llcontext.get(), NULL, 0, false);
		auto voidPointerType = LLVMPointerType(voidType, 0);
		auto stringType = LLVMPointerType(LLVMInt8TypeInContext(_llcontext.get()), 0);
		auto stringPtrType = LLVMPointerType(stringType, 0);
		auto i64Type = LLVMInt64TypeInContext(_llcontext.get());
		std::array<LLVMTypeRef, 3> params {
			voidPointerType,
			i64Type,
			stringPtrType,
		};
		llfuncType = LLVMFunctionType(voidPointerType, params.data(), params.size(), false);
	} else if (name == "_Alta_bad_cast") {
		auto strType = LLVMPointerType(LLVMInt8TypeInContext(_llcontext.get()), 0);
		std::array<LLVMTypeRef, 2> params {
			strType,
			strType,
		};
		llfuncType = LLVMFunctionType(LLVMVoidTypeInContext(_llcontext.get()), params.data(), params.size(), false);
	} else if (name == "_Alta_bad_enum") {
		auto i64Type = LLVMInt64TypeInContext(_llcontext.get());
		auto strType = LLVMPointerType(LLVMInt8TypeInContext(_llcontext.get()), 0);
		std::array<LLVMTypeRef, 2> params {
			strType,
			i64Type,
		};
		llfuncType = LLVMFunctionType(LLVMVoidTypeInContext(_llcontext.get()), params.data(), params.size(), false);
	} else if (name == "_Alta_virtual_lookup") {
		auto voidType = LLVMStructTypeInContext(_llcontext.get(), NULL, 0, false);
		auto voidPointerType = LLVMPointerType(voidType, 0);
		auto stringType = LLVMPointerType(LLVMInt8TypeInContext(_llcontext.get()), 0);
		std::array<LLVMTypeRef, 2> params {
			voidPointerType,
			stringType,
		};
		llfuncType = LLVMFunctionType(voidPointerType, params.data(), params.size(), false);
	} else if (name == "_Alta_runtime_rawstrings_equal") {
		auto boolType = LLVMInt1TypeInContext(_llcontext.get());
		auto stringType = LLVMPointerType(LLVMInt8TypeInContext(_llcontext.get()), 0);
		std::array<LLVMTypeRef, 2> params {
			stringType,
			stringType,
		};
		llfuncType = LLVMFunctionType(boolType, params.data(), params.size(), false);
	} else {
		throw std::runtime_error("Unknown runtime function: " + name);
	}

	if (!_definedFunctions[name]) {
		_definedFunctions[name] = LLVMAddFunction(_llmod.get(), name.c_str(), llfuncType);
	}

	llfunc = _definedFunctions[name];

	return std::make_pair(llfunc, llfuncType);
};

LLVMValueRef AltaLL::Compiler::buildSuspendablePushStack(LLBuilder builder, LLVMValueRef suspendableContext, LLVMValueRef stackSize) {
	auto [llfunc, llfuncType] = defineRuntimeFunction("_Alta_suspendable_push_stack");
	std::array<LLVMValueRef, 2> args {
		LLVMBuildPointerCast(builder.get(), suspendableContext, LLVMPointerType(_definedTypes["_Alta_basic_suspendable"], 0), ""),
		stackSize,
	};
	return LLVMBuildCall2(builder.get(), llfuncType, llfunc, args.data(), args.size(), "");
};

LLVMValueRef AltaLL::Compiler::buildSuspendablePushStack(LLBuilder builder, LLVMValueRef suspendableContext, size_t stackSize) {
	return buildSuspendablePushStack(builder, suspendableContext, LLVMConstInt(LLVMInt64TypeInContext(_llcontext.get()), stackSize, false));
};

void AltaLL::Compiler::buildSuspendablePopStack(LLBuilder builder, LLVMValueRef suspendableContext) {
	auto [llfunc, llfuncType] = defineRuntimeFunction("_Alta_suspendable_pop_stack");
	std::array<LLVMValueRef, 1> args {
		LLVMBuildPointerCast(builder.get(), suspendableContext, LLVMPointerType(_definedTypes["_Alta_basic_suspendable"], 0), ""),
	};
	LLVMBuildCall2(builder.get(), llfuncType, llfunc, args.data(), args.size(), "");
};

LLVMValueRef AltaLL::Compiler::buildSuspendableAlloca(LLBuilder builder, LLVMValueRef suspendableContext) {
	auto [llfunc, llfuncType] = defineRuntimeFunction("_Alta_suspendable_alloca");
	std::array<LLVMValueRef, 2> args {
		LLVMBuildPointerCast(builder.get(), suspendableContext, LLVMPointerType(_definedTypes["_Alta_basic_suspendable"], 0), ""),
		LLVMGetPoison(LLVMInt64TypeInContext(_llcontext.get())),
	};
	return LLVMBuildCall2(builder.get(), llfuncType, llfunc, args.data(), args.size(), "");
};

void AltaLL::Compiler::buildSuspendableReload(LLBuilder builder, LLVMValueRef suspendableContext) {
	auto [llfunc, llfuncType] = defineRuntimeFunction("_Alta_suspendable_reload");
	std::array<LLVMValueRef, 1> args {
		LLVMBuildPointerCast(builder.get(), suspendableContext, LLVMPointerType(_definedTypes["_Alta_basic_suspendable"], 0), ""),
	};
	LLVMBuildCall2(builder.get(), llfuncType, llfunc, args.data(), args.size(), "");
};

void AltaLL::Compiler::buildSuspendableReloadContinue(LLBuilder builder, LLVMValueRef suspendableContext) {
	auto [llfunc, llfuncType] = defineRuntimeFunction("_Alta_suspendable_reload_continue");
	std::array<LLVMValueRef, 1> args {
		LLVMBuildPointerCast(builder.get(), suspendableContext, LLVMPointerType(_definedTypes["_Alta_basic_suspendable"], 0), ""),
	};
	LLVMBuildCall2(builder.get(), llfuncType, llfunc, args.data(), args.size(), "");
};

LLVMValueRef AltaLL::Compiler::buildGetChild(LLBuilder builder, LLVMValueRef instance, const std::vector<std::string>& parentContainerPath) {
	auto [llfunc, llfuncType] = defineRuntimeFunction("_Alta_get_child");
	auto llvoidPtrType = LLVMPointerType(LLVMStructTypeInContext(_llcontext.get(), NULL, 0, false), 0);

	auto stringType = LLVMPointerType(LLVMInt8TypeInContext(_llcontext.get()), 0);
	auto arrayType = LLVMArrayType(stringType, parentContainerPath.size());
	auto containerPathArray = LLVMAddGlobal(_llmod.get(), arrayType, "");
	LLVMSetLinkage(containerPathArray, LLVMInternalLinkage);

	std::vector<LLVMValueRef> strings;

	for (const auto& component: parentContainerPath) {
		strings.push_back(LLVMBuildGlobalStringPtr(builder.get(), component.c_str(), ""));
	}

	auto init = LLVMConstArray(stringType, strings.data(), strings.size());
	LLVMSetInitializer(containerPathArray, init);
	LLVMSetGlobalConstant(containerPathArray, true);

	std::array<LLVMValueRef, 3> args {
		LLVMBuildPointerCast(builder.get(), instance, llvoidPtrType, ""),
		LLVMConstInt(LLVMInt64TypeInContext(_llcontext.get()), parentContainerPath.size(), false),
		containerPathArray,
	};
	return LLVMBuildCall2(builder.get(), llfuncType, llfunc, args.data(), args.size(), "");
};

void AltaLL::Compiler::buildBadCast(LLBuilder builder, const std::string& from, const std::string& to) {
	auto [llfunc, llfuncType] = defineRuntimeFunction("_Alta_bad_cast");
	std::array<LLVMValueRef, 2> args {
		LLVMBuildGlobalStringPtr(builder.get(), from.c_str(), ""),
		LLVMBuildGlobalStringPtr(builder.get(), to.c_str(), ""),
	};
	LLVMBuildCall2(builder.get(), llfuncType, llfunc, args.data(), args.size(), "");
	LLVMBuildUnreachable(builder.get());
};

void AltaLL::Compiler::buildBadEnum(LLBuilder builder, const std::string& enumType, LLVMValueRef badEnumValue) {
	auto [llfunc, llfuncType] = defineRuntimeFunction("_Alta_bad_enum");
	std::array<LLVMValueRef, 2> args {
		LLVMBuildGlobalStringPtr(builder.get(), enumType.c_str(), ""),
		LLVMBuildIntCast2(builder.get(), badEnumValue, LLVMInt64TypeInContext(_llcontext.get()), false, ""),
	};
	LLVMBuildCall2(builder.get(), llfuncType, llfunc, args.data(), args.size(), "");
	LLVMBuildUnreachable(builder.get());
};

LLVMValueRef AltaLL::Compiler::buildVirtualLookup(LLBuilder builder, LLVMValueRef instance, const std::string& methodName) {
	auto [llfunc, llfuncType] = defineRuntimeFunction("_Alta_virtual_lookup");
	auto llvoidPtrType = LLVMPointerType(LLVMStructTypeInContext(_llcontext.get(), NULL, 0, false), 0);
	std::array<LLVMValueRef, 2> args {
		LLVMBuildPointerCast(builder.get(), instance, llvoidPtrType, ""),
		LLVMBuildGlobalStringPtr(builder.get(), methodName.c_str(), ""),
	};
	return LLVMBuildCall2(builder.get(), llfuncType, llfunc, args.data(), args.size(), "");
};

LLVMValueRef AltaLL::Compiler::buildRawStringsAreEqual(LLBuilder builder, LLVMValueRef lhs, LLVMValueRef rhs) {
	auto [llfunc, llfuncType] = defineRuntimeFunction("_Alta_runtime_rawstrings_equal");
	std::array<LLVMValueRef, 2> args {
		lhs,
		rhs,
	};
	return LLVMBuildCall2(builder.get(), llfuncType, llfunc, args.data(), args.size(), "");
};

void AltaLL::Compiler::buildBadSuspendableState(LLBuilder builder, LLVMValueRef suspendableContext) {
	auto [llfunc, llfuncType] = defineRuntimeFunction("_Alta_bad_suspendable_state");
	std::array<LLVMValueRef, 1> args {
		suspendableContext,
	};
	LLVMBuildCall2(builder.get(), llfuncType, llfunc, args.data(), args.size(), "");
	LLVMBuildUnreachable(builder.get());
};

void AltaLL::Compiler::updateSuspendableAlloca(LLVMValueRef alloca, size_t stackOffset) {
	LLVMSetOperand(alloca, 1, LLVMConstInt(LLVMInt64TypeInContext(_llcontext.get()), stackOffset, false));
};

void AltaLL::Compiler::updateSuspendablePushStack(LLVMValueRef push, size_t stackSize) {
	LLVMSetOperand(push, 1, LLVMConstInt(LLVMInt64TypeInContext(_llcontext.get()), stackSize, false));
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileNode(std::shared_ptr<AltaCore::AST::Node> node, std::shared_ptr<AltaCore::DH::Node> info) {
	switch (node->nodeType()) {
		case AltaCore::AST::NodeType::Node: return returnNull();
		case AltaCore::AST::NodeType::StatementNode: return returnNull();
		case AltaCore::AST::NodeType::ExpressionNode: return returnNull();
		case AltaCore::AST::NodeType::RootNode: return returnNull();
		case AltaCore::AST::NodeType::Type: return returnNull();
		case AltaCore::AST::NodeType::Parameter: return returnNull();
		case AltaCore::AST::NodeType::ImportStatement: return returnNull();
		case AltaCore::AST::NodeType::AttributeNode: return returnNull();
		case AltaCore::AST::NodeType::LiteralNode: return returnNull();
		case AltaCore::AST::NodeType::ClassStatementNode: return returnNull();
		case AltaCore::AST::NodeType::TypeAliasStatement: return returnNull();
		case AltaCore::AST::NodeType::RetrievalNode: return returnNull();
		case AltaCore::AST::NodeType::Generic: return returnNull();
		case AltaCore::AST::NodeType::ExportStatement: return returnNull();
		case AltaCore::AST::NodeType::AliasStatement: return returnNull();
		case AltaCore::AST::NodeType::ClassMemberDefinitionStatement: return returnNull();

		case AltaCore::AST::NodeType::ExpressionStatement: return compileExpressionStatement(std::dynamic_pointer_cast<AltaCore::AST::ExpressionStatement>(node), std::dynamic_pointer_cast<AltaCore::DH::ExpressionStatement>(info));
		case AltaCore::AST::NodeType::BlockNode: return compileBlockNode(std::dynamic_pointer_cast<AltaCore::AST::BlockNode>(node), std::dynamic_pointer_cast<AltaCore::DH::BlockNode>(info));
		case AltaCore::AST::NodeType::FunctionDefinitionNode: return compileFunctionDefinitionNode(std::dynamic_pointer_cast<AltaCore::AST::FunctionDefinitionNode>(node), std::dynamic_pointer_cast<AltaCore::DH::FunctionDefinitionNode>(info));
		case AltaCore::AST::NodeType::ReturnDirectiveNode: return compileReturnDirectiveNode(std::dynamic_pointer_cast<AltaCore::AST::ReturnDirectiveNode>(node), std::dynamic_pointer_cast<AltaCore::DH::ReturnDirectiveNode>(info));
		case AltaCore::AST::NodeType::IntegerLiteralNode: return compileIntegerLiteralNode(std::dynamic_pointer_cast<AltaCore::AST::IntegerLiteralNode>(node), std::dynamic_pointer_cast<AltaCore::DH::IntegerLiteralNode>(info));
		case AltaCore::AST::NodeType::VariableDefinitionExpression: return compileVariableDefinitionExpression(std::dynamic_pointer_cast<AltaCore::AST::VariableDefinitionExpression>(node), std::dynamic_pointer_cast<AltaCore::DH::VariableDefinitionExpression>(info));
		case AltaCore::AST::NodeType::Accessor: return compileAccessor(std::dynamic_pointer_cast<AltaCore::AST::Accessor>(node), std::dynamic_pointer_cast<AltaCore::DH::Accessor>(info));
		case AltaCore::AST::NodeType::Fetch: return compileFetch(std::dynamic_pointer_cast<AltaCore::AST::Fetch>(node), std::dynamic_pointer_cast<AltaCore::DH::Fetch>(info));
		case AltaCore::AST::NodeType::AssignmentExpression: return compileAssignmentExpression(std::dynamic_pointer_cast<AltaCore::AST::AssignmentExpression>(node), std::dynamic_pointer_cast<AltaCore::DH::AssignmentExpression>(info));
		case AltaCore::AST::NodeType::BooleanLiteralNode: return compileBooleanLiteralNode(std::dynamic_pointer_cast<AltaCore::AST::BooleanLiteralNode>(node), std::dynamic_pointer_cast<AltaCore::DH::BooleanLiteralNode>(info));
		case AltaCore::AST::NodeType::BinaryOperation: return compileBinaryOperation(std::dynamic_pointer_cast<AltaCore::AST::BinaryOperation>(node), std::dynamic_pointer_cast<AltaCore::DH::BinaryOperation>(info));
		case AltaCore::AST::NodeType::FunctionCallExpression: return compileFunctionCallExpression(std::dynamic_pointer_cast<AltaCore::AST::FunctionCallExpression>(node), std::dynamic_pointer_cast<AltaCore::DH::FunctionCallExpression>(info));
		case AltaCore::AST::NodeType::StringLiteralNode: return compileStringLiteralNode(std::dynamic_pointer_cast<AltaCore::AST::StringLiteralNode>(node), std::dynamic_pointer_cast<AltaCore::DH::StringLiteralNode>(info));
		case AltaCore::AST::NodeType::FunctionDeclarationNode: return compileFunctionDeclarationNode(std::dynamic_pointer_cast<AltaCore::AST::FunctionDeclarationNode>(node), std::dynamic_pointer_cast<AltaCore::DH::FunctionDeclarationNode>(info));
		case AltaCore::AST::NodeType::AttributeStatement: return compileAttributeStatement(std::dynamic_pointer_cast<AltaCore::AST::AttributeStatement>(node), std::dynamic_pointer_cast<AltaCore::DH::AttributeStatement>(info));
		case AltaCore::AST::NodeType::ConditionalStatement: return compileConditionalStatement(std::dynamic_pointer_cast<AltaCore::AST::ConditionalStatement>(node), std::dynamic_pointer_cast<AltaCore::DH::ConditionalStatement>(info));
		case AltaCore::AST::NodeType::ConditionalExpression: return compileConditionalExpression(std::dynamic_pointer_cast<AltaCore::AST::ConditionalExpression>(node), std::dynamic_pointer_cast<AltaCore::DH::ConditionalExpression>(info));
		case AltaCore::AST::NodeType::ClassDefinitionNode: return compileClassDefinitionNode(std::dynamic_pointer_cast<AltaCore::AST::ClassDefinitionNode>(node), std::dynamic_pointer_cast<AltaCore::DH::ClassDefinitionNode>(info));
		case AltaCore::AST::NodeType::ClassMethodDefinitionStatement: return compileClassMethodDefinitionStatement(std::dynamic_pointer_cast<AltaCore::AST::ClassMethodDefinitionStatement>(node), std::dynamic_pointer_cast<AltaCore::DH::ClassMethodDefinitionStatement>(info));
		case AltaCore::AST::NodeType::ClassSpecialMethodDefinitionStatement: return compileClassSpecialMethodDefinitionStatement(std::dynamic_pointer_cast<AltaCore::AST::ClassSpecialMethodDefinitionStatement>(node), std::dynamic_pointer_cast<AltaCore::DH::ClassSpecialMethodDefinitionStatement>(info));
		case AltaCore::AST::NodeType::ClassInstantiationExpression: return compileClassInstantiationExpression(std::dynamic_pointer_cast<AltaCore::AST::ClassInstantiationExpression>(node), std::dynamic_pointer_cast<AltaCore::DH::ClassInstantiationExpression>(info));
		case AltaCore::AST::NodeType::PointerExpression: return compilePointerExpression(std::dynamic_pointer_cast<AltaCore::AST::PointerExpression>(node), std::dynamic_pointer_cast<AltaCore::DH::PointerExpression>(info));
		case AltaCore::AST::NodeType::DereferenceExpression: return compileDereferenceExpression(std::dynamic_pointer_cast<AltaCore::AST::DereferenceExpression>(node), std::dynamic_pointer_cast<AltaCore::DH::DereferenceExpression>(info));
		case AltaCore::AST::NodeType::WhileLoopStatement: return compileWhileLoopStatement(std::dynamic_pointer_cast<AltaCore::AST::WhileLoopStatement>(node), std::dynamic_pointer_cast<AltaCore::DH::WhileLoopStatement>(info));
		case AltaCore::AST::NodeType::CastExpression: return compileCastExpression(std::dynamic_pointer_cast<AltaCore::AST::CastExpression>(node), std::dynamic_pointer_cast<AltaCore::DH::CastExpression>(info));
		case AltaCore::AST::NodeType::ClassReadAccessorDefinitionStatement: return compileClassReadAccessorDefinitionStatement(std::dynamic_pointer_cast<AltaCore::AST::ClassReadAccessorDefinitionStatement>(node), std::dynamic_pointer_cast<AltaCore::DH::ClassReadAccessorDefinitionStatement>(info));
		case AltaCore::AST::NodeType::CharacterLiteralNode: return compileCharacterLiteralNode(std::dynamic_pointer_cast<AltaCore::AST::CharacterLiteralNode>(node), std::dynamic_pointer_cast<AltaCore::DH::CharacterLiteralNode>(info));
		case AltaCore::AST::NodeType::SubscriptExpression: return compileSubscriptExpression(std::dynamic_pointer_cast<AltaCore::AST::SubscriptExpression>(node), std::dynamic_pointer_cast<AltaCore::DH::SubscriptExpression>(info));
		case AltaCore::AST::NodeType::SuperClassFetch: return compileSuperClassFetch(std::dynamic_pointer_cast<AltaCore::AST::SuperClassFetch>(node), std::dynamic_pointer_cast<AltaCore::DH::SuperClassFetch>(info));
		case AltaCore::AST::NodeType::InstanceofExpression: return compileInstanceofExpression(std::dynamic_pointer_cast<AltaCore::AST::InstanceofExpression>(node), std::dynamic_pointer_cast<AltaCore::DH::InstanceofExpression>(info));
		case AltaCore::AST::NodeType::ForLoopStatement: return compileForLoopStatement(std::dynamic_pointer_cast<AltaCore::AST::ForLoopStatement>(node), std::dynamic_pointer_cast<AltaCore::DH::ForLoopStatement>(info));
		case AltaCore::AST::NodeType::RangedForLoopStatement: return compileRangedForLoopStatement(std::dynamic_pointer_cast<AltaCore::AST::RangedForLoopStatement>(node), std::dynamic_pointer_cast<AltaCore::DH::RangedForLoopStatement>(info));
		case AltaCore::AST::NodeType::UnaryOperation: return compileUnaryOperation(std::dynamic_pointer_cast<AltaCore::AST::UnaryOperation>(node), std::dynamic_pointer_cast<AltaCore::DH::UnaryOperation>(info));
		case AltaCore::AST::NodeType::SizeofOperation: return compileSizeofOperation(std::dynamic_pointer_cast<AltaCore::AST::SizeofOperation>(node), std::dynamic_pointer_cast<AltaCore::DH::SizeofOperation>(info));
		case AltaCore::AST::NodeType::FloatingPointLiteralNode: return compileFloatingPointLiteralNode(std::dynamic_pointer_cast<AltaCore::AST::FloatingPointLiteralNode>(node), std::dynamic_pointer_cast<AltaCore::DH::FloatingPointLiteralNode>(info));
		case AltaCore::AST::NodeType::StructureDefinitionStatement: return compileStructureDefinitionStatement(std::dynamic_pointer_cast<AltaCore::AST::StructureDefinitionStatement>(node), std::dynamic_pointer_cast<AltaCore::DH::StructureDefinitionStatement>(info));
		case AltaCore::AST::NodeType::VariableDeclarationStatement: return compileVariableDeclarationStatement(std::dynamic_pointer_cast<AltaCore::AST::VariableDeclarationStatement>(node), std::dynamic_pointer_cast<AltaCore::DH::VariableDeclarationStatement>(info));
		case AltaCore::AST::NodeType::DeleteStatement: return compileDeleteStatement(std::dynamic_pointer_cast<AltaCore::AST::DeleteStatement>(node), std::dynamic_pointer_cast<AltaCore::DH::DeleteStatement>(info));
		case AltaCore::AST::NodeType::ControlDirective: return compileControlDirective(std::dynamic_pointer_cast<AltaCore::AST::ControlDirective>(node), std::dynamic_pointer_cast<AltaCore::DH::Node>(info));
		case AltaCore::AST::NodeType::TryCatchBlock: return compileTryCatchBlock(std::dynamic_pointer_cast<AltaCore::AST::TryCatchBlock>(node), std::dynamic_pointer_cast<AltaCore::DH::TryCatchBlock>(info));
		case AltaCore::AST::NodeType::ThrowStatement: return compileThrowStatement(std::dynamic_pointer_cast<AltaCore::AST::ThrowStatement>(node), std::dynamic_pointer_cast<AltaCore::DH::ThrowStatement>(info));
		case AltaCore::AST::NodeType::NullptrExpression: return compileNullptrExpression(std::dynamic_pointer_cast<AltaCore::AST::NullptrExpression>(node), std::dynamic_pointer_cast<AltaCore::DH::NullptrExpression>(info));
		case AltaCore::AST::NodeType::CodeLiteralNode: return compileCodeLiteralNode(std::dynamic_pointer_cast<AltaCore::AST::CodeLiteralNode>(node), std::dynamic_pointer_cast<AltaCore::DH::CodeLiteralNode>(info));
		case AltaCore::AST::NodeType::BitfieldDefinitionNode: return compileBitfieldDefinitionNode(std::dynamic_pointer_cast<AltaCore::AST::BitfieldDefinitionNode>(node), std::dynamic_pointer_cast<AltaCore::DH::BitfieldDefinitionNode>(info));
		case AltaCore::AST::NodeType::LambdaExpression: return compileLambdaExpression(std::dynamic_pointer_cast<AltaCore::AST::LambdaExpression>(node), std::dynamic_pointer_cast<AltaCore::DH::LambdaExpression>(info));
		case AltaCore::AST::NodeType::SpecialFetchExpression: return compileSpecialFetchExpression(std::dynamic_pointer_cast<AltaCore::AST::SpecialFetchExpression>(node), std::dynamic_pointer_cast<AltaCore::DH::SpecialFetchExpression>(info));
		case AltaCore::AST::NodeType::ClassOperatorDefinitionStatement: return compileClassOperatorDefinitionStatement(std::dynamic_pointer_cast<AltaCore::AST::ClassOperatorDefinitionStatement>(node), std::dynamic_pointer_cast<AltaCore::DH::ClassOperatorDefinitionStatement>(info));
		case AltaCore::AST::NodeType::EnumerationDefinitionNode: return compileEnumerationDefinitionNode(std::dynamic_pointer_cast<AltaCore::AST::EnumerationDefinitionNode>(node), std::dynamic_pointer_cast<AltaCore::DH::EnumerationDefinitionNode>(info));
		case AltaCore::AST::NodeType::YieldExpression: return compileYieldExpression(std::dynamic_pointer_cast<AltaCore::AST::YieldExpression>(node), std::dynamic_pointer_cast<AltaCore::DH::YieldExpression>(info));
		case AltaCore::AST::NodeType::AssertionStatement: return compileAssertionStatement(std::dynamic_pointer_cast<AltaCore::AST::AssertionStatement>(node), std::dynamic_pointer_cast<AltaCore::DH::AssertionStatement>(info));
		case AltaCore::AST::NodeType::AwaitExpression: return compileAwaitExpression(std::dynamic_pointer_cast<AltaCore::AST::AwaitExpression>(node), std::dynamic_pointer_cast<AltaCore::DH::AwaitExpression>(info));
		case AltaCore::AST::NodeType::VoidExpression: return compileVoidExpression(std::dynamic_pointer_cast<AltaCore::AST::VoidExpression>(node), std::dynamic_pointer_cast<AltaCore::DH::VoidExpression>(info));
	}

	throw std::runtime_error("Impossible");
};

/*
 * !!!
 * IMPORTANT
 * !!!
 *
 * Every statement must push a new scope stack before evaluating any expressions and clean it up afterwards.
 * This is used to cleanup temporaries created as part of any expression the statement evaluates.
 *
 */

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileExpressionStatement(std::shared_ptr<AltaCore::AST::ExpressionStatement> node, std::shared_ptr<AltaCore::DH::ExpressionStatement> info) {
	LLVMValueRef modInitFunc = NULL;

	if (_builders.size() == 0) {
		modInitFunc = enterInitFunction();
	}

	pushStack(ScopeStack::Type::Temporary);
	pushDebugLocation(node->position, info->inputScope);
	co_await tmpify(node->expression, info->expression);
	co_await currentStack().cleanup();
	popDebugLocation();
	co_await popStack();

	if (modInitFunc) {
		exitInitFunction();
	}

	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileBlockNode(std::shared_ptr<AltaCore::AST::BlockNode> node, std::shared_ptr<AltaCore::DH::BlockNode> info) {
	pushDebugLocation(node->position, info->inputScope);
	for (size_t i = 0; i < node->statements.size(); ++i) {
		pushDebugLocation(node->statements[i]->position);
		co_await compileNode(node->statements[i], info->statements[i]);
		popDebugLocation();
	}
	popDebugLocation();
	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileFunctionDefinitionNode(std::shared_ptr<AltaCore::AST::FunctionDefinitionNode> node, std::shared_ptr<AltaCore::DH::FunctionDefinitionNode> mainInfo) {
	bool isGeneric = mainInfo->function->genericParameterCount > 0;
	size_t iterationCount = isGeneric ? mainInfo->genericInstantiations.size() : 1;

	for (size_t i = 0; i < iterationCount; ++i) {
		auto info = isGeneric ? mainInfo->genericInstantiations[i] : mainInfo;

		auto retType = co_await translateType(info->function->returnType, true, false);

		auto genClass = info->function->isGenerator ? info->generator : (info->function->isAsync ? info->coroutine : nullptr);
		auto llclass = genClass ? co_await defineClassType(genClass) : NULL;
		LLVMMetadataRef debugGenClass = genClass ? translateClassDebug(genClass) : nullptr;

		auto mangled = mangleName(info->function);
		auto [llfuncType, llfunc] = co_await declareFunction(info->function);
		auto origLLFuncType = llfuncType;
		auto origLLFunc = llfunc;

		std::shared_ptr<AltaCore::DET::Function> nextFunc = nullptr;

		auto llfuncDebug = translateFunction(info->function, true);
		LLVMSetSubprogram(llfunc, llfuncDebug);

		LLVMSetLinkage(llfunc, mangled == "main" ? LLVMExternalLinkage : LLVMInternalLinkage);

		auto entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "@entry");
		auto builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
		LLVMPositionBuilderAtEnd(builder.get(), entryBlock);

		_builders.push(builder);

		LLVMValueRef suspendableContext = NULL;

		_suspendableContext.push(nullptr);

		if (info->function->isGenerator || info->function->isAsync) {
			auto [dtorType, dtor] = co_await declareFunction(genClass->destructor);

			// set up the destructor's entry and exit blocks
			LLVMSetLinkage(dtor, LLVMInternalLinkage);
			LLVMAppendBasicBlockInContext(_llcontext.get(), dtor, "@entry");
			LLVMAppendBasicBlockInContext(_llcontext.get(), dtor, "@final");

			suspendableContext = LLVMGetParam(llfunc, 0);

			auto globalClassInfo = LLVMAddGlobal(_llmod.get(), _definedTypes["_Alta_class_info"], (mangleName(genClass) + "@class_info").c_str());

			auto i64Type = LLVMInt64TypeInContext(_llcontext.get());
			auto strType = LLVMPointerType(LLVMInt8TypeInContext(_llcontext.get()), 0);
			auto voidPtrType = LLVMPointerTypeInContext(_llcontext.get(), 0);

			/*
			struct _Alta_class_info {
				const char* type_name;
				void (*destructor)(void*);
				const char* child_name;
				uint64_t offset_from_real;
				uint64_t offset_from_base;
				uint64_t offset_from_owner;
				uint64_t offset_to_next;
				const struct _Alta_virtual_entry* virtual_table;
			};
			*/
			std::array<LLVMValueRef, 8> members {
				LLVMBuildGlobalStringPtr(_builders.top().get(), (mangleName(genClass)).c_str(), ""),
				dtor,
				LLVMConstNull(strType),
				LLVMConstInt(i64Type, 0, false),
				LLVMConstInt(i64Type, 0, false),
				LLVMConstInt(i64Type, 0, false),
				LLVMConstInt(i64Type, 0, false),
				LLVMConstNull(voidPtrType),
			};

			LLVMSetInitializer(globalClassInfo, LLVMConstNamedStruct(_definedTypes["_Alta_class_info"], members.data(), members.size()));
			LLVMSetGlobalConstant(globalClassInfo, true);

			suspendableContext = LLVMBuildAlloca(_builders.top().get(), llclass, "@suspendable");
			auto initVal = LLVMConstNull(llclass);
			if (info->coroutine) {
				auto nextFunc = std::dynamic_pointer_cast<AltaCore::DET::Function>(genClass->scope->items[3]);
				auto [coroNextType, coroNext] = co_await declareFunction(nextFunc);

				auto initContext = LLVMConstNull(_definedTypes["_Alta_basic_coroutine"]);
				initContext = LLVMBuildInsertValue(_builders.top().get(), initContext, coroNext, 2, "@suspendable_init_context");
				initVal = LLVMBuildInsertValue(_builders.top().get(), initVal, initContext, 0, "@suspendable_init_val");
			}
			LLVMBuildStore(_builders.top().get(), initVal, suspendableContext);

			auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
			auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());

			std::array<LLVMValueRef, 4> gepIndices {
				LLVMConstInt(gepIndexType, 0, false), // the first index in the "array"
				LLVMConstInt(gepStructIndexType, 0, false), // @Suspendable@::basic_context
				LLVMConstInt(gepStructIndexType, 0, false), // _Alta_basic_{coroutine,generator}::basic_suspendable
				LLVMConstInt(gepStructIndexType, 0, false), // _Alta_basic_suspendable::instance_info
			};

			std::array<LLVMValueRef, 1> instInfoVals {
				globalClassInfo,
			};

			auto instInfoGEP = LLVMBuildGEP2(_builders.top().get(), llclass, suspendableContext, gepIndices.data(), gepIndices.size(), "@suspendable_instance_info_init_ref");
			LLVMBuildStore(_builders.top().get(), LLVMConstNamedStruct(_definedTypes["_Alta_instance_info"], instInfoVals.data(), instInfoVals.size()), instInfoGEP);
		}

		pushStack(ScopeStack::Type::Function);
		pushUnknownDebugLocation(info->function->scope);

		std::vector<LLVMTypeRef> paramStackMembers {
			_definedTypes["_Alta_basic_suspendable_stack"],
		};

		if (suspendableContext) {
			if (info->function->isMethod) {
				paramStackMembers.push_back(co_await translateType(info->function->parentClassType));
			}

			for (size_t i = 0; i < info->parameters.size(); ++i) {
				const auto& param = node->parameters[i];
				const auto& paramInfo = info->parameters[i];
				const auto& var = info->function->parameterVariables[i];

				if (param->isVariable) {
					throw std::runtime_error("TODO: support variable parameters in suspendable functions");
				} else {
					paramStackMembers.push_back(co_await translateType(var->type));
				}
			}
		}

		auto paramStackType = LLVMStructTypeInContext(_llcontext.get(), paramStackMembers.data(), paramStackMembers.size(), false);
		auto paramStackSize = LLVMStoreSizeOfType(_targetData.get(), paramStackType);
		size_t paramStackIndex = 1;

		if (suspendableContext) {
			buildSuspendablePushStack(_builders.top(), suspendableContext, paramStackSize);
		}

		if (info->function->isMethod) {
			auto selfParam = LLVMGetParam(llfunc, 0);
			LLVMSetValueName(selfParam, "@this");

			if (suspendableContext) {
				auto alloca = buildSuspendableAlloca(_builders.top(), suspendableContext);
				updateSuspendableAlloca(alloca, LLVMOffsetOfElement(_targetData.get(), paramStackType, paramStackIndex++));
				LLVMBuildStore(_builders.top().get(), selfParam, alloca);
			} else {
				_thisContextValue.push(selfParam);
			}

			if (!suspendableContext) {
				auto tmpThis = co_await tmpify(selfParam, info->function->parentClassType, false, true);

				auto thisDebugType = LLVMDIBuilderCreateObjectPointerType(_debugBuilder.get(), translateTypeDebug(info->function->parentClassType));
				auto thisDebugVar = LLVMDIBuilderCreateParameterVariable(_debugBuilder.get(), translateScope(info->function->scope), "this", sizeof("this") - 1, 1, debugFileForScopeItem(info->function), node->position.line, thisDebugType, false, LLVMDIFlagZero);
				auto thisDebugLoc = LLVMDIBuilderCreateDebugLocation(_llcontext.get(), node->position.line, node->position.column, translateScope(info->function->scope), NULL);

				LLVMDIBuilderInsertDeclareAtEnd(_debugBuilder.get(), tmpThis, thisDebugVar, LLVMDIBuilderCreateExpression(_debugBuilder.get(), NULL, 0), thisDebugLoc, LLVMGetInsertBlock(_builders.top().get()));
			}
		}

		size_t llparamIndex = info->function->isMethod ? 1 : 0;
		for (size_t i = 0; i < info->parameters.size(); ++i) {
			const auto& param = node->parameters[i];
			const auto& paramInfo = info->parameters[i];
			const auto& var = info->function->parameterVariables[i];

			auto llparam = LLVMGetParam(llfunc, llparamIndex);
			++llparamIndex;

			auto paramName = (param->isVariable ? "@len_" : "") + param->name;
			auto paramNameDebug = std::regex_replace(paramName, std::regex("@"), "__");

			LLVMSetValueName(llparam, paramName.c_str());

			auto paramTypeDebug = param->isVariable ? LLVMDIBuilderCreateBasicType(_debugBuilder.get(), "u64", 3, 64, llvm::dwarf::DW_ATE_unsigned, LLVMDIFlagZero) : translateTypeDebug(var->type);
			auto llparamDebug = LLVMDIBuilderCreateParameterVariable(_debugBuilder.get(), translateScope(info->function->scope), paramNameDebug.c_str(), paramNameDebug.size(), llparamIndex, debugFileForScope(info->function->scope), param->position.line, paramTypeDebug, false, LLVMDIFlagZero);

			auto paramLoc = LLVMDIBuilderCreateDebugLocation(_llcontext.get(), param->position.line, param->position.column, translateScope(info->function->scope), NULL);

			LLVMValueRef llparamLen = NULL;

			if (param->isVariable) {
				// TODO: support variable parameters in suspendable functions
				assert(!suspendableContext);

				auto tmp = suspendableContext ? NULL : co_await tmpify(llparam, std::make_shared<AltaCore::DET::Type>(AltaCore::DET::NativeType::Integer, std::vector<uint8_t> { static_cast<uint8_t>(AltaCore::DET::TypeModifierFlag::Long), static_cast<uint8_t>(AltaCore::DET::TypeModifierFlag::Long) }), false, true);

				if (!suspendableContext) {
					LLVMDIBuilderInsertDeclareAtEnd(_debugBuilder.get(), tmp, llparamDebug, LLVMDIBuilderCreateExpression(_debugBuilder.get(), NULL, 0), paramLoc, entryBlock);
				}

				llparamLen = llparam;
				llparam = LLVMGetParam(llfunc, llparamIndex);
				++llparamIndex;

				LLVMSetValueName(llparam, param->name.c_str());

				paramNameDebug = std::regex_replace(param->name, std::regex("@"), "__");

				if (!suspendableContext) {
					auto elmDebugType = translateTypeDebug(var->type->followBlindly());
					paramTypeDebug = LLVMDIBuilderCreateArrayType(_debugBuilder.get(), 0, LLVMDITypeGetAlignInBits(elmDebugType), elmDebugType, NULL, 0);
					llparamDebug = LLVMDIBuilderCreateParameterVariable(_debugBuilder.get(), translateScope(info->function->scope), paramNameDebug.c_str(), paramNameDebug.size(), llparamIndex, debugFileForScope(info->function->scope), param->position.line, paramTypeDebug, false, LLVMDIFlagZero);
				}
			}

			if (!param->isVariable) {
				if (suspendableContext) {
					auto alloca = buildSuspendableAlloca(_builders.top(), suspendableContext);
					updateSuspendableAlloca(alloca, LLVMOffsetOfElement(_targetData.get(), paramStackType, paramStackIndex++));
					LLVMBuildStore(_builders.top().get(), llparam, alloca);
				} else {
					llparam = co_await tmpify(llparam, var->type, false, true);
					currentStack().pushItem(llparam, var->type);
					LLVMSetValueName(llparam, (param->name + "@on_stack").c_str());
				}
			} else if (param->isVariable) {
				// TODO: support variable parameters in suspendable functions
				assert(!suspendableContext);
				currentStack().pushRuntimeArray(llparam, llparamLen, var->type, LLVMInt64TypeInContext(_llcontext.get()));
			}

			if (!suspendableContext) {
				LLVMDIBuilderInsertDeclareAtEnd(_debugBuilder.get(), llparam, llparamDebug, LLVMDIBuilderCreateExpression(_debugBuilder.get(), NULL, 0), paramLoc, entryBlock);
			}

			if (!suspendableContext) {
				_definedVariables[var->id] = llparam;
				if (llparamLen) {
					_definedVariables["_Alta_array_length_" + var->id] = llparamLen;
				}
			}
		}

		if (suspendableContext) {
			auto load = LLVMBuildLoad2(_builders.top().get(), llclass, suspendableContext, "@ret_context");
			LLVMBuildRet(_builders.top().get(), load);
			popDebugLocation();
			co_await popStack();

			// now build the actual suspendable function

			nextFunc = std::dynamic_pointer_cast<AltaCore::DET::Function>(genClass->scope->items[info->isGenerator ? 2 : 3]);

			mangled = mangleName(nextFunc);
			auto [newllfuncType, newllfunc] = co_await declareFunction(nextFunc);
			llfuncType = newllfuncType;
			llfunc = newllfunc;

			llfuncDebug = translateFunction(nextFunc, true);
			LLVMSetSubprogram(llfunc, llfuncDebug);

			LLVMSetLinkage(llfunc, LLVMInternalLinkage);

			entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "@entry");
			LLVMPositionBuilderAtEnd(builder.get(), entryBlock);

			suspendableContext = LLVMGetParam(llfunc, 0);

			pushUnknownDebugLocation(nextFunc->scope);

			_overrideFuncScopes.emplace(info->function->id, llfuncDebug);

			auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
			auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());

			// check if suspendable function is done
			std::array<LLVMValueRef, 4> doneGEPIndices {
				LLVMConstInt(gepIndexType, 0, false), // first element in the "array"
				LLVMConstInt(gepStructIndexType, 0, false), // @Suspendable@::basic_context
				LLVMConstInt(gepStructIndexType, 0, false), // _Alta_basic_{coroutine,generator}::basic_suspendable
				LLVMConstInt(gepStructIndexType, 2, false), // _Alta_basic_suspendable::done
			};
			auto doneGEP = LLVMBuildGEP2(_builders.top().get(), llclass, suspendableContext, doneGEPIndices.data(), doneGEPIndices.size(), "@suspendable_is_done_ref");
			auto done = LLVMBuildLoad2(_builders.top().get(), LLVMInt1TypeInContext(_llcontext.get()), doneGEP, "@suspendable_is_done");

			auto exitNow = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "@done_exit");
			entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "@entry@continue");

			LLVMBuildCondBr(_builders.top().get(), done, exitNow, entryBlock);

			LLVMPositionBuilderAtEnd(_builders.top().get(), exitNow);

			if (info->coroutine) {
				LLVMBuildRetVoid(_builders.top().get());
			} else {
				std::array<LLVMValueRef, 2> vals;
				vals[0] = LLVMConstInt(LLVMInt1TypeInContext(_llcontext.get()), 0, false);
				vals[1] = LLVMGetPoison(co_await translateType(info->function->generatorReturnType));
				auto expr = LLVMConstNamedStruct(co_await translateType(info->function->generatorReturnType->makeOptional()), vals.data(), vals.size());
				LLVMBuildRet(_builders.top().get(), expr);
			}

			LLVMPositionBuilderAtEnd(_builders.top().get(), entryBlock);

			buildSuspendableReload(_builders.top(), suspendableContext);

			paramStackIndex = 1;

			_suspendableContext.push(suspendableContext);
			_suspendableStateIndex.push(0);
			_suspendableClass.push(genClass);

			auto dtor = (co_await declareFunction(genClass->destructor)).second;

			_suspendableDestructor.push(SuspendableDestructor {
				.function = dtor,
				.builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get())),
				.entry = LLVMGetFirstBasicBlock(dtor),
				.exit = LLVMGetLastBasicBlock(dtor),
				.stateValue = NULL, // TODO
				.stateUnwindBlocks = {},
			});

			if (info->function->isMethod) {
				auto selfParam = buildSuspendableAlloca(_builders.top(), suspendableContext);
				updateSuspendableAlloca(selfParam, LLVMOffsetOfElement(_targetData.get(), paramStackType, paramStackIndex++));
				// "this" is a pointer to the class instance; we don't want a pointer to a pointer
				selfParam = LLVMBuildLoad2(_builders.top().get(), co_await translateType(info->function->parentClassType), selfParam, "@this");
				_thisContextValue.push(selfParam);
			}

			for (size_t i = 0; i < info->parameters.size(); ++i) {
				const auto& param = node->parameters[i];
				const auto& paramInfo = info->parameters[i];
				const auto& var = info->function->parameterVariables[i];

				// TODO: support variable parameters in suspendable functions
				assert(!param->isVariable);

				auto llparam = buildSuspendableAlloca(_builders.top(), suspendableContext);
				updateSuspendableAlloca(llparam, LLVMOffsetOfElement(_targetData.get(), paramStackType, paramStackIndex++));

				LLVMSetValueName(llparam, (param->name + "@on_stack").c_str());

				_definedVariables[var->id] = llparam;
			}

			// we'll come back to the entry block after we've filled out the body of the function
			entryBlock = LLVMGetInsertBlock(_builders.top().get());

			auto initialStateBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "@state0");
			LLVMPositionBuilderAtEnd(builder.get(), initialStateBlock);

			_suspendableStateBlocks.emplace();
			_suspendableStateBlocks.top()[0] = initialStateBlock;

			pushStack(ScopeStack::Type::Function);

			for (size_t i = 0; i < info->parameters.size(); ++i) {
				const auto& param = node->parameters[i];
				const auto& paramInfo = info->parameters[i];
				const auto& var = info->function->parameterVariables[i];

				currentStack().pushItem(_definedVariables[var->id], var->type);
			}
		}

		pushDebugLocation(node->body->position, nextFunc ? nextFunc->scope : info->function->scope);

		co_await compileNode(node->body, info->body);

		popDebugLocation();

		auto functionReturnType = info->function->isGenerator ? info->function->generatorReturnType : (info->function->isAsync ? info->function->coroutineReturnType : info->function->returnType);

		if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(_builders.top().get()))) {
			if (suspendableContext) {
				auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
				auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());

				// mark the suspendable function as "done"
				std::array<LLVMValueRef, 4> gepIndices {
					LLVMConstInt(gepIndexType, 0, false), // first element in the "array"
					LLVMConstInt(gepStructIndexType, 0, false), // @Suspendable@::basic_context
					LLVMConstInt(gepStructIndexType, 0, false), // _Alta_basic_{coroutine,generator}::basic_suspendable
					LLVMConstInt(gepStructIndexType, 2, false), // _Alta_basic_suspendable::done
				};
				auto gep = LLVMBuildGEP2(_builders.top().get(), llclass, suspendableContext, gepIndices.data(), gepIndices.size(), "@suspendable_done_ref");
				LLVMBuildStore(_builders.top().get(), LLVMConstInt(LLVMInt1TypeInContext(_llcontext.get()), 1, false), gep);
			}

			if (info->function->isGenerator) {
				// an implicit return in a generator means that it has been exhausted and the final iteration did not return a value.
				// therefore, we return a null optional.

				co_await currentStack().cleanup();
				buildSuspendablePopStack(_builders.top(), suspendableContext);

				std::array<LLVMValueRef, 2> vals;
				vals[0] = LLVMConstInt(LLVMInt1TypeInContext(_llcontext.get()), 0, false);
				vals[1] = LLVMGetPoison(co_await translateType(functionReturnType));
				auto expr = LLVMConstNamedStruct(co_await translateType(functionReturnType->makeOptional()), vals.data(), vals.size());
				LLVMBuildRet(_builders.top().get(), expr);
			} else if (functionReturnType && *functionReturnType == AltaCore::DET::Type(AltaCore::DET::NativeType::Void)) {
				// insert an implicit return
				co_await currentStack().cleanup();

				if (suspendableContext) {
					buildSuspendablePopStack(_builders.top(), suspendableContext);
				}

				LLVMBuildRetVoid(_builders.top().get());
			} else {
				LLVMBuildUnreachable(_builders.top().get());
			}
		}

		if (suspendableContext) {
			auto badState = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "@bad_state");
			LLVMPositionBuilderAtEnd(_builders.top().get(), badState);
			buildBadSuspendableState(_builders.top(), suspendableContext);

			// alright, now let's go back to the entry block to set up the state switch
			LLVMPositionBuilderAtEnd(builder.get(), entryBlock);

			auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
			auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());

			std::array<LLVMValueRef, 4> gepIndices {
				LLVMConstInt(gepIndexType, 0, false), // the first element of the "array"
				LLVMConstInt(gepStructIndexType, 0, false), // @Suspendable@::basic_context
				LLVMConstInt(gepStructIndexType, 0, false), // _Alta_basic_{coroutine,generator}::basic_suspendable
				LLVMConstInt(gepStructIndexType, 1, false), // _Alta_basic_suspendable::state
			};
			auto stateIndexGEP = LLVMBuildGEP2(_builders.top().get(), llclass, suspendableContext, gepIndices.data(), gepIndices.size(), "@state_index_ref");
			auto stateIndexLoad = LLVMBuildLoad2(_builders.top().get(), LLVMInt64TypeInContext(_llcontext.get()), stateIndexGEP, "@state_index");

			auto switchInstr = LLVMBuildSwitch(_builders.top().get(), stateIndexLoad, badState, _suspendableStateBlocks.size());
			for (const auto& [idx, block]: _suspendableStateBlocks.top()) {
				LLVMAddCase(switchInstr, LLVMConstInt(LLVMInt64TypeInContext(_llcontext.get()), idx, false), block);
			}
		}

		popDebugLocation();
		co_await popStack();
		_builders.pop();

		if (info->function->isMethod) {
			_thisContextValue.pop();
		}

		if (suspendableContext) {
			_suspendableContext.pop();

			// now let's finish building the destructor
			_builders.push(_suspendableDestructor.top().builder);

			suspendableContext = LLVMGetParam(_suspendableDestructor.top().function, 0);

			auto badBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), _suspendableDestructor.top().function, "@bad_state");
			LLVMPositionBuilderAtEnd(_builders.top().get(), badBlock);
			buildBadSuspendableState(_builders.top(), suspendableContext);

			LLVMPositionBuilderAtEnd(_builders.top().get(), _suspendableDestructor.top().entry);

			pushStack(ScopeStack::Type::Function);

			auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
			auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());

			std::array<LLVMValueRef, 4> doneGEPIndices {
				LLVMConstInt(gepIndexType, 0, false), // first element in the "array"
				LLVMConstInt(gepStructIndexType, 0, false), // @Suspendable@::basic_context
				LLVMConstInt(gepStructIndexType, 0, false), // _Alta_basic_{coroutine,generator}::basic_suspendable
				LLVMConstInt(gepStructIndexType, 2, false), // _Alta_basic_suspendable::done
			};
			auto doneGEP = LLVMBuildGEP2(_builders.top().get(), llclass, suspendableContext, doneGEPIndices.data(), doneGEPIndices.size(), "@suspendable_done_gep");
			auto done = LLVMBuildLoad2(_builders.top().get(), LLVMInt1TypeInContext(_llcontext.get()), doneGEP, "@suspendable_done");

			auto earlyExit = LLVMAppendBasicBlockInContext(_llcontext.get(), _suspendableDestructor.top().function, "@early_exit");
			auto continueDtor = LLVMAppendBasicBlockInContext(_llcontext.get(), _suspendableDestructor.top().function, "@continue_dtor");

			LLVMBuildCondBr(_builders.top().get(), done, earlyExit, continueDtor);

			LLVMPositionBuilderAtEnd(_builders.top().get(), earlyExit);

			if (genClass->suspendableInput || genClass->suspendableOutput) {
				std::array<LLVMValueRef, 2> storedValueGEPIndices {
					LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
					LLVMConstInt(gepStructIndexType, 1, false), // @Suspendable@::suspendable_input or @Suspendable@::suspendable_output
				};
				auto inputGEP = LLVMBuildGEP2(_builders.top().get(), llclass, suspendableContext, storedValueGEPIndices.data(), storedValueGEPIndices.size(), "@suspendable_stored_value_gep");
				auto maybeType = genClass->suspendableInput ? genClass->suspendableInput->makeOptional() : genClass->suspendableOutput->makeOptional();
				co_await doDtor(inputGEP, maybeType);
				LLVMBuildStore(_builders.top().get(), LLVMConstNull(co_await translateType(maybeType)), inputGEP);
			}

			LLVMBuildRetVoid(_builders.top().get());

			LLVMPositionBuilderAtEnd(_builders.top().get(), continueDtor);

			std::array<LLVMValueRef, 4> gepIndices {
				LLVMConstInt(gepIndexType, 0, false), // first element in the "array"
				LLVMConstInt(gepStructIndexType, 0, false), // @Suspendable@::basic_context
				LLVMConstInt(gepStructIndexType, 0, false), // _Alta_basic_{coroutine,generator}::basic_suspendable
				LLVMConstInt(gepStructIndexType, 1, false), // _Alta_basic_suspendable::state
			};
			auto gep = LLVMBuildGEP2(_builders.top().get(), llclass, suspendableContext, gepIndices.data(), gepIndices.size(), "@suspendable_state_gep");
			auto stateIndex = LLVMBuildLoad2(_builders.top().get(), LLVMInt64TypeInContext(_llcontext.get()), gep, "@suspendable_state");

			auto switchInstr = LLVMBuildSwitch(_builders.top().get(), stateIndex, badBlock, _suspendableDestructor.top().stateUnwindBlocks.size() + 1);

			LLVMAddCase(switchInstr, LLVMConstInt(LLVMInt64TypeInContext(_llcontext.get()), 0, false), _suspendableDestructor.top().exit);

			for (const auto& [idx, unwindBlock]: _suspendableDestructor.top().stateUnwindBlocks) {
				LLVMAddCase(switchInstr, LLVMConstInt(LLVMInt64TypeInContext(_llcontext.get()), idx, false), unwindBlock);
			}

			LLVMPositionBuilderAtEnd(_builders.top().get(), _suspendableDestructor.top().exit);

			for (size_t i_plus_one = info->parameters.size(); i_plus_one > 0; --i_plus_one) {
				auto i = i_plus_one - 1;

				const auto& param = node->parameters[i];
				const auto& paramInfo = info->parameters[i];
				const auto& var = info->function->parameterVariables[i];

				// TODO: support variable parameters in suspendable functions
				assert(!param->isVariable);

				auto llparam = buildSuspendableAlloca(_builders.top(), suspendableContext);
				updateSuspendableAlloca(llparam, LLVMOffsetOfElement(_targetData.get(), paramStackType, paramStackIndex++));

				co_await doDtor(llparam, var->type);
			}

			buildSuspendablePopStack(_builders.top(), suspendableContext);

			co_await currentStack().cleanup();
			co_await popStack();

			LLVMBuildBr(_builders.top().get(), earlyExit);

			_builders.pop();
		}

		if (suspendableContext) {
			_suspendableStateBlocks.pop();
			_suspendableStateIndex.pop();
			_suspendableClass.pop();
			_suspendableDestructor.pop();
			_overrideFuncScopes.pop();
		}

		_suspendableContext.pop();

		for (const auto& [variant, optionalValueProvided]: info->optionalVariantFunctions) {
			std::vector<LLVMTypeRef> llparams;

			if (info->function->isMethod) {
				llparams.push_back(co_await translateType(info->function->parentClassType));
			}

			size_t optionalIndex = 0;
			size_t variantIndex = 0;
			for (size_t i = 0; i < info->parameters.size(); ++i) {
				const auto& param = node->parameters[i];
				const auto& paramInfo = info->parameters[i];

				if (paramInfo->defaultValue && !optionalValueProvided[optionalIndex++]) {
					continue;
				}

				const auto& var = variant->parameterVariables[variantIndex];
				auto type = paramInfo->type->type;
				auto lltype = co_await translateType(type);

				if (param->isVariable) {
					llparams.push_back(LLVMInt64TypeInContext(_llcontext.get()));
					llparams.push_back(LLVMPointerType(lltype, 0));
				} else {
					llparams.push_back(lltype);
				}

				++variantIndex;
			}

			auto optionalFuncType = LLVMFunctionType(retType, llparams.data(), llparams.size(), false);
			auto [llfuncType, llfunc] = co_await declareFunction(variant, optionalFuncType);

			auto llfuncDebug = translateFunction(variant, true);
			LLVMSetSubprogram(llfunc, llfuncDebug);

			auto entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "@entry");
			auto builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
			LLVMPositionBuilderAtEnd(builder.get(), entryBlock);

			_builders.push(builder);
			pushStack(ScopeStack::Type::Function);
			pushUnknownDebugLocation(variant->scope);

			std::vector<LLVMValueRef> args;

			if (info->function->isMethod) {
				auto selfParam = LLVMGetParam(llfunc, 0);
				args.push_back(selfParam);
				LLVMSetValueName(selfParam, "@this");

				auto tmpThis = co_await tmpify(selfParam, info->function->parentClassType, false, true);

				auto thisDebugType = LLVMDIBuilderCreateObjectPointerType(_debugBuilder.get(), translateTypeDebug(info->function->parentClassType));
				auto thisDebugVar = LLVMDIBuilderCreateParameterVariable(_debugBuilder.get(), translateScope(variant->scope), "this", sizeof("this") - 1, 1, debugFileForScopeItem(variant), node->position.line, thisDebugType, false, LLVMDIFlagZero);
				auto thisDebugLoc = LLVMDIBuilderCreateDebugLocation(_llcontext.get(), node->position.line, node->position.column, translateScope(variant->scope), NULL);

				LLVMDIBuilderInsertDeclareAtEnd(_debugBuilder.get(), tmpThis, thisDebugVar, LLVMDIBuilderCreateExpression(_debugBuilder.get(), NULL, 0), thisDebugLoc, LLVMGetInsertBlock(_builders.top().get()));
			}

			optionalIndex = 0;
			variantIndex = 0;

			for (size_t i = 0; i < info->parameters.size(); ++i) {
				const auto& paramInfo = info->parameters[i];

				if (paramInfo->defaultValue && !optionalValueProvided[optionalIndex++]) {
					auto compiled = co_await compileNode(node->parameters[i]->defaultValue, paramInfo->defaultValue);
					auto casted = co_await cast(compiled, AltaCore::DET::Type::getUnderlyingType(paramInfo->defaultValue.get()), paramInfo->type->type, false, additionalCopyInfo(node->parameters[i]->defaultValue, paramInfo->defaultValue), false, &node->parameters[i]->defaultValue->position);
					args.push_back(casted);
				} else {
					auto llparam = LLVMGetParam(llfunc, variantIndex + (info->function->isMethod ? 1 : 0));
					LLVMSetValueName(llparam, node->parameters[i]->name.c_str());
					args.push_back(llparam);
					++variantIndex;
				}
			}

			bool isVoid = *functionReturnType == AltaCore::DET::Type(AltaCore::DET::NativeType::Void);

			auto call = LLVMBuildCall2(_builders.top().get(), origLLFuncType, origLLFunc, args.data(), args.size(), isVoid ? "" : "@default_call");

			co_await currentStack().cleanup();

			if (isVoid) {
				LLVMBuildRetVoid(_builders.top().get());
			} else {
				LLVMBuildRet(_builders.top().get(), call);
			}

			popDebugLocation();
			co_await popStack();
			_builders.pop();
		}

		if (info->function->generatorParameterType) {
			// build the version that accepts a generator parameter
			auto nextFuncWithParam = std::dynamic_pointer_cast<AltaCore::DET::Function>(info->generator->scope->items[3]);
			auto [llfuncType, llfunc] = co_await declareFunction(nextFuncWithParam);

			auto entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "@entry");
			auto builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
			LLVMPositionBuilderAtEnd(builder.get(), entryBlock);

			_builders.push(builder);

			suspendableContext = LLVMGetParam(llfunc, 0);
			auto val = LLVMGetParam(llfunc, 1);

			auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
			auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());

			std::array<LLVMValueRef, 2> inputGEPIndices {
				LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
				LLVMConstInt(gepStructIndexType, 1, false), // @Suspendable@::suspendable_input
			};
			auto inputGEP = LLVMBuildGEP2(_builders.top().get(), llclass, suspendableContext, inputGEPIndices.data(), inputGEPIndices.size(), "@generator_input_ref");

			auto wrapped = co_await cast(val, info->function->generatorParameterType, info->function->generatorParameterType->makeOptional(), false, std::make_pair(false, false), false, &nextFuncWithParam->position);

			LLVMBuildStore(_builders.top().get(), wrapped, inputGEP);

			auto [llfuncTypeOrigNext, llfuncOrigNext] = co_await declareFunction(nextFunc);

			std::array<LLVMValueRef, 1> args {
				suspendableContext,
			};

			auto result = LLVMBuildCall2(_builders.top().get(), llfuncTypeOrigNext, llfuncOrigNext, args.data(), args.size(), "@result");
			LLVMBuildRet(_builders.top().get(), result);

			_builders.pop();
		}

		if (info->coroutine) {
			// build the value getter
			auto valueGetter = std::dynamic_pointer_cast<AltaCore::DET::Function>(info->coroutine->scope->items[2]);
			auto [llfuncType, llfunc] = co_await declareFunction(valueGetter);

			auto entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "@entry");
			auto builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
			LLVMPositionBuilderAtEnd(builder.get(), entryBlock);

			_builders.push(builder);

			suspendableContext = LLVMGetParam(llfunc, 0);

			pushStack(ScopeStack::Type::Temporary);

			auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
			auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());

			std::array<LLVMValueRef, 2> gepIndices {
				LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
				LLVMConstInt(gepStructIndexType, 1, false), // @Suspendable@::suspendable_output
			};
			auto gep = LLVMBuildGEP2(_builders.top().get(), llclass, suspendableContext, gepIndices.data(), gepIndices.size(), "@coroutine_output_ref");

			bool didCopy = false;
			auto copy = co_await doCopyCtor(gep, valueGetter->returnType->reference(), std::make_pair(true, false), &didCopy);
			if (!didCopy) {
				copy = LLVMBuildLoad2(_builders.top().get(), co_await translateType(valueGetter->returnType, true, false), copy, "@output_val_load");
			}

			co_await popStack();

			LLVMBuildRet(_builders.top().get(), copy);

			_builders.pop();
		}

		if (suspendableContext) {
			// build the `done` getter
			auto doneGetter = std::dynamic_pointer_cast<AltaCore::DET::Function>(genClass->scope->items[1]);
			auto [llfuncType, llfunc] = co_await declareFunction(doneGetter);

			auto entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "@entry");
			auto builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
			LLVMPositionBuilderAtEnd(builder.get(), entryBlock);

			_builders.push(builder);

			suspendableContext = LLVMGetParam(llfunc, 0);

			auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
			auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());

			std::array<LLVMValueRef, 4> gepIndices {
				LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
				LLVMConstInt(gepStructIndexType, 0, false), // @Suspendable@::basic_context
				LLVMConstInt(gepStructIndexType, 0, false), // _Alta_basic_{coroutine,generator}::basic_suspendable
				LLVMConstInt(gepStructIndexType, 2, false), // _Alta_basic_suspendable::done
			};
			auto gep = LLVMBuildGEP2(_builders.top().get(), llclass, suspendableContext, gepIndices.data(), gepIndices.size(), "@suspendable_done_ref");

			auto done = LLVMBuildLoad2(_builders.top().get(), LLVMInt1TypeInContext(_llcontext.get()), gep, "@suspendable_done");

			LLVMBuildRet(_builders.top().get(), done);

			_builders.pop();
		}
	}

	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileReturnDirectiveNode(std::shared_ptr<AltaCore::AST::ReturnDirectiveNode> node, std::shared_ptr<AltaCore::DH::ReturnDirectiveNode> info) {
	LLVMValueRef expr = NULL;
	auto exprType = node->expression ? AltaCore::DET::Type::getUnderlyingType(info->expression.get()) : nullptr;

	pushDebugLocation(node->position, info->inputScope);

	auto functionReturnType = info->parentFunction ? (info->parentFunction->isGenerator ? info->parentFunction->generatorReturnType : (info->parentFunction->isAsync ? info->parentFunction->coroutineReturnType : info->parentFunction->returnType)) : nullptr;
	bool inGenerator = info->parentFunction && info->parentFunction->generatorReturnType;
	bool inCoroutine = info->parentFunction && info->parentFunction->coroutineReturnType;

	if (node->expression) {
		pushStack(ScopeStack::Type::Temporary);
		expr = co_await compileNode(node->expression, info->expression);
		co_await currentStack().cleanup();
		co_await popStack();

		bool directWrap = info->parentFunction && info->parentFunction->isGenerator;

		if (functionReturnType && functionReturnType->referenceLevel() > 0) {
			// if we're returning a reference, there's no need to copy anything

			if (directWrap) {
				// wrap it in an optional
				expr = co_await cast(expr, exprType, functionReturnType->makeOptional(), false, std::make_pair(false, false), false, &node->position);
			}
		} else {
			expr = co_await cast(expr, exprType, directWrap ? functionReturnType->makeOptional() : functionReturnType, true, additionalCopyInfo(node->expression, info->expression), false, &node->expression->position);
		}

		if (inSuspendableFunction() && !directWrap) {
			// wrap it in an optional
			expr = co_await cast(expr, functionReturnType, functionReturnType->makeOptional(), false, std::make_pair(false, false), false, &node->position);
		}
	} else if (inGenerator) {
		// generators *must* return a value; an empty return statement can be used to mark the generator as done without returning a value.
		// what we do instead is we produce a null optional
		std::array<LLVMValueRef, 2> vals;
		vals[0] = LLVMConstInt(LLVMInt1TypeInContext(_llcontext.get()), 0, false);
		vals[1] = LLVMGetPoison(co_await translateType(functionReturnType));
		expr = LLVMConstNamedStruct(co_await translateType(functionReturnType->makeOptional()), vals.data(), vals.size());
	}

	for (auto rit = _stacks.rbegin(); rit != _stacks.rend(); ++rit) {
		auto& stack = *rit;

		co_await stack.cleanup();

		if (stack.type == ScopeStack::Type::Function) {
			break;
		}
	}

	if (inSuspendableFunction()) {
		// make sure to pop off the parameter stack
		buildSuspendablePopStack(_builders.top(), _suspendableContext.top());

		auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
		auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());

		if (inCoroutine) {
			// coroutines don't return their values directly; they store them in an instance variable
			std::array<LLVMValueRef, 2> gepIndices {
				LLVMConstInt(gepIndexType, 0, false), // first element in the "array"
				LLVMConstInt(gepStructIndexType, 1, false), // @Coroutine@::suspendable_output
			};
			auto gep = LLVMBuildGEP2(_builders.top().get(), co_await translateType(info->parentFunction->returnType, true, false), currentSuspendableContext(), gepIndices.data(), gepIndices.size(), "@suspendable_output_ref");
			LLVMBuildStore(_builders.top().get(), expr, gep);
		}

		// also mark the suspendable function as "done"
		std::array<LLVMValueRef, 4> gepIndices {
			LLVMConstInt(gepIndexType, 0, false), // first element in the "array"
			LLVMConstInt(gepStructIndexType, 0, false), // @Suspendable@::basic_context
			LLVMConstInt(gepStructIndexType, 0, false), // _Alta_basic_{coroutine,generator}::basic_suspendable
			LLVMConstInt(gepStructIndexType, 2, false), // _Alta_basic_suspendable::done
		};
		auto gep = LLVMBuildGEP2(_builders.top().get(), co_await translateType(info->parentFunction->returnType, true, false), currentSuspendableContext(), gepIndices.data(), gepIndices.size(), "@suspendable_done_ref");
		LLVMBuildStore(_builders.top().get(), LLVMConstInt(LLVMInt1TypeInContext(_llcontext.get()), 1, false), gep);
	}

	if ((node->expression || inGenerator) && !inCoroutine) {
		if (!expr) {
			throw std::runtime_error("Invalid return value");
		}

		if (!inGenerator && *exprType == AltaCore::DET::Type(AltaCore::DET::NativeType::Void)) {
			LLVMBuildRetVoid(_builders.top().get());
		} else {
			LLVMBuildRet(_builders.top().get(), expr);
		}
	} else {
		LLVMBuildRetVoid(_builders.top().get());
	}

	popDebugLocation();

	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileIntegerLiteralNode(std::shared_ptr<AltaCore::AST::IntegerLiteralNode> node, std::shared_ptr<AltaCore::DH::IntegerLiteralNode> info) {
	auto type = AltaCore::DET::Type::getUnderlyingType(info.get());
	auto lltype = co_await translateType(type);
	co_return LLVMConstInt(lltype, node->integer, type->isSigned());
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileVariableDefinitionExpression(std::shared_ptr<AltaCore::AST::VariableDefinitionExpression> node, std::shared_ptr<AltaCore::DH::VariableDefinitionExpression> info) {
	bool inModuleRoot = !info->variable->parentScope.expired() && !info->variable->parentScope.lock()->parentModule.expired();
	auto lltype = co_await translateType(info->variable->type);
	auto mangled = mangleName(info->variable);
	LLVMValueRef memory = NULL;

	pushDebugLocation(node->position, info->inputScope);

	if (!_definedVariables[info->variable->id]) {
		if (inModuleRoot) {
			memory = LLVMAddGlobal(_llmod.get(), lltype, mangled.c_str());
			LLVMSetLinkage(memory, LLVMInternalLinkage);
		} else {
			memory = currentNonTemporaryStack().buildAlloca(lltype, mangled);
		}

		_definedVariables[info->variable->id] = memory;
	} else {
		memory = _definedVariables[info->variable->id];
	}

	if (inModuleRoot) {
		auto gve = LLVMDIBuilderCreateGlobalVariableExpression(_debugBuilder.get(), translateParentScope(info->variable), info->variable->name.c_str(), info->variable->name.size(), mangled.c_str(), mangled.size(), debugFileForScopeItem(info->variable), node->position.line, translateTypeDebug(info->variable->type), false, LLVMDIBuilderCreateExpression(_debugBuilder.get(), NULL, 0), NULL, 0);
		LLVMGlobalSetMetadata(memory, llvm::LLVMContext::MD_dbg, gve);
	} else {
		auto debugVar = _definedDebugVariables[info->variable->id] = LLVMDIBuilderCreateAutoVariable(_debugBuilder.get(), translateParentScope(info->variable), info->variable->name.c_str(), info->variable->name.size(), debugFileForScopeItem(info->variable), node->position.line, translateTypeDebug(info->variable->type), false, LLVMDIFlagZero, 0);
		LLVMDIBuilderInsertDeclareAtEnd(_debugBuilder.get(), memory, debugVar, LLVMDIBuilderCreateExpression(_debugBuilder.get(), NULL, 0), _debugLocations.top(), LLVMGetInsertBlock(_builders.top().get()));
	}

	if (!inModuleRoot && !_stacks.empty()) {
		currentNonTemporaryStack().pushItem(memory, info->variable->type);
	}

	if (node->initializationExpression) {
		auto exprType = AltaCore::DET::Type::getUnderlyingType(info->initializationExpression.get());

		if (inModuleRoot) {
			pushStack(ScopeStack::Type::Temporary);
			auto compiled = co_await compileNode(node->initializationExpression, info->initializationExpression);
			auto casted = co_await cast(compiled, exprType, info->variable->type, true, additionalCopyInfo(node->initializationExpression, info->initializationExpression), false, &node->initializationExpression->position);
			co_await currentStack().cleanup();
			co_await popStack();
			if (LLVMIsConstant(casted)) {
				LLVMSetInitializer(memory, casted);
				if (!(*info->variable->type == *info->variable->type->deconstify())) {
					LLVMSetGlobalConstant(memory, true);
				}
			} else {
				LLVMSetInitializer(memory, LLVMGetUndef(lltype));
				LLVMBuildStore(_builders.top().get(), casted, memory);
			}

			if (canDestroy(info->variable->type)) {
				enterDeinitFunction();

				pushStack(ScopeStack::Type::Temporary);
				pushDebugLocation(node->position, info->inputScope);
				co_await doDtor(memory, info->variable->type);
				co_await currentStack().cleanup();
				popDebugLocation();
				co_await popStack();

				exitDeinitFunction();
			}
		} else {
			pushStack(ScopeStack::Type::Temporary);
			auto compiled = co_await compileNode(node->initializationExpression, info->initializationExpression);
			auto casted = co_await cast(compiled, exprType, info->variable->type, true, additionalCopyInfo(node->initializationExpression, info->initializationExpression), false, &node->initializationExpression->position);
			LLVMBuildStore(_builders.top().get(), casted, memory);
			co_await currentStack().cleanup();
			co_await popStack();
		}
	} else if (!inModuleRoot && info->variable->type->klass && info->variable->type->indirectionLevel() == 0) {
		LLVMValueRef init = NULL;
		if (info->variable->type->klass->isBitfield || info->variable->type->klass->isStructure) {
			init = LLVMConstNull(co_await translateType(info->variable->type));
		} else {
			auto [llfuncType, llfunc] = co_await declareFunction(info->variable->type->klass->defaultConstructor);
			init = LLVMBuildCall2(_builders.top().get(), llfuncType, llfunc, nullptr, 0, ("@default_ctor_" + mangled).c_str());
		}
		LLVMBuildStore(_builders.top().get(), init, memory);
	} else {
		auto null = LLVMConstNull(lltype);
		if (inModuleRoot) {
			LLVMSetInitializer(memory, null);
			if (!(*info->variable->type == *info->variable->type->deconstify())) {
				LLVMSetGlobalConstant(memory, true);
			}
		} else {
			LLVMBuildStore(_builders.top().get(), null, memory);
		}
	}

	popDebugLocation();

	co_return memory;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileAccessor(std::shared_ptr<AltaCore::AST::Accessor> node, std::shared_ptr<AltaCore::DH::Accessor> info) {
	pushDebugLocation(node->position, info->inputScope);

	LLVMValueRef result = NULL;
	auto tmpIdx = nextTemp();
	auto tmpIdxStr = std::to_string(tmpIdx);

	if (info->getsVariableLength) {
		auto targetInfo = std::dynamic_pointer_cast<AltaCore::DH::Fetch>(info->target);
		popDebugLocation();
		co_return _definedVariables["_Alta_array_length_" + targetInfo->narrowedTo->id];
	} else if (info->narrowedTo) {
		if (info->accessesNamespace || (info->narrowedTo->nodeType() == AltaCore::DET::NodeType::Function && !std::dynamic_pointer_cast<AltaCore::DET::Function>(info->narrowedTo)->isMethod)) {
			result = _definedVariables[info->narrowedTo->id] ? _definedVariables[info->narrowedTo->id] : _definedFunctions[info->narrowedTo->id];

			if (info->narrowedTo->nodeType() == AltaCore::DET::NodeType::Function) {
				auto func = std::dynamic_pointer_cast<AltaCore::DET::Function>(info->narrowedTo);
				auto [llfuncType, llfunc] = co_await declareFunction(func);

				result = llfunc;

				if (func->isAccessor) {
					result = LLVMBuildCall2(_builders.top().get(), llfuncType, result, nullptr, 0, ("@getter_call_" + tmpIdxStr).c_str());
				}
			} else if (info->narrowedTo->nodeType() == AltaCore::DET::NodeType::Variable) {
				auto var = std::dynamic_pointer_cast<AltaCore::DET::Variable>(info->narrowedTo);
				auto varType = co_await translateType(AltaCore::DET::Type::getUnderlyingType(var)->destroyReferences());

				if (!result) {
					if (_definedVariables[var->id]) {
						throw std::runtime_error("This should be impossible");
					}

					bool ok = false;
					bool inModuleRoot = !var->parentScope.expired() && !var->parentScope.lock()->parentModule.expired();

					if (inModuleRoot) {
						ok = true;
					} else if (auto ns = AltaCore::Util::getEnum(var->parentScope.lock()).lock()) {
						ok = true;
					}

					if (!ok) {
						throw std::runtime_error("This should also be impossible");
					}

					auto mangled = mangleName(var);
					_definedVariables[var->id] = LLVMAddGlobal(_llmod.get(), varType, mangled.c_str());
					result = _definedVariables[var->id];
				}
			}

			popDebugLocation();
			co_return result;
		} else if (auto func = std::dynamic_pointer_cast<AltaCore::DET::Function>(info->narrowedTo)) {
			auto [llfuncType, llfunc] = co_await declareFunction(func);
			popDebugLocation();
			co_return llfunc;
		} else {
			auto parentFunc = AltaCore::Util::getFunction(info->inputScope).lock();

			if (parentFunc && parentFunc->isLambda) {
				for (const auto& var: parentFunc->copiedVariables) {
					if (var->id == info->narrowedTo->id) {
						result = _definedVariables["@lambda_copy@" + mangleName(parentFunc) + "@" + var->id];
						popDebugLocation();
						co_return result;
					}
				}
				for (const auto& var: parentFunc->referencedVariables) {
					if (var->id == info->narrowedTo->id) {
						result = _definedVariables["@lambda_ref@" + mangleName(parentFunc) + "@" + var->id];
						popDebugLocation();
						co_return result;
					}
				}
			}

			result = co_await tmpify(node->target, info->target);
		}
	} else if (info->readAccessor) {
		if (!info->accessesNamespace && info->readAccessor->isMethod) {
			result = co_await tmpify(node->target, info->target);
		}
	} else {
		throw std::runtime_error("This accessor needs to be narrowed");
	}

	auto handleParentAccessors = [&](const std::vector<std::pair<std::shared_ptr<AltaCore::DET::Class>, size_t>>& parents, std::shared_ptr<AltaCore::DET::Class> varClass, std::shared_ptr<AltaCore::DET::Class> sourceClass, LLVMValueRef result) -> LLCoroutine {
		auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
		auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());
		std::vector<LLVMValueRef> gepIndices {
			LLVMConstInt(gepIndexType, 0, false), // first index is for the "array" of instances we start with
		};

		for (const auto& [parent, index]: parents) {
			gepIndices.push_back(LLVMConstInt(gepStructIndexType, index + 1, false));
		}

		auto parentType = std::make_shared<AltaCore::DET::Type>(varClass)->reference();

		result = LLVMBuildGEP2(_builders.top().get(), co_await defineClassType(sourceClass), result, gepIndices.data(), gepIndices.size(), "");
		result = co_await getRealInstance(result, parentType);

		co_return result;
	};

	if (info->narrowedTo) {
		if (!info->targetType) {
			throw std::runtime_error("Must know target type to compile accessor");
		}

		if (info->targetType->indirectionLevel() > 0) {
			result = co_await loadIndirection(result, info->targetType, 1);
		}

		auto varClass = AltaCore::Util::getClass(info->narrowedTo->parentScope.lock()).lock();

		if (!varClass) {
			throw std::runtime_error("Invalid accessor (expected to be accessing a class/structure)");
		}

		if (info->parentClassAccessors.find(info->narrowedToIndex) != info->parentClassAccessors.end()) {
			result = co_await handleParentAccessors(info->parentClassAccessors[info->narrowedToIndex], varClass, info->targetType->klass, result);
		}

		if (info->targetType->bitfield) {
			auto field = std::dynamic_pointer_cast<AltaCore::DET::Variable>(info->narrowedTo);
			auto [start, end] = field->bitfieldBits;
			auto bitCount = (end - start) + 1;
			uint64_t mask = UINT64_MAX >> (64 - bitCount);
			auto underlyingType = co_await translateType(info->targetType->bitfield->underlyingBitfieldType.lock());

			_bitfieldAccessTargets[info->id] = result;

			result = co_await loadRef(result, info->targetType);
			result = LLVMBuildAnd(_builders.top().get(), result, LLVMConstInt(underlyingType, mask, false), ("@bitfield_access_" + tmpIdxStr + "_masked").c_str());
			result = LLVMBuildLShr(_builders.top().get(), result, LLVMConstInt(underlyingType, start, false), ("@bitfield_access_" + tmpIdxStr + "_shifted").c_str());
			result = LLVMBuildIntCast2(_builders.top().get(), result, co_await translateType(field->type->destroyIndirection()), field->type->isSigned(), ("@bitfield_access_" + tmpIdxStr).c_str());
		} else {
			// find the index of the variable
			size_t index = 0;

			for (; index < varClass->members.size(); ++index) {
				const auto& member = varClass->members[index];
				if (member->id == info->narrowedTo->id) {
					break;
				}
			}

			if (index == varClass->members.size()) {
				throw std::runtime_error("Invalid accessor (not retrieving class member?)");
			}

			auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
			auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());
			std::array<LLVMValueRef, 2> gepIndices {
				LLVMConstInt(gepIndexType, 0, false),     // access the first "array" item
				LLVMConstInt(gepStructIndexType, index + (varClass->isStructure ? 0 : 1) + varClass->parents.size(), false), // access the member
			};

			result = LLVMBuildGEP2(_builders.top().get(), co_await translateType(info->targetType->destroyIndirection()), result, gepIndices.data(), gepIndices.size(), ("@member_access_" + tmpIdxStr).c_str());
		}
	} else if (info->readAccessor) {
		// TODO
#if 0
		auto tmpVar = LLVMBuildAlloca(_builders.top().get(), co_await translateType(info->readAccessor->returnType), "");

		if (canPush(info->readAccessor->returnType)) {
			currentStack().pushItem(tmpVar, info->readAccessor->returnType);
		}
#endif

		std::vector<LLVMValueRef> args;

		if (!info->accessesNamespace && info->readAccessor->isMethod) {
			auto selfCopyInfo = additionalCopyInfo(node->target, info->target);
			auto selfType = AltaCore::DET::Type::getUnderlyingType(info->target.get());

			if (selfCopyInfo.second && node->target->nodeType() != AltaCore::AST::NodeType::FunctionCallExpression && node->target->nodeType() != AltaCore::AST::NodeType::AwaitExpression && node->target->nodeType() != AltaCore::AST::NodeType::YieldExpression) {
				result = co_await tmpify(result, selfType);
			}

			if (selfType->indirectionLevel() > 0) {
				result = co_await loadIndirection(result, selfType->reference(), 1);
			}

			auto accessorClass = AltaCore::Util::getClass(info->readAccessor->parentScope.lock()).lock();

			if (!accessorClass) {
				throw std::runtime_error("Invalid accessor (expected to be accessing a class/structure)");
			}

			if (info->parentClassAccessors.find(info->readAccessorIndex) != info->parentClassAccessors.end()) {
				result = co_await handleParentAccessors(info->parentClassAccessors[info->readAccessorIndex], accessorClass, selfType->klass, result);
			}

			if (info->readAccessor->isVirtual()) {
				auto type = std::make_shared<AltaCore::DET::Type>(accessorClass)->reference();
				result = co_await getRootInstance(result, type);
			}

			args.push_back(result);
		}

		LLVMTypeRef llfuncType = NULL;
		LLVMValueRef llfunc = NULL;

		if (info->readAccessor->isVirtual()) {
			llfuncType = co_await translateType(AltaCore::DET::Type::getUnderlyingType(info->readAccessor), false);
			llfunc = buildVirtualLookup(_builders.top(), args[0], mangleName(info->readAccessor, false));
		} else {
			std::tie(llfuncType, llfunc) = co_await declareFunction(info->readAccessor);
		}

		result = LLVMBuildCall2(_builders.top().get(), llfuncType, llfunc, args.data(), args.size(), ("@getter_call_" + tmpIdxStr).c_str());
	}

	if (info->isRootClassRetrieval) {
		info->isRootClassRetrieval = false;
		auto realType = AltaCore::DET::Type::getUnderlyingType(info.get());
		info->isRootClassRetrieval = true;
		if (realType->klass) {
			result = co_await getRootInstance(result, realType);
		}
	}

	if (!result) {
		throw std::runtime_error("Unable to determine accessor result");
	}

	popDebugLocation();
	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileFetch(std::shared_ptr<AltaCore::AST::Fetch> node, std::shared_ptr<AltaCore::DH::Fetch> info) {
	pushDebugLocation(node->position, info->inputScope);

	auto tmpIdx = nextTemp();
	auto tmpIdxStr = std::to_string(tmpIdx);

	if (!info->narrowedTo) {
		if (info->readAccessor) {
			auto [llfuncType, llfunc] = co_await declareFunction(info->readAccessor);
			popDebugLocation();
			co_return LLVMBuildCall2(_builders.top().get(), llfuncType, llfunc, nullptr, 0, ("@getter_call_" + tmpIdxStr).c_str());
		}

		throw std::runtime_error("Invalid fetch: must be narrowed");
	}

	auto parentFunc = AltaCore::Util::getFunction(info->inputScope).lock();

	if (parentFunc && parentFunc->isLambda) {
		for (const auto& var: parentFunc->copiedVariables) {
			if (var->id == info->narrowedTo->id) {
				popDebugLocation();
				co_return _definedVariables["@lambda_copy@" + mangleName(parentFunc) + "@" + var->id];
			}
		}
		for (const auto& var: parentFunc->referencedVariables) {
			if (var->id == info->narrowedTo->id) {
				popDebugLocation();
				co_return _definedVariables["@lambda_ref@" + mangleName(parentFunc) + "@" + var->id];
			}
		}
	}

	if (info->narrowedTo->nodeType() == AltaCore::DET::NodeType::Variable && info->narrowedTo->name == "this") {
		auto var = std::dynamic_pointer_cast<AltaCore::DET::Variable>(info->narrowedTo);
		if (!var->parentScope.expired() && !var->parentScope.lock()->parentClass.expired()) {
			if (_thisContextValue.empty()) {
				throw std::runtime_error("No `this` context?");
			}
			popDebugLocation();
			co_return _thisContextValue.top();
		}
	}

	auto result = _definedVariables[info->narrowedTo->id] ? _definedVariables[info->narrowedTo->id] : _definedFunctions[info->narrowedTo->id];

	if (info->narrowedTo->nodeType() == AltaCore::DET::NodeType::Function) {
		auto func = std::dynamic_pointer_cast<AltaCore::DET::Function>(info->narrowedTo);
		auto [llfuncType, llfunc] = co_await declareFunction(func);

		result = llfunc;

		if (func->isAccessor) {
			result = LLVMBuildCall2(_builders.top().get(), llfuncType, result, nullptr, 0, ("@getter_call_" + tmpIdxStr).c_str());
		}
	} else if (info->narrowedTo->nodeType() == AltaCore::DET::NodeType::Variable) {
		auto var = std::dynamic_pointer_cast<AltaCore::DET::Variable>(info->narrowedTo);
		auto varType = co_await translateType(var->type);

		if (!result) {
			if (_definedVariables[var->id]) {
				throw std::runtime_error("This should be impossible");
			}

			bool ok = false;
			bool inModuleRoot = !var->parentScope.expired() && !var->parentScope.lock()->parentModule.expired();

			if (inModuleRoot) {
				ok = true;
			} else if (auto ns = AltaCore::Util::getEnum(var->parentScope.lock()).lock()) {
				ok = true;
			}

			if (!ok) {
				throw std::runtime_error("This should also be impossible");
			}

			auto mangled = mangleName(var);
			_definedVariables[var->id] = LLVMAddGlobal(_llmod.get(), varType, mangled.c_str());
			result = _definedVariables[var->id];
		}

		if (var->type->referenceLevel() > 0) {
			result = LLVMBuildLoad2(_builders.top().get(), varType, result, ("@fetch_ref_" + tmpIdxStr).c_str());
		}
	}

	if (info->isRootClassRetrieval) {
		info->isRootClassRetrieval = false;
		auto realType = AltaCore::DET::Type::getUnderlyingType(info.get());
		info->isRootClassRetrieval = true;
		if (realType->klass) {
			result = co_await getRootInstance(result, realType);
		}
	}

	if (!result) {
		throw std::runtime_error("Unable to determine result for fetch");
	}

	popDebugLocation();
	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileAssignmentExpression(std::shared_ptr<AltaCore::AST::AssignmentExpression> node, std::shared_ptr<AltaCore::DH::AssignmentExpression> info) {
	pushDebugLocation(node->position, info->inputScope);

	auto lhs = co_await compileNode(node->target, info->target);
	LLVMValueRef result = NULL;
	auto tmpIdx = nextTemp();
	auto tmpIdxStr = std::to_string(tmpIdx);

	bool storedLHS = false;

	// evaluating RHS might cause a state change, so we need to store the LHS value
	if (inSuspendableFunction() && !LLVMIsConstant(lhs)) {
		auto alloca = currentStack().buildAlloca(co_await translateType(info->targetType), ("@assignment_lhs_save_" + tmpIdxStr).c_str());
		LLVMBuildStore(_builders.top().get(), lhs, alloca);
		lhs = alloca;
		storedLHS = true;
	}

	bool isNullopt = info->targetType->isOptional && info->targetType->pointerLevel() < 1 && node->value->nodeType() == AltaCore::AST::NodeType::NullptrExpression;
	bool canCopy = !isNullopt && (!info->targetType->isNative || !info->targetType->isRawFunction) && info->targetType->pointerLevel() < 1 && (!info->strict || info->targetType->dereference()->indirectionLevel() < 1) && (!info->targetType->klass || info->targetType->klass->copyConstructor);
	bool canDestroyVar = !info->operatorMethod && !info->strict && canDestroy(info->targetType, true);

	auto rhs = co_await compileNode(node->value, info->value);
	auto rhsType = AltaCore::DET::Type::getUnderlyingType(info->value.get());
	auto rhsTargetType = info->operatorMethod ? info->operatorMethod->parameterVariables.front()->type : (info->strict ? info->targetType->dereference() : info->targetType->destroyReferences());
	bool didRetrieval = false;

	if (storedLHS) {
		// now load the LHS value again (we've evaluated RHS, so we can't have another state change)
		lhs = LLVMBuildLoad2(_builders.top().get(), co_await translateType(info->targetType), lhs, ("@assignment_lhs_restore_" + tmpIdxStr).c_str());
	}

	if (auto acc = std::dynamic_pointer_cast<AltaCore::DH::Accessor>(info->target)) {
		if (auto var = std::dynamic_pointer_cast<AltaCore::DET::Variable>(acc->narrowedTo)) {
			if (var->isBitfieldEntry) {
				// "get operand" accesses the operand of the cast, so we're left with the `lshr`, which we then access so that we're left with the `and`
				auto bitfieldFetchMasked = LLVMGetOperand(LLVMGetOperand(lhs, 0), 0);
				auto [start, end] = var->bitfieldBits;
				auto bitCount = (end - start) + 1;
				uint64_t mask = UINT64_MAX >> (64 - bitCount);
				auto bitfieldClass = AltaCore::Util::getClass(var->parentScope.lock()).lock();
				auto underlyingType = co_await translateType(bitfieldClass->underlyingBitfieldType.lock());

				rhs = co_await loadRef(rhs, rhsType);

				auto extended = LLVMBuildIntCast2(_builders.top().get(), rhs, underlyingType, info->targetType->destroyIndirection()->isSigned(), ("@bitfield_assignment_" + tmpIdxStr + "_extended").c_str());
				result = LLVMBuildShl(_builders.top().get(), extended, LLVMConstInt(underlyingType, start, false), ("@bitfield_assignment_" + tmpIdxStr + "_shift").c_str());
				result = LLVMBuildOr(_builders.top().get(), result, bitfieldFetchMasked, ("@bitfield_assignment_" + tmpIdxStr).c_str());

				auto accessTarget = _bitfieldAccessTargets[acc->id];
				accessTarget = co_await loadRef(accessTarget, acc->targetType, 1);

				LLVMBuildStore(_builders.top().get(), result, accessTarget);

				popDebugLocation();
				co_return rhs;
			}
		}
	}

	if (isNullopt) {
		std::array<LLVMValueRef, 2> vals;
		vals[0] = LLVMConstInt(LLVMInt1TypeInContext(_llcontext.get()), 0, false);
		vals[1] = LLVMGetPoison(co_await translateType(info->targetType->optionalTarget));
		rhs = LLVMConstNamedStruct(co_await translateType(info->targetType->destroyIndirection()), vals.data(), vals.size());
	} else {
		rhs = co_await cast(rhs, rhsType, rhsTargetType, canCopy, additionalCopyInfo(node->value, info->value), false, &node->value->position);
	}

	if (!info->operatorMethod) {
		LLVMOpcode binop = static_cast<LLVMOpcode>(0);
		auto isFP = rhsTargetType->isFloatingPoint();
		auto isSigned = rhsTargetType->isSigned();

		switch (info->type) {
			case AltaCore::Shared::AssignmentType::Simple:         binop = LLVMStore; break;
			case AltaCore::Shared::AssignmentType::Addition:       binop = isFP ? LLVMFAdd : LLVMAdd; break;
			case AltaCore::Shared::AssignmentType::Subtraction:    binop = isFP ? LLVMFSub : LLVMSub; break;
			case AltaCore::Shared::AssignmentType::Multiplication: binop = isFP ? LLVMFMul : LLVMMul; break;
			case AltaCore::Shared::AssignmentType::Division:       binop = isFP ? LLVMFDiv : (isSigned ? LLVMSDiv : LLVMUDiv); break;
			case AltaCore::Shared::AssignmentType::Modulo:         binop = isFP ? LLVMFRem : (isSigned ? LLVMSRem : LLVMURem); break;
			case AltaCore::Shared::AssignmentType::LeftShift:      binop = LLVMShl; break;
			case AltaCore::Shared::AssignmentType::RightShift:     binop = isSigned ? LLVMAShr : LLVMLShr; break;
			case AltaCore::Shared::AssignmentType::BitwiseAnd:     binop = LLVMAnd; break;
			case AltaCore::Shared::AssignmentType::BitwiseOr:      binop = LLVMOr; break;
			case AltaCore::Shared::AssignmentType::BitwiseXor:     binop = LLVMXor; break;
		}

		if (binop == static_cast<LLVMOpcode>(0)) {
			throw std::runtime_error("Invalid assignment type");
		}

		if (binop != LLVMStore) {
			auto lhsDeref = co_await loadRef(lhs, info->targetType);
			rhs = LLVMBuildBinOp(_builders.top().get(), binop, lhsDeref, rhs, ("@assignment_operator_" + tmpIdxStr + "_binop").c_str());
		}
	}

	if (canDestroyVar) {
		co_await doDtor(co_await loadRef(lhs, info->targetType->dereference()), info->targetType->destroyReferences());
	}

	if (info->operatorMethod) {
		auto [llfuncType, llfunc] = co_await declareFunction(info->operatorMethod);

		std::array<LLVMValueRef, 2> args {
			co_await cast(lhs, info->targetType, info->operatorMethod->parentClassType, false, additionalCopyInfo(node->target, info->target), false, &node->target->position),
			rhs,
		};
		result = LLVMBuildCall2(_builders.top().get(), llfuncType, llfunc, args.data(), args.size(), ("@assignment_operator_" + tmpIdxStr).c_str());
	} else {
		result = LLVMBuildStore(_builders.top().get(), rhs, lhs);
		result = lhs; // the result of the assignment is a reference to the assignee
	}

	popDebugLocation();
	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileBooleanLiteralNode(std::shared_ptr<AltaCore::AST::BooleanLiteralNode> node, std::shared_ptr<AltaCore::DH::BooleanLiteralNode> info) {
	co_return LLVMConstInt(LLVMInt1TypeInContext(_llcontext.get()), node->value ? 1 : 0, false);
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileBinaryOperation(std::shared_ptr<AltaCore::AST::BinaryOperation> node, std::shared_ptr<AltaCore::DH::BinaryOperation> info) {
	pushDebugLocation(node->position, info->inputScope);

	bool shortCircuit = node->type == AltaCore::Shared::OperatorType::LogicalAnd || node->type == AltaCore::Shared::OperatorType::LogicalOr;

	auto lhs = co_await compileNode(node->left, info->left);
	auto rhs = shortCircuit ? NULL : co_await compileNode(node->right, info->right);
	LLVMValueRef result = NULL;
	auto tmpIdx = nextTemp();
	auto tmpIdxStr = std::to_string(tmpIdx);

	if (info->operatorMethod) {
		// if true, the `this` argument is the left-hand side of the expression
		bool thisIsLeftHand = info->operatorMethod->orientation == AltaCore::Shared::ClassOperatorOrientation::Left;

		auto instance = thisIsLeftHand ? lhs : rhs;
		auto instanceAlta = thisIsLeftHand ? node->left : node->right;
		auto instanceInfo = thisIsLeftHand ? info->left : info->right;
		auto instanceType = thisIsLeftHand ? info->leftType : info->rightType;

		auto arg = thisIsLeftHand ? rhs : lhs;
		auto argAlta = thisIsLeftHand ? node->right : node->left;
		auto argInfo = thisIsLeftHand ? info->right : info->left;
		auto argType = thisIsLeftHand ? info->rightType : info->leftType;

		if (!additionalCopyInfo(instanceAlta, instanceInfo).first) {
			instance = co_await tmpify(instance, instanceType);
		}

		arg = co_await cast(arg, argType, info->operatorMethod->parameterVariables.front()->type, true, additionalCopyInfo(argAlta, argInfo), false, &argAlta->position);

		auto [llfuncType, llfunc] = co_await declareFunction(info->operatorMethod);

		std::array<LLVMValueRef, 2> args {
			instance,
			arg,
		};

		result = LLVMBuildCall2(_builders.top().get(), llfuncType, llfunc, args.data(), args.size(), ("@binop_call_" + tmpIdxStr).c_str());
	} else if (shortCircuit) {
		// these two operators are special because they short-circuit
		lhs = co_await cast(lhs, info->leftType, info->commonOperandType->destroyReferences(), false, additionalCopyInfo(node->left, info->left), false, &node->left->position);

		auto llfunc = LLVMGetBasicBlockParent(LLVMGetInsertBlock(_builders.top().get()));
		auto trueBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@short_circuit_true_" + tmpIdxStr).c_str());
		auto falseBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@short_circuit_false_" + tmpIdxStr).c_str());
		auto doneBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@short_circuit_done_" + tmpIdxStr).c_str());

		// for logical AND, we only evaluate RHS if LHS is true.
		// for logical OR, we only evaluate RHS if LHS is false.
		auto evalRHSBlock = (node->type == AltaCore::Shared::OperatorType::LogicalAnd) ? trueBlock : falseBlock;
		auto noEvalBlock = (evalRHSBlock == trueBlock) ? falseBlock : trueBlock;

		// this is the result of the no-eval block.
		// for logical AND, we don't evaluate RHS when LHS is false.
		// for logical OR, we don't evaluate RHS when LHS is true.
		auto shortCircuitResult = LLVMConstInt(LLVMInt1TypeInContext(_llcontext.get()), (node->type == AltaCore::Shared::OperatorType::LogicalAnd) ? 0 : 1, false);

		LLVMBuildCondBr(_builders.top().get(), lhs, trueBlock, falseBlock);

		currentStack().beginBranch();

		LLVMPositionBuilderAtEnd(_builders.top().get(), evalRHSBlock);
		rhs = co_await compileNode(node->right, info->right);
		auto evalResult = co_await cast(rhs, info->rightType, info->commonOperandType->destroyReferences(), false, additionalCopyInfo(node->right, info->right), false, &node->right->position);
		auto finalEvalBlock = LLVMGetInsertBlock(_builders.top().get());
		LLVMBuildBr(_builders.top().get(), doneBlock);

		LLVMPositionBuilderAtEnd(_builders.top().get(), noEvalBlock);
		auto finalNoEvalBlock = LLVMGetInsertBlock(_builders.top().get());
		LLVMBuildBr(_builders.top().get(), doneBlock);

		LLVMPositionBuilderAtEnd(_builders.top().get(), doneBlock);

		result = LLVMBuildPhi(_builders.top().get(), co_await translateType(info->commonOperandType), "");
		LLVMAddIncoming(result, &evalResult, &finalEvalBlock, 1);
		LLVMAddIncoming(result, &shortCircuitResult, &finalNoEvalBlock, 1);

		co_await currentStack().endBranch(doneBlock, { finalEvalBlock, finalNoEvalBlock }, true);

		// after a conditional, we have to create a new state for the merged control flow
		if (inSuspendableFunction()) {
			co_await updateSuspendableStateIndex(++_suspendableStateIndex.top());
		}
	} else {
		lhs = co_await cast(lhs, info->leftType, info->commonOperandType->destroyReferences(), false, additionalCopyInfo(node->left, info->left), false, &node->left->position);
		rhs = co_await cast(rhs, info->rightType, info->commonOperandType->destroyReferences(), false, additionalCopyInfo(node->right, info->right), false, &node->right->position);

		bool arithmetic = false;
		bool pointerArithmetic = false;
		auto i64Type = LLVMInt64TypeInContext(_llcontext.get());
		auto opaquePtr = LLVMPointerTypeInContext(_llcontext.get(), 0);

		switch (node->type) {
			case AltaCore::Shared::OperatorType::Addition:
			case AltaCore::Shared::OperatorType::Subtraction:
			case AltaCore::Shared::OperatorType::Multiplication:
			case AltaCore::Shared::OperatorType::Division:
			case AltaCore::Shared::OperatorType::Modulo:
			case AltaCore::Shared::OperatorType::LeftShift:
			case AltaCore::Shared::OperatorType::RightShift:
			case AltaCore::Shared::OperatorType::BitwiseAnd:
			case AltaCore::Shared::OperatorType::BitwiseOr:
			case AltaCore::Shared::OperatorType::BitwiseXor:
				arithmetic = true;
				break;

			case AltaCore::Shared::OperatorType::EqualTo:
			case AltaCore::Shared::OperatorType::NotEqualTo:
			case AltaCore::Shared::OperatorType::GreaterThan:
			case AltaCore::Shared::OperatorType::LessThan:
			case AltaCore::Shared::OperatorType::GreaterThanOrEqualTo:
			case AltaCore::Shared::OperatorType::LessThanOrEqualTo:
				arithmetic = false;
				break;

			case AltaCore::Shared::OperatorType::LogicalAnd:
			case AltaCore::Shared::OperatorType::LogicalOr:
				throw std::runtime_error("This should be impossible");
		}

		if (arithmetic && info->commonOperandType->pointerLevel() > 0) {
			pointerArithmetic = true;
			lhs = LLVMBuildPtrToInt(_builders.top().get(), lhs, i64Type, ("@binop_cast_lhs_" + tmpIdxStr).c_str());
			rhs = LLVMBuildPtrToInt(_builders.top().get(), rhs, i64Type, ("@binop_cast_rhs_" + tmpIdxStr).c_str());
		}

		switch (node->type) {
			case AltaCore::Shared::OperatorType::Addition: {
				result = (info->commonOperandType->isFloatingPoint() ? LLVMBuildFAdd : LLVMBuildAdd)(_builders.top().get(), lhs, rhs, ("@add_" + tmpIdxStr).c_str());
			} break;
			case AltaCore::Shared::OperatorType::Subtraction: {
				result = (info->commonOperandType->isFloatingPoint() ? LLVMBuildFSub : LLVMBuildSub)(_builders.top().get(), lhs, rhs, ("@sub_" + tmpIdxStr).c_str());
			} break;
			case AltaCore::Shared::OperatorType::Multiplication: {
				result = (info->commonOperandType->isFloatingPoint() ? LLVMBuildFMul : LLVMBuildMul)(_builders.top().get(), lhs, rhs, ("@mul_" + tmpIdxStr).c_str());
			} break;
			case AltaCore::Shared::OperatorType::Division: {
				result = (info->commonOperandType->isFloatingPoint() ? LLVMBuildFDiv : (info->commonOperandType->isSigned() ? LLVMBuildSDiv : LLVMBuildUDiv))(_builders.top().get(), lhs, rhs, ("@div_" + tmpIdxStr).c_str());
			} break;
			case AltaCore::Shared::OperatorType::Modulo: {
				result = (info->commonOperandType->isFloatingPoint() ? LLVMBuildFRem : (info->commonOperandType->isSigned() ? LLVMBuildSRem : LLVMBuildURem))(_builders.top().get(), lhs, rhs, ("@mod_" + tmpIdxStr).c_str());
			} break;
			case AltaCore::Shared::OperatorType::LeftShift: {
				result = LLVMBuildShl(_builders.top().get(), lhs, rhs, ("@shl_" + tmpIdxStr).c_str());
			} break;
			case AltaCore::Shared::OperatorType::RightShift: {
				result = (info->commonOperandType->isSigned() ? LLVMBuildAShr : LLVMBuildLShr)(_builders.top().get(), lhs, rhs, ("@shr_" + tmpIdxStr).c_str());
			} break;

			case AltaCore::Shared::OperatorType::BitwiseAnd: {
				result = LLVMBuildAnd(_builders.top().get(), lhs, rhs, ("@and_" + tmpIdxStr).c_str());
			} break;

			case AltaCore::Shared::OperatorType::BitwiseOr: {
				result = LLVMBuildOr(_builders.top().get(), lhs, rhs, ("@orr_" + tmpIdxStr).c_str());
			} break;

			case AltaCore::Shared::OperatorType::BitwiseXor: {
				result = LLVMBuildXor(_builders.top().get(), lhs, rhs, ("@xor_" + tmpIdxStr).c_str());
			} break;

			case AltaCore::Shared::OperatorType::EqualTo:
			case AltaCore::Shared::OperatorType::NotEqualTo:
			case AltaCore::Shared::OperatorType::GreaterThan:
			case AltaCore::Shared::OperatorType::LessThan:
			case AltaCore::Shared::OperatorType::GreaterThanOrEqualTo:
			case AltaCore::Shared::OperatorType::LessThanOrEqualTo: {
				if (info->commonOperandType->isFloatingPoint()) {
					LLVMRealPredicate pred;

					switch (node->type) {
						case AltaCore::Shared::OperatorType::EqualTo:              pred = LLVMRealUEQ; break;
						case AltaCore::Shared::OperatorType::NotEqualTo:           pred = LLVMRealUNE; break;
						case AltaCore::Shared::OperatorType::GreaterThan:          pred = LLVMRealUGT; break;
						case AltaCore::Shared::OperatorType::LessThan:             pred = LLVMRealULT; break;
						case AltaCore::Shared::OperatorType::GreaterThanOrEqualTo: pred = LLVMRealUGE; break;
						case AltaCore::Shared::OperatorType::LessThanOrEqualTo:    pred = LLVMRealULE; break;
						default:                                                   throw std::runtime_error("impossible");
					}

					result = LLVMBuildFCmp(_builders.top().get(), pred, lhs, rhs, ("@cmp_" + tmpIdxStr).c_str());
				} else {
					LLVMIntPredicate pred;

					switch (node->type) {
						case AltaCore::Shared::OperatorType::EqualTo:              pred = LLVMIntEQ; break;
						case AltaCore::Shared::OperatorType::NotEqualTo:           pred = LLVMIntNE; break;
						case AltaCore::Shared::OperatorType::GreaterThan:          pred = info->commonOperandType->isSigned() ? LLVMIntSGT : LLVMIntUGT; break;
						case AltaCore::Shared::OperatorType::LessThan:             pred = info->commonOperandType->isSigned() ? LLVMIntSLT : LLVMIntULT; break;
						case AltaCore::Shared::OperatorType::GreaterThanOrEqualTo: pred = info->commonOperandType->isSigned() ? LLVMIntSGE : LLVMIntUGE; break;
						case AltaCore::Shared::OperatorType::LessThanOrEqualTo:    pred = info->commonOperandType->isSigned() ? LLVMIntSLE : LLVMIntULE; break;
						default:                                                   throw std::runtime_error("impossible");
					}

					result = LLVMBuildICmp(_builders.top().get(), pred, lhs, rhs, ("@cmp_" + tmpIdxStr).c_str());
				}
			} break;

			case AltaCore::Shared::OperatorType::LogicalAnd:
			case AltaCore::Shared::OperatorType::LogicalOr:
				throw std::runtime_error("This should be impossible");
		}

		if (pointerArithmetic) {
			result = LLVMBuildIntToPtr(_builders.top().get(), result, opaquePtr, ("@binop_cast_result_" + tmpIdxStr).c_str());
		}
	}

	popDebugLocation();
	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileFunctionCallExpression(std::shared_ptr<AltaCore::AST::FunctionCallExpression> node, std::shared_ptr<AltaCore::DH::FunctionCallExpression> info) {
	pushDebugLocation(node->position, info->inputScope);

	LLVMValueRef result = NULL;
	auto tmpIdx = nextTemp();
	auto tmpIdxStr = std::to_string(tmpIdx);

	bool isVoid = *AltaCore::DET::Type::getUnderlyingType(info.get()) == AltaCore::DET::Type(AltaCore::DET::NativeType::Void);
	std::shared_ptr<AltaCore::DET::Type> targetExprStoreType = NULL;

	bool storedTarget = false;

	if (info->isMethodCall) {
		result = co_await tmpify(info->methodClassTarget, info->methodClassTargetInfo);
		targetExprStoreType = AltaCore::DET::Type::getUnderlyingType(info->methodClassTargetInfo.get());
	} else {
		result = co_await compileNode(node->target, info->target);
		targetExprStoreType = info->targetType;
	}

	std::vector<LLVMValueRef> args;
	std::shared_ptr<AltaCore::AST::Accessor> acc = nullptr;
	std::shared_ptr<AltaCore::DH::Accessor> accInfo = nullptr;
	std::shared_ptr<AltaCore::DET::Function> virtFunc = nullptr;

	if (info->isMethodCall) {
		acc = std::dynamic_pointer_cast<AltaCore::AST::Accessor>(node->target);
		accInfo = std::dynamic_pointer_cast<AltaCore::DH::Accessor>(info->target);

		if (auto maybeVirtFunc = std::dynamic_pointer_cast<AltaCore::DET::Function>(accInfo->narrowedTo)) {
			if (maybeVirtFunc->isVirtual()) {
				virtFunc = maybeVirtFunc;
			}
		}

		auto selfType = AltaCore::DET::Type::getUnderlyingType(info->methodClassTargetInfo.get());

		while (selfType->pointerLevel() > 1) {
			result = LLVMBuildLoad2(_builders.top().get(), co_await translateType(selfType->follow()), result, "");
			selfType = selfType->follow();
		}

		bool didRetrieval = false;

		if (virtFunc) {
			result = co_await getRootInstance(result, selfType);
			targetExprStoreType = std::make_shared<AltaCore::DET::Type>(AltaCore::DET::NativeType::Void, std::vector<uint8_t> { (uint8_t)AltaCore::DET::TypeModifierFlag::Pointer });
		} else {
			auto targetType = std::make_shared<AltaCore::DET::Type>(info->targetType->methodParent)->reference();
			result = co_await doParentRetrieval(result, selfType, targetType, &didRetrieval);
			if (didRetrieval) {
				targetExprStoreType = targetType;
			}
		}

		if (inSuspendableFunction() && !LLVMIsConstant(result)) {
			// evaluating the arguments might cause a state change, so let's save the value of the target
			auto alloca = currentStack().buildAlloca(co_await translateType(targetExprStoreType));
			LLVMBuildStore(_builders.top().get(), result, alloca);
			result = alloca;
			storedTarget = true;
		}

		args.push_back(result);
	} else /*if (!info->targetType->isRawFunction)*/ {
		result = co_await loadRef(result, info->targetType);
		targetExprStoreType = info->targetType->destroyReferences();

		if (inSuspendableFunction() && !LLVMIsConstant(result)) {
			// evaluating the arguments might cause a state change, so let's save the value of the target
			auto alloca = currentStack().buildAlloca(co_await translateType(targetExprStoreType));
			LLVMBuildStore(_builders.top().get(), result, alloca);
			result = alloca;
			storedTarget = true;
		}
	}

	if (info->isSpecialScheduleMethod) {
		std::vector<std::tuple<std::string, std::shared_ptr<AltaCore::DET::Type>, bool, std::string>> params;
		params.push_back(std::make_tuple<std::string, std::shared_ptr<AltaCore::DET::Type>, bool, std::string>("", AltaCore::DET::Type::getUnderlyingType(info->arguments[0].get())->destroyIndirection()->reference(), false, ""));
		co_await processArgs(info->adjustedArguments, params, &node->position, args);
	} else {
		co_await processArgs(info->adjustedArguments, info->targetType->parameters, &node->position, args);
	}

	if (info->isMethodCall) {
		LLVMTypeRef llfuncType = NULL;
		LLVMValueRef llfunc = NULL;
		auto func = std::dynamic_pointer_cast<AltaCore::DET::Function>(accInfo->narrowedTo);

		if (storedTarget) {
			// now that we can no longer have a state change, load back the self argument
			args[0] = LLVMBuildLoad2(_builders.top().get(), co_await translateType(targetExprStoreType), args[0], "");
		}

		if (virtFunc) {
			llfuncType = co_await translateType(AltaCore::DET::Type::getUnderlyingType(func), false);
			llfunc = buildVirtualLookup(_builders.top(), args[0], mangleName(func, false));
		} else {
			std::tie(llfuncType, llfunc) = co_await declareFunction(func);
		}

		result = LLVMBuildCall2(_builders.top().get(), llfuncType, llfunc, args.data(), args.size(), isVoid ? "" : ("@method_call_" + tmpIdxStr).c_str());
	} else if (!info->targetType->isRawFunction) {
		auto targetType = info->targetType->destroyReferences();
		auto target = result;
		auto basicFunctionType = co_await translateType(targetType);

		if (storedTarget) {
			// now that we can no longer have a state change, load back the target
			target = LLVMBuildLoad2(_builders.top().get(), co_await translateType(targetExprStoreType), target, "");
		}

		auto targetTypeAsRaw = targetType->copy();
		targetTypeAsRaw->isRawFunction = true;
		auto rawFunctionType = co_await translateType(targetTypeAsRaw, false);

		auto opaquePtr = LLVMPointerTypeInContext(_llcontext.get(), 0);

		std::vector<LLVMTypeRef> params(LLVMCountParamTypes(rawFunctionType) + 1);
		params[0] = opaquePtr;
		LLVMGetParamTypes(rawFunctionType, &params.data()[1]);

		auto lambdaFunctionType = LLVMFunctionType(LLVMGetReturnType(rawFunctionType), params.data(), params.size(), false);

		bool isVoid = *targetTypeAsRaw->returnType == AltaCore::DET::Type(AltaCore::DET::NativeType::Void);

		auto lambdaState = LLVMBuildExtractValue(_builders.top().get(), target, 1, ("@lambda_state_" + tmpIdxStr).c_str());

		args.insert(args.begin(), lambdaState);

		auto functionPointer = LLVMBuildExtractValue(_builders.top().get(), target, 0, ("@func_ptr_" + tmpIdxStr).c_str());

		auto lambdaStateIsNotNull = LLVMBuildIsNotNull(_builders.top().get(), lambdaState, ("@lambda_state_not_null_" + tmpIdxStr).c_str());

		auto llfunc = LLVMGetBasicBlockParent(LLVMGetInsertBlock(_builders.top().get()));
		auto doLambdaBlock = LLVMAppendBasicBlock(llfunc, ("@invoke_lambda_" + tmpIdxStr).c_str());
		auto doRawBlock = LLVMAppendBasicBlock(llfunc, ("@invoke_raw_" + tmpIdxStr).c_str());
		auto doneBlock = LLVMAppendBasicBlock(llfunc, ("@invoke_done_" + tmpIdxStr).c_str());

		LLVMBuildCondBr(_builders.top().get(), lambdaStateIsNotNull, doLambdaBlock, doRawBlock);

		LLVMPositionBuilderAtEnd(_builders.top().get(), doLambdaBlock);
		auto lambdaResult = LLVMBuildCall2(_builders.top().get(), lambdaFunctionType, functionPointer, args.data(), args.size(), isVoid ? "" : ("@lambda_call_" + tmpIdxStr).c_str());
		LLVMBuildBr(_builders.top().get(), doneBlock);

		LLVMPositionBuilderAtEnd(_builders.top().get(), doRawBlock);
		auto rawResult = LLVMBuildCall2(_builders.top().get(), rawFunctionType, functionPointer, &args.data()[1], args.size() - 1, isVoid ? "" : ("@raw_call_" + tmpIdxStr).c_str());
		LLVMBuildBr(_builders.top().get(), doneBlock);

		LLVMPositionBuilderAtEnd(_builders.top().get(), doneBlock);

		if (isVoid) {
			result = NULL;
		} else {
			result = LLVMBuildPhi(_builders.top().get(), LLVMGetReturnType(rawFunctionType), ("@fat_func_phi_" + tmpIdxStr).c_str());
			LLVMAddIncoming(result, &lambdaResult, &doLambdaBlock, 1);
			LLVMAddIncoming(result, &rawResult, &doRawBlock, 1);
		}
	} else {
		auto funcType = co_await translateType(AltaCore::DET::Type::getUnderlyingType(info->target.get())->destroyIndirection(), false);

		if (!result) {
			throw std::runtime_error("Call target is null?");
		}

		if (storedTarget) {
			// now that we can no longer have a state change, load back the target
			result = LLVMBuildLoad2(_builders.top().get(), co_await translateType(targetExprStoreType), result, "");
		}

		result = LLVMBuildCall2(_builders.top().get(), funcType, result, args.data(), args.size(), isVoid ? "" : ("@call_" + tmpIdxStr).c_str());
	}

	if (node->maybe) {
		// for now, since we don't have exception support, we just wrap the result in an optional
		auto resultType = AltaCore::DET::Type::getUnderlyingType(info.get());
		auto funcRetType = resultType->optionalTarget;
		result = co_await cast(result, funcRetType, resultType, false, additionalCopyInfo(node, info), false, &node->position);
	}

	popDebugLocation();
	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileStringLiteralNode(std::shared_ptr<AltaCore::AST::StringLiteralNode> node, std::shared_ptr<AltaCore::DH::StringLiteralNode> info) {
	co_return LLVMBuildGlobalStringPtr(_builders.top().get(), node->value.c_str(), "");
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileFunctionDeclarationNode(std::shared_ptr<AltaCore::AST::FunctionDeclarationNode> node, std::shared_ptr<AltaCore::DH::FunctionDeclarationNode> info) {
	auto retType = co_await translateType(info->returnType->type);
	std::vector<LLVMTypeRef> paramTypes;

	for (const auto& param: info->parameters) {
		paramTypes.push_back(co_await translateType(param->type->type));
	}

	auto [llfuncType, llfunc] = co_await declareFunction(info->function);

	LLVMSetLinkage(llfunc, LLVMExternalLinkage);

	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileAttributeStatement(std::shared_ptr<AltaCore::AST::AttributeStatement> node, std::shared_ptr<AltaCore::DH::AttributeStatement> info) {
	// TODO
	std::cerr << "TODO: AttributeStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileConditionalStatement(std::shared_ptr<AltaCore::AST::ConditionalStatement> node, std::shared_ptr<AltaCore::DH::ConditionalStatement> info) {
	pushDebugLocation(node->position, info->inputScope);

	auto entryBlock = LLVMGetInsertBlock(_builders.top().get());
	auto tmpIdx = nextTemp();
	auto tmpIdxStr = std::to_string(tmpIdx);
	auto llfunc = LLVMGetBasicBlockParent(entryBlock);
	auto finalBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@cond_final_" + tmpIdxStr).c_str());
	LLVMBasicBlockRef elseBlock = NULL;

	// note that we do NOT need to use `beginBranch` and `endBranch` on the current scope stack
	// because each conditional clause creates its own scope (which includes the tests) and at no point do we create any variables
	// within the conditional that live on past the conditional.

	for (size_t i = 0; i < info->alternatives.size() + 1; ++i) {
		auto& test = (i == 0) ? node->primaryTest : node->alternatives[i - 1].first;
		auto& testInfo = (i == 0) ? info->primaryTest : info->alternatives[i - 1].first;
		auto& result = (i == 0) ? node->primaryResult : node->alternatives[i - 1].second;
		auto& resultInfo = (i == 0) ? info->primaryResult : info->alternatives[i - 1].second;
		auto& scope = (i == 0) ? info->primaryScope : info->alternativeScopes[i - 1];

		pushStack(ScopeStack::Type::Other);

		auto compiledTest = co_await compileNode(test, testInfo);
		auto lltest = co_await cast(compiledTest, AltaCore::DET::Type::getUnderlyingType(testInfo.get()), std::make_shared<AltaCore::DET::Type>(AltaCore::DET::NativeType::Bool), false, additionalCopyInfo(test, testInfo), false, &test->position);

		auto trueBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@cond_true_" + tmpIdxStr + "_alt" + std::to_string(i)).c_str());
		auto falseBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@cond_false_" + tmpIdxStr + "_alt" + std::to_string(i)).c_str());

		if (i == info->alternatives.size() && node->finalResult) {
			// this is the last alternative we have but we have an "else" case;
			elseBlock = falseBlock;
		}

		LLVMBuildCondBr(_builders.top().get(), lltest, trueBlock, falseBlock);

		// first, cleanup the scope in the "false" block (so that we only cleanup anything produced by the test)
		LLVMPositionBuilderAtEnd(_builders.top().get(), falseBlock);

		co_await currentStack().cleanup();

		auto falseBlockExit = LLVMGetInsertBlock(_builders.top().get());

		// now, evaluate the result in the "true" block
		LLVMPositionBuilderAtEnd(_builders.top().get(), trueBlock);

		co_await compileNode(result, resultInfo);

		// now cleanup the scope in the true block
		co_await currentStack().cleanup();

		// finally, we can pop the scope stack off
		co_await popStack();

		// we're still in the "true" block; let's go to the final block (if we don't terminate otherwise)
		if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(_builders.top().get()))) {
			LLVMBuildBr(_builders.top().get(), finalBlock);
		}

		// now let's position the builder in the "false" block so we can continue with other alternatives
		LLVMPositionBuilderAtEnd(_builders.top().get(), falseBlockExit);
		if (inSuspendableFunction()) {
			// create a new state for the false branch (since the next block will use the current suspendable index
			// to determine where to branch for stack reloading)
			co_await updateSuspendableStateIndex(++_suspendableStateIndex.top());
		}

		if (i == info->alternatives.size() && !node->finalResult) {
			// this is the final alterantive and we don't have an "else" case;
			// the "false" block must go to the final block in this case.
			LLVMBuildBr(_builders.top().get(), finalBlock);
		}
	}

	if (node->finalResult) {
		pushStack(ScopeStack::Type::Other);

		co_await compileNode(node->finalResult, info->finalResult);

		co_await currentStack().cleanup();

		co_await popStack();

		if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(_builders.top().get()))) {
			LLVMBuildBr(_builders.top().get(), finalBlock);
		}
	}

	LLVMPositionBuilderAtEnd(_builders.top().get(), finalBlock);

	// after a conditional, we have to create a new state for the merged control flow
	if (inSuspendableFunction()) {
		co_await updateSuspendableStateIndex(++_suspendableStateIndex.top());
	}

	popDebugLocation();
	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileConditionalExpression(std::shared_ptr<AltaCore::AST::ConditionalExpression> node, std::shared_ptr<AltaCore::DH::ConditionalExpression> info) {
	pushDebugLocation(node->position, info->inputScope);

	LLVMValueRef result = NULL;
	auto tmpIdx = nextTemp();
	auto tmpIdxStr = std::to_string(tmpIdx);
	auto llfunc = LLVMGetBasicBlockParent(LLVMGetInsertBlock(_builders.top().get()));
	auto trueBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@condexpr_true_" + tmpIdxStr).c_str());
	auto falseBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@condexpr_false_" + tmpIdxStr).c_str());
	auto doneBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@condexpr_done_" + tmpIdxStr).c_str());

	auto testType = AltaCore::DET::Type::getUnderlyingType(info->test.get());
	auto primaryType = AltaCore::DET::Type::getUnderlyingType(info->primaryResult.get());
	auto secondaryType = AltaCore::DET::Type::getUnderlyingType(info->secondaryResult.get());

	auto test = co_await tmpify(node->test, info->test);
	test = co_await cast(test, testType, std::make_shared<AltaCore::DET::Type>(AltaCore::DET::NativeType::Bool), false, additionalCopyInfo(node->test, info->test), false, &node->test->position);

	LLVMBuildCondBr(_builders.top().get(), test, trueBlock, falseBlock);

	// this and the short-circuiting logical operators should be the *only* times when we could have a suspendable state change (i.e. yield or await) during a stack branch

	currentStack().beginBranch();

	LLVMPositionBuilderAtEnd(_builders.top().get(), trueBlock);
	auto primaryResult = co_await compileNode(node->primaryResult, info->primaryResult);
	primaryResult = co_await cast(primaryResult, primaryType, info->commonType, true, additionalCopyInfo(node->primaryResult, info->primaryResult), false, &node->primaryResult->position);
	auto finalTrueBlock = LLVMGetInsertBlock(_builders.top().get());
	LLVMBuildBr(_builders.top().get(), doneBlock);

	LLVMPositionBuilderAtEnd(_builders.top().get(), falseBlock);
	auto secondaryResult = co_await compileNode(node->secondaryResult, info->secondaryResult);
	secondaryResult = co_await cast(secondaryResult, secondaryType, info->commonType, true, additionalCopyInfo(node->secondaryResult, info->secondaryResult), false, &node->secondaryResult->position);
	auto finalFalseBlock = LLVMGetInsertBlock(_builders.top().get());
	LLVMBuildBr(_builders.top().get(), doneBlock);

	LLVMPositionBuilderAtEnd(_builders.top().get(), doneBlock);

	result = LLVMBuildPhi(_builders.top().get(), co_await translateType(info->commonType), "");
	LLVMAddIncoming(result, &primaryResult, &finalTrueBlock, 1);
	LLVMAddIncoming(result, &secondaryResult, &finalFalseBlock, 1);

	co_await currentStack().endBranch(doneBlock, { finalTrueBlock, finalFalseBlock }, true);

	// after a conditional, we have to create a new state for the merged control flow
	if (inSuspendableFunction()) {
		co_await updateSuspendableStateIndex(++_suspendableStateIndex.top());
	}

	popDebugLocation();
	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileClassDefinitionNode(std::shared_ptr<AltaCore::AST::ClassDefinitionNode> node, std::shared_ptr<AltaCore::DH::ClassDefinitionNode> _info) {
	bool isGeneric = _info->klass->genericParameterCount > 0;
	size_t iterationCount = isGeneric ? _info->genericInstantiations.size() : 1;

	for (size_t i = 0; i < iterationCount; ++i) {
		const auto& info = isGeneric ? _info->genericInstantiations[i] : _info;

		if (info->klass->isCaptureClass()) {
			throw std::runtime_error("TODO: support capture classes");
		}

		auto llclassType = co_await defineClassType(info->klass);
		translateClassDebug(info->klass);

		std::array<LLVMTypeRef, 3> initParams {
			co_await translateType(info->initializerMethod->parentClassType),
			LLVMInt1TypeInContext(_llcontext.get()),
			LLVMInt1TypeInContext(_llcontext.get()),
		};
		auto initFuncType = LLVMFunctionType(LLVMVoidTypeInContext(_llcontext.get()), initParams.data(), initParams.size(), false);

		if (!_definedFunctions["_init_" + info->klass->id]) {
			auto mangled = "_init_" + mangleName(info->klass);
			_definedFunctions["_init_" + info->klass->id] = LLVMAddFunction(_llmod.get(), mangled.c_str(), initFuncType);
		}

		auto initFunc = _definedFunctions["_init_" + info->klass->id];
		auto debugFunc = translateFunction(info->initializerMethod, true);
		LLVMSetSubprogram(initFunc, debugFunc);

		LLVMSetLinkage(initFunc, LLVMInternalLinkage);

		_builders.push(llwrap(LLVMCreateBuilderInContext(_llcontext.get())));
		pushStack(ScopeStack::Type::Function);
		pushUnknownDebugLocation(info->initializerMethod->scope);

		auto entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), initFunc, "@entry");
		LLVMPositionBuilderAtEnd(_builders.top().get(), entryBlock);

		auto llself = LLVMGetParam(initFunc, 0);
		auto llisRoot = LLVMGetParam(initFunc, 1);
		auto llshouldInitMembers = LLVMGetParam(initFunc, 2);

		auto initInfosBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), initFunc, "@init_infos");
		auto initMembersBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), initFunc, "@init_members");
		auto doMemberInitBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), initFunc, "@do_member_init");
		auto exitBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), initFunc, "@exit");

		LLVMBuildCondBr(_builders.top().get(), llisRoot, initInfosBlock, initMembersBlock);

		// build the init-infos block (only executed when this is the root class for an instance)
		LLVMPositionBuilderAtEnd(_builders.top().get(), initInfosBlock);

		struct InfoStackEntry {
			std::shared_ptr<AltaCore::DET::Class> klass;
			// the child class (of this class) that contains this particular instance of the class
			std::shared_ptr<AltaCore::DET::Class> child;
			size_t index = 0;
			// a.k.a. offset_from_base
			size_t offsetFromRoot = 0;
			size_t offsetFromOwner = 0;
			LLVMTypeRef classType = NULL;
		};

		struct InfoMapEntry {
			// a.k.a. offset_from_base for the real instance
			size_t offsetFromRootForReal = 0;
			// a.k.a. offset_from_base for the last instance of this class that we encountered
			size_t lastOffsetFromRoot = 0;
			LLVMValueRef lastGlobalClassInfoVar = NULL;
			std::array<LLVMValueRef, 8> lastMembers;
		};

		std::deque<InfoStackEntry> infoStack;
		ALTACORE_MAP<std::string, InfoMapEntry> infoMap;

		infoStack.push_back({
			.klass = info->klass,
			.child = nullptr,
			.index = 0,
			.offsetFromRoot = 0,
			.offsetFromOwner = 0,
			.classType = llclassType,
		});

		while (infoStack.size() > 0) {
			auto& stackEntry = infoStack.back();

			if (stackEntry.index < stackEntry.klass->parents.size()) {
				const auto& parent = stackEntry.klass->parents[stackEntry.index];
				auto offsetFromOwner = LLVMOffsetOfElement(_targetData.get(), stackEntry.classType, stackEntry.index + 1);
				infoStack.push_back({
					.klass = parent,
					.child = stackEntry.klass,
					.index = 0,
					.offsetFromRoot = stackEntry.offsetFromRoot + offsetFromOwner,
					.offsetFromOwner = offsetFromOwner,
					.classType = co_await defineClassType(parent),
				});
				++stackEntry.index;
				continue;
			}

			auto mangled = mangleName(stackEntry.klass);
			auto mangledChild = stackEntry.child ? mangleName(stackEntry.child) : "";
			auto infoVar = LLVMAddGlobal(_llmod.get(), _definedTypes["_Alta_class_info"], ("_Alta_class_info@" + mangled).c_str());
			auto i64Type = LLVMInt64TypeInContext(_llcontext.get());
			auto strType = LLVMPointerType(LLVMInt8TypeInContext(_llcontext.get()), 0);
			auto dtorType = LLVMPointerType(_definedTypes["_Alta_class_destructor"], 0);

			LLVMValueRef virtualTableVar = LLVMConstNull(LLVMPointerTypeInContext(_llcontext.get(), 0));

			if (!stackEntry.child) {
				// this is the root class; let's populate the virtual function table
				auto virtFuncs = stackEntry.klass->findAllVirtualFunctions();

				if (virtFuncs.size() > 0) {
					auto llarrayType = LLVMArrayType(_definedTypes["_Alta_virtual_entry"], virtFuncs.size() + 1);
					virtualTableVar = LLVMAddGlobal(_llmod.get(), llarrayType, ("_Alta_virtual_table@" + mangled).c_str());
					LLVMSetLinkage(virtualTableVar, LLVMInternalLinkage);
					LLVMSetGlobalConstant(virtualTableVar, true);

					std::vector<LLVMValueRef> entries;

					for (const auto& virtFunc: virtFuncs) {
						auto parentClass = virtFunc->parentClassType->klass;
						LLVMValueRef llfunc = NULL;

						if (parentClass->id == stackEntry.klass->id) {
							// this is a virtual function defined on the root class, so we can just pass it through directly
							llfunc = (co_await declareFunction(virtFunc)).second;
						} else {
							// this is a virtual function defined in one of the root class' parents, so we have to define a wrapper
							auto [llfuncType, llactualFunc] = co_await declareFunction(virtFunc);
							llfunc = LLVMAddFunction(_llmod.get(), ("_Alta_virtual_wrapper@" + mangled + "@" + mangleName(virtFunc, false)).c_str(), llfuncType);
							LLVMSetLinkage(llfunc, LLVMInternalLinkage);

							auto prevBlock = LLVMGetInsertBlock(_builders.top().get());

							auto entry = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "@entry");
							LLVMPositionBuilderAtEnd(_builders.top().get(), entry);

							std::vector<LLVMValueRef> args;
							for (size_t i = 0; i < LLVMCountParams(llfunc); ++i) {
								args.push_back(LLVMGetParam(llfunc, i));
							}

							args[0] = co_await doParentRetrieval(args[0], std::make_shared<AltaCore::DET::Type>(stackEntry.klass, std::vector<uint8_t> { (uint8_t)AltaCore::DET::TypeModifierFlag::Reference }), virtFunc->parentClassType);

							bool isVoid = *virtFunc->returnType == AltaCore::DET::Type(AltaCore::DET::NativeType::Void);

							auto result = LLVMBuildCall2(_builders.top().get(), llfuncType, llactualFunc, args.data(), args.size(), isVoid ? "" : "@result");

							if (isVoid) {
								LLVMBuildRetVoid(_builders.top().get());
							} else {
								LLVMBuildRet(_builders.top().get(), result);
							}

							LLVMPositionBuilderAtEnd(_builders.top().get(), prevBlock);
						}

						std::array<LLVMValueRef, 2> entryMembers {
							LLVMBuildGlobalStringPtr(_builders.top().get(), mangleName(virtFunc, false).c_str(), ""),
							llfunc,
						};

						auto entry = LLVMConstNamedStruct(_definedTypes["_Alta_virtual_entry"], entryMembers.data(), entryMembers.size());
						entries.push_back(entry);
					}

					entries.push_back(LLVMConstNull(_definedTypes["_Alta_virtual_entry"]));

					auto init = LLVMConstArray(_definedTypes["_Alta_virtual_entry"], entries.data(), entries.size());
					LLVMSetInitializer(virtualTableVar, init);
				}
			}

			if (info->klass->destructor) {
				co_await declareFunction(info->klass->destructor);
			}

			/*
			struct _Alta_class_info {
				const char* type_name;
				void (*destructor)(void*);
				const char* child_name;
				uint64_t offset_from_real;
				uint64_t offset_from_base;
				uint64_t offset_from_owner;
				uint64_t offset_to_next;
				const struct _Alta_virtual_entry* virtual_table;
			};
			*/
			std::array<LLVMValueRef, 8> members {
				LLVMBuildGlobalStringPtr(_builders.top().get(), mangled.c_str(), ""),
				info->klass->destructor ? _definedFunctions[info->klass->destructor->id] : LLVMConstNull(dtorType),
				stackEntry.child ? LLVMBuildGlobalStringPtr(_builders.top().get(), mangledChild.c_str(), "") : LLVMConstNull(strType),
				nullptr, // filled in later
				LLVMConstInt(i64Type, stackEntry.offsetFromRoot, false),
				LLVMConstInt(i64Type, stackEntry.offsetFromOwner, false),
				LLVMConstInt(i64Type, 0, false), // updated later
				virtualTableVar,
			};

			LLVMSetLinkage(infoVar, LLVMInternalLinkage);

			if (infoMap.find(mangled) == infoMap.end()) {
				members[3] = LLVMConstInt(i64Type, 0, false);

				infoMap[mangled] = {
					.offsetFromRootForReal = stackEntry.offsetFromRoot,
					.lastOffsetFromRoot = stackEntry.offsetFromRoot,
					.lastGlobalClassInfoVar = infoVar,
					.lastMembers = members,
				};
			} else {
				auto& mapEntry = infoMap[mangled];
				auto offsetToNextUpdated = stackEntry.offsetFromRoot - mapEntry.lastOffsetFromRoot;
				auto offsetFromReal = stackEntry.offsetFromRoot - mapEntry.offsetFromRootForReal;

				members[3] = LLVMConstInt(i64Type, offsetFromReal, false);

				// update the `offset_to_next` member of the last instance
				mapEntry.lastMembers[6] = LLVMConstInt(i64Type, offsetToNextUpdated, false);
				LLVMSetInitializer(infoVar, LLVMConstNamedStruct(_definedTypes["_Alta_class_info"], mapEntry.lastMembers.data(), mapEntry.lastMembers.size()));

				mapEntry.lastOffsetFromRoot = stackEntry.offsetFromRoot;
				mapEntry.lastGlobalClassInfoVar = infoVar;
				mapEntry.lastMembers = members;
			}

			LLVMSetInitializer(infoVar, LLVMConstNamedStruct(_definedTypes["_Alta_class_info"], members.data(), members.size()));
			LLVMSetGlobalConstant(infoVar, true);

			auto gepIndexType = i64Type;
			auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());
			std::vector<LLVMValueRef> gepIndices {
				LLVMConstInt(gepIndexType, 0, false),
			};

			for (size_t i = 0; i < infoStack.size() - 1; ++i) {
				// we should subtract 1 from the index in the entry to get the index for the current branch, but we have to add 1 back
				// because the first member of every class is the instance info structure
				gepIndices.push_back(LLVMConstInt(gepStructIndexType, infoStack[i].index /* - 1 + 1 */, false));
			}

			// we want a pointer to the instance info structure
			gepIndices.push_back(LLVMConstInt(gepStructIndexType, 0, false));
			// but we actually want a pointer to the class info pointer within the instance info structure
			gepIndices.push_back(LLVMConstInt(gepStructIndexType, 0, false));

			auto gep = LLVMBuildGEP2(_builders.top().get(), llclassType, llself, gepIndices.data(), gepIndices.size(), "");
			LLVMBuildStore(_builders.top().get(), infoVar, gep);

			infoStack.pop_back();
		}

		LLVMBuildBr(_builders.top().get(), initMembersBlock);

		// now build the member initialization block
		LLVMPositionBuilderAtEnd(_builders.top().get(), initMembersBlock);

		// call parent initializers
		for (size_t i = 0; i < info->klass->parents.size(); ++i) {
			const auto& parent = info->klass->parents[i];

			auto llboolType = LLVMInt1TypeInContext(_llcontext.get());
			auto llparentClassType = co_await defineClassType(parent);
			std::array<LLVMTypeRef, 3> parentInitParams {
				LLVMPointerType(llparentClassType, 0),
				llboolType,
				llboolType,
			};
			auto parentInitFuncType = LLVMFunctionType(LLVMVoidTypeInContext(_llcontext.get()), initParams.data(), initParams.size(), false);

			if (!_definedFunctions["_init_" + parent->id]) {
				auto mangled = "_init_" + mangleName(parent);
				_definedFunctions["_init_" + parent->id] = LLVMAddFunction(_llmod.get(), mangled.c_str(), initFuncType);
			}

			auto parentInitFunc = _definedFunctions["_init_" + parent->id];

			auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
			auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());
			std::array<LLVMValueRef, 2> gepIndices {
				LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
				LLVMConstInt(gepStructIndexType, 1 + i, false), // the parent
			};

			auto llparent = LLVMBuildGEP2(_builders.top().get(), llclassType, llself, gepIndices.data(), gepIndices.size(), "");

			// get a pointer to the class info pointer
			std::array<LLVMValueRef, 3> gepIndices2 {
				LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
				LLVMConstInt(gepStructIndexType, 0, false), // the instance info structure
				LLVMConstInt(gepStructIndexType, 0, false), // the class info pointer
			};
			auto classInfoPtrPtr = LLVMBuildGEP2(_builders.top().get(), llparentClassType, llparent, gepIndices2.data(), gepIndices2.size(), "");
			auto classInfoPtr = LLVMBuildLoad2(_builders.top().get(), LLVMPointerType(_definedTypes["_Alta_class_info"], 0), classInfoPtrPtr, "");

			std::array<LLVMValueRef, 2> gepIndices3 {
				LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
				LLVMConstInt(gepStructIndexType, 3, false), // _Alta_class_info::offset_from_real
			};
			auto offsetFromRealPtr = LLVMBuildGEP2(_builders.top().get(), _definedTypes["_Alta_class_info"], classInfoPtr, gepIndices3.data(), gepIndices3.size(), "");
			auto offsetFromReal = LLVMBuildLoad2(_builders.top().get(), LLVMInt64TypeInContext(_llcontext.get()), offsetFromRealPtr, "");

			auto cmp = LLVMBuildICmp(_builders.top().get(), LLVMIntEQ, offsetFromReal, LLVMConstInt(LLVMInt64TypeInContext(_llcontext.get()), 0, false), "");

			std::array<LLVMValueRef, 3> parentInitArgs {
				llparent,
				LLVMConstInt(llboolType, 0, false),
				LLVMBuildAnd(_builders.top().get(), llshouldInitMembers, cmp, ""),
			};

			LLVMBuildCall2(_builders.top().get(), parentInitFuncType, parentInitFunc, parentInitArgs.data(), parentInitArgs.size(), "");
		}

		LLVMBuildCondBr(_builders.top().get(), llshouldInitMembers, doMemberInitBlock, exitBlock);

		LLVMPositionBuilderAtEnd(_builders.top().get(), doMemberInitBlock);

		auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
		auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());

		size_t varIndex = 1 + info->klass->parents.size();

		for (size_t j = 0; j < node->statements.size(); ++j) {
			if (auto memberDef = std::dynamic_pointer_cast<AltaCore::AST::ClassMemberDefinitionStatement>(node->statements[j])) {
				auto memberDefInfo = std::dynamic_pointer_cast<AltaCore::DH::ClassMemberDefinitionStatement>(info->statements[j]);

				std::array<LLVMValueRef, 2> gepIndices {
					LLVMConstInt(gepIndexType, 0, false), // the first element of the "array"
					LLVMConstInt(gepStructIndexType, varIndex, false), // the member index within the class structure
				};

				++varIndex;

				auto gep = LLVMBuildGEP2(_builders.top().get(), llclassType, llself, gepIndices.data(), gepIndices.size(), "");

				if (memberDef->varDef->initializationExpression) {
					auto expr = co_await compileNode(memberDef->varDef->initializationExpression, memberDefInfo->varDef->initializationExpression);
					auto casted = co_await cast(expr, AltaCore::DET::Type::getUnderlyingType(memberDefInfo->varDef->initializationExpression.get()), memberDefInfo->varDef->variable->type, true, additionalCopyInfo(memberDef->varDef->initializationExpression, memberDefInfo->varDef->initializationExpression), false, &memberDef->varDef->initializationExpression->position);
					LLVMBuildStore(_builders.top().get(), casted, gep);
				} else if (memberDefInfo->varDef->type->type->klass && memberDefInfo->varDef->type->type->indirectionLevel() == 0 && memberDefInfo->varDef->type->type->klass->defaultConstructor) {
					LLVMValueRef init = NULL;

					if (memberDefInfo->varDef->type->type->klass->isBitfield || memberDefInfo->varDef->type->type->klass->isStructure) {
						init = LLVMConstNull(co_await translateType(memberDefInfo->varDef->type->type));
					} else {
						auto func = memberDefInfo->varDef->type->type->klass->defaultConstructor;
						auto [llfuncType, llfunc] = co_await declareFunction(func);
						init = LLVMBuildCall2(_builders.top().get(), llfuncType, llfunc, nullptr, 0, "");
					}

					LLVMBuildStore(_builders.top().get(), init, gep);
				} else if (memberDefInfo->varDef->type->type->referenceLevel() > 0 || memberDefInfo->varDef->type->type->klass) {
					throw std::runtime_error("this should have been handled by AltaCore");
				} else {
					LLVMBuildStore(_builders.top().get(), LLVMConstNull(co_await translateType(memberDefInfo->varDef->type->type)), gep);
				}
			}
		}

		co_await currentStack().cleanup();

		LLVMBuildBr(_builders.top().get(), exitBlock);

		LLVMPositionBuilderAtEnd(_builders.top().get(), exitBlock);

		LLVMBuildRetVoid(_builders.top().get());

		popDebugLocation();
		co_await popStack();
		_builders.pop();

		if (info->defaultConstructor) {
			co_await compileNode(info->defaultConstructor, info->defaultConstructorDetail);
		}

		if (info->defaultCopyConstructor) {
			co_await compileNode(info->defaultCopyConstructor, info->defaultCopyConstructorDetail);
		}

		if (info->defaultDestructor) {
			co_await compileNode(info->defaultDestructor, info->defaultDestructorDetail);
		}

		// now compile all the rest of the statements
		for (size_t j = 0; j < node->statements.size(); ++j) {
			auto statement = node->statements[j];
			auto statementInfo = info->statements[j];

			if (statement->nodeType() == AltaCore::AST::NodeType::ClassMemberDefinitionStatement) {
				continue;
			}

			co_await compileNode(statement, statementInfo);
		}
	}

	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileClassMethodDefinitionStatement(std::shared_ptr<AltaCore::AST::ClassMethodDefinitionStatement> node, std::shared_ptr<AltaCore::DH::ClassMethodDefinitionStatement> info) {
	co_await compileNode(node->funcDef, info->funcDef);
	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileClassSpecialMethodDefinitionStatement(std::shared_ptr<AltaCore::AST::ClassSpecialMethodDefinitionStatement> node, std::shared_ptr<AltaCore::DH::ClassSpecialMethodDefinitionStatement> info) {
	bool isCtor = false;
	bool isDtor = false;
	bool isFrom = false;
	bool isTo = false;

	_suspendableContext.push(nullptr);

	switch (node->type) {
		case AltaCore::AST::SpecialClassMethod::Constructor: isCtor = true; break;
		case AltaCore::AST::SpecialClassMethod::Destructor:  isDtor = true; break;
		case AltaCore::AST::SpecialClassMethod::From:        isFrom = true; break;
		case AltaCore::AST::SpecialClassMethod::To:          isTo   = true; break;
	}

	LLVMTypeRef llretType = NULL;
	LLVMTypeRef llfuncType = NULL;
	std::vector<LLVMTypeRef> llparamTypes;

	auto llclassType = co_await defineClassType(info->klass);

	if (isCtor || isDtor) {
		llretType = LLVMVoidTypeInContext(_llcontext.get());
		if (isDtor) {
			llfuncType = _definedTypes["_Alta_class_destructor"];
		}
	} else if (isFrom) {
		llretType = llclassType;
	} else if (isTo) {
		llretType = co_await translateType(info->specialType->type, true, false);
	} else {
		throw std::runtime_error("impossible");
	}

	if (isCtor || isTo || isDtor) {
		llparamTypes.push_back(co_await translateType(info->method->parentClassType));
	}

	if (isCtor) {
		for (size_t i = 0; i < info->parameters.size(); ++i) {
			const auto& param = node->parameters[i];
			const auto& paramInfo = info->parameters[i];
			auto lltype = co_await translateType(paramInfo->type->type);

			if (param->isVariable) {
				llparamTypes.push_back(LLVMInt64TypeInContext(_llcontext.get()));
				llparamTypes.push_back(LLVMPointerType(lltype, 0));
			} else {
				llparamTypes.push_back(lltype);
			}
		}
	} else if (isFrom) {
		llparamTypes.push_back(co_await translateType(info->specialType->type));
	}

	if (!llfuncType) {
		llfuncType = LLVMFunctionType(llretType, llparamTypes.data(), llparamTypes.size(), false);
	}

	auto methodID = (isCtor ? "_internal_" : "") + info->method->id;

	if (!_definedFunctions[methodID]) {
		auto mangled = (isCtor ? "_internal_" : "") + mangleName(info->method);
		_definedFunctions[methodID] = LLVMAddFunction(_llmod.get(), mangled.c_str(), llfuncType);
	}

	auto llfunc = _definedFunctions[methodID];

	LLVMSetLinkage(llfunc, LLVMInternalLinkage);

	auto entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "@entry");
	auto builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
	LLVMPositionBuilderAtEnd(builder.get(), entryBlock);

	auto llfuncDebug = translateFunction(info->method, true, methodID);
	LLVMSetSubprogram(llfunc, llfuncDebug);

	_overrideFuncScopes.emplace(info->method->id, llfuncDebug);

	_builders.push(builder);
	pushStack(ScopeStack::Type::Function);
	pushUnknownDebugLocation(info->method->scope);

	if (isCtor || isTo || isDtor) {
		auto selfParam = LLVMGetParam(llfunc, 0);
		LLVMSetValueName(selfParam, "@this");
		_thisContextValue.push(selfParam);

		auto tmpThis = co_await tmpify(selfParam, info->method->parentClassType, false, true);

		auto thisDebugType = LLVMDIBuilderCreateObjectPointerType(_debugBuilder.get(), translateTypeDebug(info->method->parentClassType));
		auto thisDebugVar = LLVMDIBuilderCreateParameterVariable(_debugBuilder.get(), translateScope(info->method->scope), "this", sizeof("this") - 1, 1, debugFileForScopeItem(info->method), node->position.line, thisDebugType, false, LLVMDIFlagZero);
		auto thisDebugLoc = LLVMDIBuilderCreateDebugLocation(_llcontext.get(), node->position.line, node->position.column, translateScope(info->method->scope), NULL);

		LLVMDIBuilderInsertDeclareAtEnd(_debugBuilder.get(), tmpThis, thisDebugVar, LLVMDIBuilderCreateExpression(_debugBuilder.get(), NULL, 0), thisDebugLoc, LLVMGetInsertBlock(_builders.top().get()));
	}

	if (isCtor) {
		size_t llparamIndex = 1;
		for (size_t i = 0; i < info->parameters.size(); ++i) {
			const auto& param = node->parameters[i];
			const auto& paramInfo = info->parameters[i];
			const auto& var = info->method->parameterVariables[i];

			auto llparam = LLVMGetParam(llfunc, llparamIndex);
			++llparamIndex;

			LLVMValueRef llparamLen = NULL;

			LLVMSetValueName(llparam, ((param->isVariable ? "@len_" : "") + param->name).c_str());

			if (param->isVariable) {
				llparamLen = llparam;
				llparam = LLVMGetParam(llfunc, llparamIndex);
				++llparamIndex;

				LLVMSetValueName(llparam, param->name.c_str());
			}

			if (!param->isVariable) {
				llparam = co_await tmpify(llparam, var->type, false, true);
				currentStack().pushItem(llparam, var->type);
				LLVMSetValueName(llparam, (param->name + "@on_stack").c_str());
			} else if (param->isVariable) {
				currentStack().pushRuntimeArray(llparam, llparamLen, var->type, LLVMInt64TypeInContext(_llcontext.get()));
			}

			_definedVariables[var->id] = llparam;
			if (llparamLen) {
				_definedVariables["_Alta_array_length_" + var->id] = llparamLen;
			}
		}
	} else if (isFrom) {
		auto llparam = LLVMGetParam(llfunc, 1);
		const auto& var = info->method->parameterVariables[0];

		LLVMSetValueName(llparam, "@val");

		llparam = co_await tmpify(llparam, var->type, false, true);
		currentStack().pushItem(llparam, var->type);
		LLVMSetValueName(llparam, "@val@on_stack");

		_definedVariables[var->id] = llparam;
	}

	if (info->isDefaultCopyConstructor) {
		auto llself = LLVMGetParam(llfunc, 0);
		auto llsource = LLVMGetParam(llfunc, 1);

		std::unordered_set<std::string> processedParents;

		std::function<void(std::shared_ptr<AltaCore::DET::Class>)> insertProcessedParent = [&](std::shared_ptr<AltaCore::DET::Class> parent) {
			processedParents.insert(parent->id);

			for (const auto& grandparent: parent->parents) {
				insertProcessedParent(grandparent);
			}
		};

		auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
		auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());

		for (size_t i = 0; i < info->klass->parents.size(); ++i) {
			const auto& parent = info->klass->parents[i];

			if (processedParents.find(parent->id) != processedParents.end()) {
				continue;
			}

			insertProcessedParent(parent);

			if (!parent->copyConstructor) {
				continue;
			}

			std::array<LLVMValueRef, 2> gepIndices {
				LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
				LLVMConstInt(gepStructIndexType, 1 + i, false), // the parent
			};

			auto selfParent = LLVMBuildGEP2(_builders.top().get(), llclassType, llself, gepIndices.data(), gepIndices.size(), "");
			auto sourceParent = LLVMBuildGEP2(_builders.top().get(), llclassType, llsource, gepIndices.data(), gepIndices.size(), "");

			auto parentClassType = co_await defineClassType(parent);
			auto parentRefType = LLVMPointerType(parentClassType, 0);

			std::array<LLVMTypeRef, 2> parentCopyFuncParams {
				parentRefType,
				parentRefType,
			};

			auto parentCopyFuncType = LLVMFunctionType(LLVMVoidTypeInContext(_llcontext.get()), parentCopyFuncParams.data(), parentCopyFuncParams.size(), false);

			if (!_definedFunctions["_internal_" + parent->copyConstructor->id]) {
				auto mangled = "_internal_" + mangleName(parent->copyConstructor);
				_definedFunctions["_internal_" + parent->copyConstructor->id] = LLVMAddFunction(_llmod.get(), mangled.c_str(), parentCopyFuncType);
			}

			auto parentCopyFunc = _definedFunctions["_internal_" + parent->copyConstructor->id];

			std::array<LLVMValueRef, 2> parentCopyFuncArgs {
				selfParent,
				sourceParent,
			};

			LLVMBuildCall2(_builders.top().get(), parentCopyFuncType, parentCopyFunc, parentCopyFuncArgs.data(), parentCopyFuncArgs.size(), "");
		}

		for (size_t i = 0; i < info->klass->members.size(); ++i) {
			const auto& member = info->klass->members[i];

			std::array<LLVMValueRef, 2> gepIndices {
				LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
				LLVMConstInt(gepStructIndexType, 1 + info->klass->parents.size() + i, false), // the member
			};

			auto selfMember = LLVMBuildGEP2(_builders.top().get(), llclassType, llself, gepIndices.data(), gepIndices.size(), ("@this_" + member->name).c_str());
			auto sourceMember = LLVMBuildGEP2(_builders.top().get(), llclassType, llsource, gepIndices.data(), gepIndices.size(), ("@that_" + member->name).c_str());

			if (member->type->indirectionLevel() > 0) {
				LLVMBuildStore(_builders.top().get(), LLVMBuildLoad2(_builders.top().get(), co_await translateType(member->type), sourceMember, ""), selfMember);
			} else {
				bool didCopy = false;
				auto copy = co_await doCopyCtor(sourceMember, member->type->reference(), std::make_pair(true, false), &didCopy);
				if (!didCopy) {
					copy = LLVMBuildLoad2(_builders.top().get(), co_await translateType(member->type), sourceMember, ("@that_load@" + member->name).c_str());
				}
				LLVMBuildStore(_builders.top().get(), copy, selfMember);
			}
		}
	} else {
		pushDebugLocation(node->body->position, info->method->scope);
		co_await compileNode(node->body, info->body);
		popDebugLocation();
	}

	if (isDtor) {
		auto llself = LLVMGetParam(llfunc, 0);

		auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
		auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());

		// first, destroy members

		for (size_t i = info->klass->members.size(); i > 0; --i) {
			auto idx = i - 1;
			const auto& member = info->klass->members[idx];

			std::array<LLVMValueRef, 2> gepIndices {
				LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
				LLVMConstInt(gepStructIndexType, 1 + info->klass->parents.size() + idx, false), // the member
			};

			auto llmember = LLVMBuildGEP2(_builders.top().get(), llclassType, llself, gepIndices.data(), gepIndices.size(), ("@this_" + member->name).c_str());

			if (member->type->indirectionLevel() == 0) {
				co_await doDtor(llmember, member->type);
			}
		}

		// now, destroy parents

		std::unordered_set<std::string> processedParents;

		std::function<void(std::shared_ptr<AltaCore::DET::Class>)> insertProcessedParent = [&](std::shared_ptr<AltaCore::DET::Class> parent) {
			processedParents.insert(parent->id);

			for (const auto& grandparent: parent->parents) {
				insertProcessedParent(grandparent);
			}
		};

		for (size_t i = 0; i < info->klass->parents.size(); ++i) {
			const auto& parent = info->klass->parents[i];

			if (processedParents.find(parent->id) != processedParents.end()) {
				continue;
			}

			insertProcessedParent(parent);

			if (!parent->destructor) {
				continue;
			}

			auto [dtorFuncType, dtorFunc] = co_await declareFunction(parent->destructor);

			std::array<LLVMValueRef, 2> gepIndices {
				LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
				LLVMConstInt(gepStructIndexType, 1 + i, false), // the parent
			};

			auto llparent = LLVMBuildGEP2(_builders.top().get(), llclassType, llself, gepIndices.data(), gepIndices.size(), ("@this@parent_" + mangleName(parent)).c_str());

			std::array<LLVMValueRef, 1> dtorArgs {
				llparent,
			};

			LLVMBuildCall2(_builders.top().get(), dtorFuncType, dtorFunc, dtorArgs.data(), dtorArgs.size(), "");
		}
	}

	if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(_builders.top().get()))) {
		if (isDtor || isCtor || isFrom) {
			// insert an implicit return
			co_await currentStack().cleanup();

			LLVMBuildRetVoid(_builders.top().get());
		} else {
			LLVMBuildUnreachable(_builders.top().get());
		}
	}

	if (isCtor || isTo || isDtor) {
		_thisContextValue.pop();
	}

	co_await popStack();
	popDebugLocation();
	_builders.pop();

	_overrideFuncScopes.pop();

	if (isCtor) {
		auto methodID2 = info->method->id;

		std::vector<LLVMTypeRef> llparams2(llparamTypes.begin() + 1, llparamTypes.end());

		auto llfuncType2 = LLVMFunctionType(llclassType, llparams2.data(), llparams2.size(), false);

		if (!_definedFunctions[methodID2]) {
			auto mangled = mangleName(info->method);
			_definedFunctions[methodID2] = LLVMAddFunction(_llmod.get(), mangled.c_str(), llfuncType2);
		}

		auto llfunc2 = _definedFunctions[methodID2];

		auto llfuncReturnsInstance = llfunc2;
		auto llfuncTypeReturnsInstance = llfuncType2;

		auto entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc2, "@entry");
		auto builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
		LLVMPositionBuilderAtEnd(builder.get(), entryBlock);

		auto llfuncDebug = translateFunction(info->method, true, methodID2, true);
		LLVMSetSubprogram(llfunc2, llfuncDebug);

		_overrideFuncScopes.emplace(info->method->id, llfuncDebug);

		_builders.push(builder);
		pushUnknownDebugLocation(info->method->scope);

		auto llclassRefType = LLVMPointerType(llclassType, 0);
		auto llboolType = LLVMInt1TypeInContext(_llcontext.get());
		std::array<LLVMTypeRef, 3> initFuncParams {
			llclassRefType,
			llboolType,
			llboolType,
		};

		auto instance = LLVMBuildAlloca(_builders.top().get(), llclassType, "@this");
		auto initFuncType = LLVMFunctionType(LLVMVoidTypeInContext(_llcontext.get()), initFuncParams.data(), initFuncParams.size(), false);
		auto initFunc = _definedFunctions["_init_" + info->klass->id];

		std::vector<LLVMValueRef> args {
			instance,
		};

		for (size_t i = 0; i < llparams2.size(); ++i) {
			args.push_back(LLVMGetParam(llfunc2, i));
		}

		std::array<LLVMValueRef, 3> initArgs {
			instance,
			LLVMConstInt(llboolType, 1, false),
			LLVMConstInt(llboolType, info->isDefaultCopyConstructor ? 0 : 1, false),
		};

		LLVMBuildCall2(_builders.top().get(), initFuncType, initFunc, initArgs.data(), initArgs.size(), "");
		LLVMBuildCall2(_builders.top().get(), llfuncType, llfunc, args.data(), args.size(), "");

		LLVMBuildRet(_builders.top().get(), LLVMBuildLoad2(_builders.top().get(), llclassType, instance, ""));

		popDebugLocation();
		_builders.pop();

		_overrideFuncScopes.pop();

		// now build the persistent version
		methodID2 = "_persistent_" + methodID2;

		llfuncType2 = LLVMFunctionType(llclassRefType, llparams2.data(), llparams2.size(), false);

		if (!_definedFunctions[methodID2]) {
			auto mangled = "_persistent_" + mangleName(info->method);
			_definedFunctions[methodID2] = LLVMAddFunction(_llmod.get(), mangled.c_str(), llfuncType2);
		}

		llfunc2 = _definedFunctions[methodID2];

		entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc2, "@entry");
		builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
		LLVMPositionBuilderAtEnd(builder.get(), entryBlock);

		auto llfuncDebug2 = translateFunction(info->method, true, methodID2, true);
		LLVMSetSubprogram(llfunc2, llfuncDebug2);

		_overrideFuncScopes.emplace(info->method->id, llfuncDebug2);

		_builders.push(builder);
		pushUnknownDebugLocation(info->method->scope);

		instance = LLVMBuildMalloc(_builders.top().get(), llclassType, "@this");

		args.clear();
		args.push_back(instance);

		for (size_t i = 0; i < llparams2.size(); ++i) {
			args.push_back(LLVMGetParam(llfunc2, i));
		}

		initArgs[0] = instance;

		LLVMBuildCall2(_builders.top().get(), initFuncType, initFunc, initArgs.data(), initArgs.size(), "");
		LLVMBuildCall2(_builders.top().get(), llfuncType, llfunc, args.data(), args.size(), "");

		LLVMBuildRet(_builders.top().get(), instance);

		popDebugLocation();
		_builders.pop();

		_overrideFuncScopes.pop();

		auto llfuncReturnsRef = llfunc2;
		auto llfuncTypeReturnsRef = llfuncType2;

		if (info->isCastConstructor) {
			auto [castFuncType, castFunc] = co_await declareFunction(info->correspondingMethod);

			auto entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), castFunc, "@entry");
			auto builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
			LLVMPositionBuilderAtEnd(builder.get(), entryBlock);

			auto castFuncDebug = translateFunction(info->correspondingMethod, true);
			LLVMSetSubprogram(castFunc, castFuncDebug);

			_builders.push(builder);
			pushUnknownDebugLocation(info->correspondingMethod->scope);

			auto llval = LLVMGetParam(castFunc, 0);

			auto result = LLVMBuildCall2(builder.get(), llfuncTypeReturnsInstance, llfuncReturnsInstance, &llval, 1, "");

			LLVMBuildRet(builder.get(), result);

			popDebugLocation();
			_builders.pop();
		}

		if (info->optionalVariantFunctions.size() > 0) {
			for (const auto& [variant, optionalValueProvided]: info->optionalVariantFunctions) {
				std::vector<LLVMTypeRef> llparamsOptional;

				size_t optionalIndex = 0;
				size_t variantIndex = 0;
				for (size_t i = 0; i < info->parameters.size(); ++i) {
					const auto& param = node->parameters[i];
					const auto& paramInfo = info->parameters[i];

					if (paramInfo->defaultValue && !optionalValueProvided[optionalIndex++]) {
						continue;
					}

					const auto& var = variant->parameterVariables[variantIndex];
					auto type = paramInfo->type->type;
					auto lltype = co_await translateType(type);

					if (param->isVariable) {
						llparamsOptional.push_back(LLVMInt64TypeInContext(_llcontext.get()));
						llparamsOptional.push_back(LLVMPointerType(lltype, 0));
					} else {
						llparamsOptional.push_back(lltype);
					}

					++variantIndex;
				}

				auto optionalFuncType = LLVMFunctionType(llclassType, llparamsOptional.data(), llparamsOptional.size(), false);
				auto optionalMethodID = variant->id;

				if (!_definedFunctions[optionalMethodID]) {
					auto mangled = mangleName(variant);
					_definedFunctions[optionalMethodID] = LLVMAddFunction(_llmod.get(), mangled.c_str(), optionalFuncType);
				}

				auto llfuncOptional = _definedFunctions[optionalMethodID];

				auto entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfuncOptional, "@entry");
				auto builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
				LLVMPositionBuilderAtEnd(builder.get(), entryBlock);

				auto llfuncOptionalDebug = translateFunction(variant, true);
				LLVMSetSubprogram(llfuncOptional, llfuncOptionalDebug);

				_builders.push(builder);
				pushUnknownDebugLocation(variant->scope);
				pushStack(ScopeStack::Type::Function);

				std::vector<LLVMValueRef> args;

				optionalIndex = 0;
				variantIndex = 0;

				for (size_t i = 0; i < info->parameters.size(); ++i) {
					const auto& paramInfo = info->parameters[i];

					if (paramInfo->defaultValue && !optionalValueProvided[optionalIndex++]) {
						auto tmp = paramInfo->defaultValue->inputScope;
						paramInfo->defaultValue->inputScope = variant->scope;
						auto compiled = co_await compileNode(node->parameters[i]->defaultValue, paramInfo->defaultValue);
						paramInfo->defaultValue->inputScope = tmp;
						auto casted = co_await cast(compiled, AltaCore::DET::Type::getUnderlyingType(paramInfo->defaultValue.get()), paramInfo->type->type, false, additionalCopyInfo(node->parameters[i]->defaultValue, paramInfo->defaultValue), false, &node->parameters[i]->defaultValue->position);
						args.push_back(casted);
					} else {
						auto llparam = LLVMGetParam(llfuncOptional, variantIndex);
						LLVMSetValueName(llparam, node->parameters[i]->name.c_str());
						args.push_back(llparam);
						++variantIndex;
					}
				}

				auto call = LLVMBuildCall2(_builders.top().get(), llfuncTypeReturnsInstance, llfuncReturnsInstance, args.data(), args.size(), "@default_call");

				co_await currentStack().cleanup();

				LLVMBuildRet(_builders.top().get(), call);

				co_await popStack();
				popDebugLocation();
				_builders.pop();

				// now build the persistent version
				optionalMethodID = "_persistent_" + optionalMethodID;

				optionalFuncType = LLVMFunctionType(llclassRefType, llparamsOptional.data(), llparamsOptional.size(), false);

				if (!_definedFunctions[optionalMethodID]) {
					auto mangled = "_persistent_" + mangleName(variant);
					_definedFunctions[optionalMethodID] = LLVMAddFunction(_llmod.get(), mangled.c_str(), optionalFuncType);
				}

				llfuncOptional = _definedFunctions[optionalMethodID];

				entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfuncOptional, "@entry");
				builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
				LLVMPositionBuilderAtEnd(builder.get(), entryBlock);

				llfuncOptionalDebug = translateFunction(variant, true, optionalMethodID);
				LLVMSetSubprogram(llfuncOptional, llfuncOptionalDebug);

				_overrideFuncScopes.emplace(variant->id, llfuncOptionalDebug);

				_builders.push(builder);
				pushUnknownDebugLocation(variant->scope);
				pushStack(ScopeStack::Type::Function);

				args.clear();

				optionalIndex = 0;
				variantIndex = 0;

				for (size_t i = 0; i < info->parameters.size(); ++i) {
					const auto& paramInfo = info->parameters[i];

					if (paramInfo->defaultValue && !optionalValueProvided[optionalIndex++]) {
						auto tmp = paramInfo->defaultValue->inputScope;
						paramInfo->defaultValue->inputScope = variant->scope;
						auto compiled = co_await compileNode(node->parameters[i]->defaultValue, paramInfo->defaultValue);
						paramInfo->defaultValue->inputScope = tmp;
						auto casted = co_await cast(compiled, AltaCore::DET::Type::getUnderlyingType(paramInfo->defaultValue.get()), paramInfo->type->type, false, additionalCopyInfo(node->parameters[i]->defaultValue, paramInfo->defaultValue), false, &node->parameters[i]->defaultValue->position);
						args.push_back(casted);
					} else {
						auto llparam = LLVMGetParam(llfuncOptional, variantIndex);
						LLVMSetValueName(llparam, node->parameters[i]->name.c_str());
						args.push_back(llparam);
						++variantIndex;
					}
				}

				call = LLVMBuildCall2(_builders.top().get(), llfuncTypeReturnsRef, llfuncReturnsRef, args.data(), args.size(), "@default_call");

				co_await currentStack().cleanup();

				LLVMBuildRet(_builders.top().get(), call);

				co_await popStack();
				popDebugLocation();
				_builders.pop();

				_overrideFuncScopes.pop();
			}
		}
	} else {
		// only constructors can have optional/default parameters
		assert(info->optionalVariantFunctions.size() == 0);
	}

	_suspendableContext.pop();

	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileClassInstantiationExpression(std::shared_ptr<AltaCore::AST::ClassInstantiationExpression> node, std::shared_ptr<AltaCore::DH::ClassInstantiationExpression> info) {
	pushDebugLocation(node->position, info->inputScope);

	LLVMValueRef result = NULL;
	auto tmpIdx = nextTemp();
	auto tmpIdxStr = std::to_string(tmpIdx);

	std::vector<LLVMValueRef> args;
	co_await processArgs(info->adjustedArguments, info->constructor->parameters, &node->position, args);

	if (info->superclass) {
		auto thisClass = AltaCore::Util::getClass(info->inputScope).lock();
		auto parentClass = info->klass;

		auto llfunc = LLVMGetBasicBlockParent(LLVMGetInsertBlock(_builders.top().get()));
		auto llself = LLVMGetParam(llfunc, 0);

		args.insert(args.begin(), llself);

		auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
		auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());

		size_t parentIndex = 0;

		for (; parentIndex < thisClass->parents.size(); ++parentIndex) {
			if (parentClass->id == thisClass->parents[parentIndex]->id) {
				break;
			}
		}

		if (parentIndex == thisClass->parents.size()) {
			throw std::runtime_error("impossible");
		}

		std::array<LLVMValueRef, 2> gepIndices {
			LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
			LLVMConstInt(gepStructIndexType, parentIndex + 1, false), // the parent
		};

		auto llclassType = co_await defineClassType(thisClass);
		auto llparentClassType = co_await defineClassType(parentClass);

		auto llparent = LLVMBuildGEP2(_builders.top().get(), llclassType, llself, gepIndices.data(), gepIndices.size(), ("@superclass_ptr_" + tmpIdxStr).c_str());
		args[0] = llparent;

		std::vector<LLVMTypeRef> ctorParams;
		ctorParams.push_back(co_await translateType(info->constructor->parentClassType));

		for (size_t i = 0; i < info->constructor->parameters.size(); ++i) {
			const auto& [name, type, isVariable, id] = info->constructor->parameters[i];
			auto lltype = co_await translateType(type);

			if (isVariable) {
				ctorParams.push_back(LLVMInt64TypeInContext(_llcontext.get()));
				ctorParams.push_back(LLVMPointerType(lltype, 0));
			} else {
				ctorParams.push_back(lltype);
			}
		}

		auto ctorFuncType = LLVMFunctionType(LLVMVoidTypeInContext(_llcontext.get()), ctorParams.data(), ctorParams.size(), false);

		if (!_definedFunctions["_internal_" + info->constructor->id]) {
			auto mangled = "_internal_" +  mangleName(info->constructor);
			_definedFunctions["_internal_" + info->constructor->id] = LLVMAddFunction(_llmod.get(), mangled.c_str(), ctorFuncType);
		}

		auto doCtorBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@superclass_do_ctor_" + tmpIdxStr).c_str());
		auto doneBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@superclass_done_" + tmpIdxStr).c_str());

		// FIXME: this breaks short-circuiting expectations because we still evaluate the arguments even when we don't execute the call

		// get a pointer to the class info pointer
		std::array<LLVMValueRef, 3> gepIndices2 {
			LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
			LLVMConstInt(gepStructIndexType, 0, false), // the instance info structure
			LLVMConstInt(gepStructIndexType, 0, false), // the class info pointer
		};
		auto classInfoPtrPtr = LLVMBuildGEP2(_builders.top().get(), llparentClassType, llparent, gepIndices2.data(), gepIndices2.size(), ("@superclass_this_class_info_ptr_ptr_" + tmpIdxStr).c_str());
		auto classInfoPtr = LLVMBuildLoad2(_builders.top().get(), LLVMPointerType(_definedTypes["_Alta_class_info"], 0), classInfoPtrPtr, ("@superclass_this_class_info_ptr_" + tmpIdxStr).c_str());

		std::array<LLVMValueRef, 2> gepIndices3 {
			LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
			LLVMConstInt(gepStructIndexType, 3, false), // _Alta_class_info::offset_from_real
		};
		auto offsetFromRealPtr = LLVMBuildGEP2(_builders.top().get(), _definedTypes["_Alta_class_info"], classInfoPtr, gepIndices3.data(), gepIndices3.size(), ("@superclass_offset_from_real_ptr_" + tmpIdxStr).c_str());
		auto offsetFromReal = LLVMBuildLoad2(_builders.top().get(), LLVMInt64TypeInContext(_llcontext.get()), offsetFromRealPtr, ("@superclass_offset_from_real_" + tmpIdxStr).c_str());

		auto cmp = LLVMBuildICmp(_builders.top().get(), LLVMIntEQ, offsetFromReal, LLVMConstInt(LLVMInt64TypeInContext(_llcontext.get()), 0, false), ("@superclass_offset_from_real_is_zero_" + tmpIdxStr).c_str());

		LLVMBuildCondBr(_builders.top().get(), cmp, doCtorBlock, doneBlock);

		LLVMPositionBuilderAtEnd(_builders.top().get(), doCtorBlock);
		LLVMBuildCall2(_builders.top().get(), ctorFuncType, _definedFunctions["_internal_" + info->constructor->id], args.data(), args.size(), "");
		LLVMBuildBr(_builders.top().get(), doneBlock);

		LLVMPositionBuilderAtEnd(_builders.top().get(), doneBlock);
	} else if (info->klass->isStructure) {
		auto lltype = co_await defineClassType(info->klass);

		for (size_t i = args.size(); i < info->klass->members.size(); ++i) {
			args.push_back(LLVMConstNull(co_await translateType(info->klass->members[i]->type)));
		}

		result = LLVMGetUndef(lltype);

		for (size_t i = 0; i < args.size(); ++i) {
			result = LLVMBuildInsertValue(_builders.top().get(), result, args[i], i, ("@struct_init_" + tmpIdxStr + "_partial_" + std::to_string(i)).c_str());
		}

		LLVMSetValueName(result, ("@struct_init_" + tmpIdxStr).c_str());

		if (info->persistent) {
			auto alloc = LLVMBuildMalloc(_builders.top().get(), lltype, ("@struct_init_persistent_" + tmpIdxStr).c_str());
			LLVMBuildStore(_builders.top().get(), result, alloc);
			result = alloc;
		}
	} else {
		auto mangled = mangleName(info->constructor);
		auto retType = std::make_shared<AltaCore::DET::Type>(info->klass);
		std::vector<LLVMTypeRef> llparams;
		auto methodID = info->constructor->id;

		if (info->persistent) {
			mangled = "_persistent_" + mangled;
			methodID = "_persistent_" + methodID;
			retType = retType->reference();
		}

		auto llret = co_await translateType(retType, true, false);

		for (const auto& param: info->constructor->parameterVariables) {
			auto lltype = co_await translateType(param->type);
			if (param->isVariable) {
				llparams.push_back(LLVMInt64TypeInContext(_llcontext.get()));
				llparams.push_back(LLVMPointerType(lltype, 0));
			} else {
				llparams.push_back(lltype);
			}
		}

		auto funcType = LLVMFunctionType(llret, llparams.data(), llparams.size(), false);

		if (!_definedFunctions[methodID]) {
			_definedFunctions[methodID] = LLVMAddFunction(_llmod.get(), mangled.c_str(), funcType);
		}

		result = LLVMBuildCall2(_builders.top().get(), funcType, _definedFunctions[methodID], args.data(), args.size(), ("@class_inst_" + tmpIdxStr).c_str());
	}

	popDebugLocation();
	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compilePointerExpression(std::shared_ptr<AltaCore::AST::PointerExpression> node, std::shared_ptr<AltaCore::DH::PointerExpression> info) {
	pushDebugLocation(node->position, info->inputScope);

	auto result = co_await compileNode(node->target, info->target);
	auto type = AltaCore::DET::Type::getUnderlyingType(info->target.get());

	if (type->referenceLevel() == 0) {
		result = co_await tmpify(result, type, true);
	}

	popDebugLocation();
	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileDereferenceExpression(std::shared_ptr<AltaCore::AST::DereferenceExpression> node, std::shared_ptr<AltaCore::DH::DereferenceExpression> info) {
	pushDebugLocation(node->position, info->inputScope);

	auto result = co_await compileNode(node->target, info->target);
	auto type = AltaCore::DET::Type::getUnderlyingType(info->target.get());

	if (type->pointerLevel() > 0) {
		result = co_await loadRef(result, type);
		// we return a reference to the pointed value, so we would only need to perform a pointer cast.
		// however, now that LLVM uses opaque pointers, we don't even need to do that; just pass the pointer on through.
		//result = LLVMBuildLoad2(_builders.top().get(), co_await translateType(type->destroyReferences()->follow()), result, "");
	} else if (type->isOptional) {
		auto tmpIdx = nextTemp();
		auto tmpIdxStr = std::to_string(tmpIdx);

		if (type->referenceLevel() > 0) {
			auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
			auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());
			std::array<LLVMValueRef, 2> gepIndices {
				LLVMConstInt(gepIndexType, 0, false), // get the first "array" element
				LLVMConstInt(gepStructIndexType, 1, false), // get the contained type from the optional
			};
			result = LLVMBuildGEP2(_builders.top().get(), co_await translateType(type->destroyReferences()), result, gepIndices.data(), gepIndices.size(), ("@opt_unwrap_ref_" + tmpIdxStr).c_str());
			if (type->optionalTarget->referenceLevel() > 0) {
				// this behavior is neither ideal nor desired, but for the sake of preserving compatibility with the existing code,
				// when the optional contains a reference, we return that reference instead of a reference to the reference.
				//
				// i think i'm going to rework the compiler (including the frontend) to just do away with the current reference behavior (and maybe references altogether),
				// since it's way too confusing and error-prone.
				result = LLVMBuildLoad2(_builders.top().get(), co_await translateType(type->optionalTarget), result, ("@opt_unwrap_ref_deref_" + tmpIdxStr).c_str());
			}
		} else {
			result = LLVMBuildExtractValue(_builders.top().get(), result, 1, ("@opt_unwrap_" + tmpIdxStr).c_str());
		}
	} else {
		throw std::runtime_error("Invalid dereference");
	}

	popDebugLocation();
	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileWhileLoopStatement(std::shared_ptr<AltaCore::AST::WhileLoopStatement> node, std::shared_ptr<AltaCore::DH::WhileLoopStatement> info) {
	pushDebugLocation(node->position, info->inputScope);

	auto tmpIdx = nextTemp();
	auto tmpIdxStr = std::to_string(tmpIdx);

	auto llfunc = LLVMGetBasicBlockParent(LLVMGetInsertBlock(_builders.top().get()));

	auto condBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@while_" + tmpIdxStr + "_cond").c_str());
	auto bodyBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@while_" + tmpIdxStr + "_body").c_str());
	auto endBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@while_" + tmpIdxStr + "_end").c_str());
	auto doneBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@while_" + tmpIdxStr + "_done").c_str());

	auto entryState = inSuspendableFunction() ? _suspendableStateIndex.top() : SIZE_MAX;

	LLVMBuildBr(_builders.top().get(), condBlock);

	LLVMPositionBuilderAtEnd(_builders.top().get(), condBlock);

	// before we go for another iteration, reset the state to what it was when we created the loop.
	if (inSuspendableFunction()) {
		co_await updateSuspendableStateIndex(entryState);
	}

	pushStack(ScopeStack::Type::Other);
	pushLoop(doneBlock, condBlock);

	auto cond = co_await compileNode(node->test, info->test);
	auto asBool = co_await cast(cond, AltaCore::DET::Type::getUnderlyingType(info->test.get()), std::make_shared<AltaCore::DET::Type>(AltaCore::DET::NativeType::Bool), false, additionalCopyInfo(node->test, info->test), false, &node->test->position);

	LLVMBuildCondBr(_builders.top().get(), asBool, bodyBlock, endBlock);

	// build the end block now to avoid having to use startBranch/endBranch
	LLVMPositionBuilderAtEnd(_builders.top().get(), endBlock);
	co_await currentStack().cleanup();
	LLVMBuildBr(_builders.top().get(), doneBlock);

	LLVMPositionBuilderAtEnd(_builders.top().get(), bodyBlock);
	co_await compileNode(node->body, info->body);
	co_await currentStack().cleanup();
	LLVMBuildBr(_builders.top().get(), condBlock);

	popLoop();
	co_await popStack();

	LLVMPositionBuilderAtEnd(_builders.top().get(), doneBlock);

	// after a loop, we have to create a new state for the merged control flow (just like we do for conditionals)
	if (inSuspendableFunction()) {
		co_await updateSuspendableStateIndex(++_suspendableStateIndex.top());
	}

	popDebugLocation();
	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileCastExpression(std::shared_ptr<AltaCore::AST::CastExpression> node, std::shared_ptr<AltaCore::DH::CastExpression> info) {
	pushDebugLocation(node->position, info->inputScope);

	auto expr = co_await compileNode(node->target, info->target);
	auto exprType = AltaCore::DET::Type::getUnderlyingType(info->target.get());
	auto val = co_await cast(expr, exprType, info->type->type, false, additionalCopyInfo(node->target, info->target), true, &node->position);

	popDebugLocation();
	co_return val;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileClassReadAccessorDefinitionStatement(std::shared_ptr<AltaCore::AST::ClassReadAccessorDefinitionStatement> node, std::shared_ptr<AltaCore::DH::ClassReadAccessorDefinitionStatement> info) {
	// TODO
	std::cerr << "TODO: ClassReadAccessorDefinitionStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileCharacterLiteralNode(std::shared_ptr<AltaCore::AST::CharacterLiteralNode> node, std::shared_ptr<AltaCore::DH::CharacterLiteralNode> info) {
	co_return LLVMConstInt(LLVMInt8TypeInContext(_llcontext.get()), node->value, false);
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileSubscriptExpression(std::shared_ptr<AltaCore::AST::SubscriptExpression> node, std::shared_ptr<AltaCore::DH::SubscriptExpression> info) {
	pushDebugLocation(node->position, info->inputScope);

	LLVMValueRef result = NULL;
	auto tmpIdx = nextTemp();
	auto tmpIdxStr = std::to_string(tmpIdx);

	if (info->enumeration) {
		std::shared_ptr<AltaCore::DET::Function> func = info->reverseLookup ? info->enumeration->enumerationReverseLookupFunction : info->enumeration->enumerationLookupFunction;
		auto [llfuncType, llfunc] = co_await declareFunction(func);
		auto subscript = co_await compileNode(node->index, info->index);

		std::array<LLVMValueRef, 1> args {
			co_await cast(subscript, info->indexType, std::get<1>(func->parameters[0]), true, additionalCopyInfo(node->index, info->index), false, &node->position),
		};

		result = LLVMBuildCall2(_builders.top().get(), llfuncType, llfunc, args.data(), args.size(), ("@enum" + std::string(info->reverseLookup ? "_reverse" : "") + "_lookup_" + tmpIdxStr).c_str());
	} else {
		auto target = co_await compileNode(node->target, info->target);

		bool storedTarget = false;

		if (inSuspendableFunction() && !LLVMIsConstant(target)) {
			// evaluating the subscript might cause a state change, so let's save the value of the target
			auto alloca = currentStack().buildAlloca(co_await translateType(info->targetType), ("@subscript_target_save_" + tmpIdxStr).c_str());
			LLVMBuildStore(_builders.top().get(), target, alloca);
			target = alloca;
			storedTarget = true;
		}

		auto subscript = co_await compileNode(node->index, info->index);

		if (storedTarget) {
			// now that we've evaluated the subscript, we can't have another state change, so let's load back the target value
			target = LLVMBuildLoad2(_builders.top().get(), co_await translateType(info->targetType), target, ("@subscript_target_restore_" + tmpIdxStr).c_str());
		}

		if (info->operatorMethod) {
			subscript = co_await cast(subscript, info->indexType, info->operatorMethod->parameterVariables.front()->type, true, additionalCopyInfo(node->index, info->index), false, &node->index->position);

			auto [llfuncType, llfunc] = co_await declareFunction(info->operatorMethod);

			std::array<LLVMValueRef, 2> args {
				target,
				subscript,
			};

			result = LLVMBuildCall2(_builders.top().get(), llfuncType, llfunc, args.data(), args.size(), ("@subscript_op_call_" + tmpIdxStr).c_str());
		} else {
			target = co_await loadRef(target, info->targetType);
			subscript = co_await loadRef(subscript, info->indexType);
			result = LLVMBuildGEP2(_builders.top().get(), co_await translateType(info->targetType->destroyReferences()->follow()), target, &subscript, 1, ("@subscript_access_" + tmpIdxStr).c_str());
		}
	}

	popDebugLocation();
	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileSuperClassFetch(std::shared_ptr<AltaCore::AST::SuperClassFetch> node, std::shared_ptr<AltaCore::DH::SuperClassFetch> info) {
	auto thisType = std::make_shared<AltaCore::DET::Type>(info->klass)->reference();
	auto parentType = std::make_shared<AltaCore::DET::Type>(info->superclass)->reference();
	co_return co_await doParentRetrieval(_thisContextValue.top(), thisType, parentType);
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileInstanceofExpression(std::shared_ptr<AltaCore::AST::InstanceofExpression> node, std::shared_ptr<AltaCore::DH::InstanceofExpression> info) {
	pushDebugLocation(node->position, info->inputScope);

	auto targetType = AltaCore::DET::Type::getUnderlyingType(info->target.get());
	auto& testType = info->type->type;
	auto tmpIdx = nextTemp();
	auto tmpIdxStr = std::to_string(tmpIdx);

	LLVMValueRef result = co_await compileNode(node->target, info->target);
	auto llfalse = LLVMConstInt(LLVMInt1TypeInContext(_llcontext.get()), 0, false);
	auto lltrue = LLVMConstInt(LLVMInt1TypeInContext(_llcontext.get()), 1, false);

	if (testType->isUnion()) {
		if (targetType->isUnion()) {
			result = co_await loadRef(result, targetType);
			result = LLVMBuildExtractValue(_builders.top().get(), result, 0, ("@instof_source_tag_" + tmpIdxStr).c_str());

			auto targetIndex = result;

			auto targetIndexType = LLVMIntTypeInContext(_llcontext.get(), std::bit_width(targetType->unionOf.size() - 1));

			// check if any of the type IDs match
			auto testResult = llfalse;
			for (size_t i = 0; i < testType->unionOf.size(); ++i) {
				const auto& currTest = testType->unionOf[i];

				for (size_t j = 0; j < targetType->unionOf.size(); ++j) {
					const auto& currTarget = targetType->unionOf[j];

					if (*currTest == *currTarget) {
						auto test = LLVMBuildICmp(_builders.top().get(), LLVMIntEQ, targetIndex, LLVMConstInt(targetIndexType, j, false), ("@instof_union_cmp_" + tmpIdxStr + "_alt_" + std::to_string(j)).c_str());
						testResult = LLVMBuildOr(_builders.top().get(), testResult, test, ("@instof_union_or_" + tmpIdxStr + "_alt_" + std::to_string(j)).c_str());
					}
				}
			}

			result = testResult;
		} else {
			result = llfalse;
			for (const auto& item: testType->unionOf) {
				if (item->isCompatibleWith(*targetType)) {
					result = lltrue;
					break;
				}
			}
		}
	} else if (targetType->isUnion()) {
		result = co_await loadRef(result, targetType);
		result = LLVMBuildExtractValue(_builders.top().get(), result, 0, ("@instof_source_tag_" + tmpIdxStr).c_str());

		auto targetIndex = result;

		auto targetIndexType = LLVMIntTypeInContext(_llcontext.get(), std::bit_width(targetType->unionOf.size() - 1));

		// check if any of the type IDs match
		auto testResult = llfalse;
		for (size_t j = 0; j < targetType->unionOf.size(); ++j) {
			const auto& currTarget = targetType->unionOf[j];

			if (*testType == *currTarget) {
				auto test = LLVMBuildICmp(_builders.top().get(), LLVMIntEQ, targetIndex, LLVMConstInt(targetIndexType, j, false), ("@instof_union_cmp_" + tmpIdxStr + "_alt_" + std::to_string(j)).c_str());
				testResult = LLVMBuildOr(_builders.top().get(), testResult, test, ("@instof_union_or_" + tmpIdxStr + "_alt_" + std::to_string(j)).c_str());
			}
		}

		result = testResult;
	} else if (targetType->isNative != testType->isNative) {
		result = llfalse;
	} else if (targetType->isNative) {
		result = (*testType == *targetType) ? lltrue : llfalse;
	} else if (targetType->klass->id == testType->klass->id) {
		result = lltrue;
	} else if (targetType->klass->hasParent(testType->klass)) {
		result = lltrue;
	} else if (testType->klass->hasParent(targetType->klass)) {
		bool didRetrieval = false;

		result = co_await doChildRetrieval(result, targetType, testType->destroyIndirection()->point(), &didRetrieval);

		if (!didRetrieval) {
			result = llfalse;
		} else {
			result = LLVMBuildIsNotNull(_builders.top().get(), result, ("@instof_downcast_not_null_" + tmpIdxStr).c_str());
		}
	} else {
		result = llfalse;
	}

	popDebugLocation();
	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileForLoopStatement(std::shared_ptr<AltaCore::AST::ForLoopStatement> node, std::shared_ptr<AltaCore::DH::ForLoopStatement> info) {
	// TODO
	std::cerr << "TODO: ForLoopStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileRangedForLoopStatement(std::shared_ptr<AltaCore::AST::RangedForLoopStatement> node, std::shared_ptr<AltaCore::DH::RangedForLoopStatement> info) {
	pushDebugLocation(node->position, info->inputScope);

	auto llfunc = LLVMGetBasicBlockParent(LLVMGetInsertBlock(_builders.top().get()));
	auto tmpIdx = nextTemp();
	auto tmpIdxStr = std::to_string(tmpIdx);

	auto condBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@ranged_for_cond_" + tmpIdxStr).c_str());
	auto nextBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@ranged_for_" + std::string(info->end ? "incdec" : "next") + "_" + tmpIdxStr).c_str());
	auto bodyBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@ranged_for_body_" + tmpIdxStr).c_str());
	auto endBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@ranged_for_end_" + tmpIdxStr).c_str());
	auto doneBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@ranged_for_done_" + tmpIdxStr).c_str());

	pushStack(ScopeStack::Type::Other);

	LLVMValueRef startVar = NULL;
	LLVMValueRef generator = NULL;

	std::shared_ptr<AltaCore::DET::Type> genType = nullptr;

	if (node->end) {
		auto llstart = co_await compileNode(node->start, info->start);
		auto castedStart = co_await cast(llstart, AltaCore::DET::Type::getUnderlyingType(info->start.get()), info->counterType->type, true, additionalCopyInfo(node->start, info->start), false, &node->start->position);
		startVar = co_await tmpify(castedStart, info->counterType->type, true);
		_definedVariables[info->counter->id] = startVar;

		auto debugCounter = LLVMDIBuilderCreateAutoVariable(_debugBuilder.get(), translateScope(info->wrapperScope), info->counter->name.c_str(), info->counter->name.size(), debugFileForScopeItem(info->counter), node->position.line, translateTypeDebug(info->counter->type), false, LLVMDIFlagZero, 0);
		auto debugLoc = LLVMDIBuilderCreateDebugLocation(_llcontext.get(), node->position.line, node->position.column, translateScope(info->wrapperScope), NULL);
		LLVMDIBuilderInsertDeclareAtEnd(_debugBuilder.get(), startVar, debugCounter, LLVMDIBuilderCreateExpression(_debugBuilder.get(), NULL, 0), debugLoc, LLVMGetInsertBlock(_builders.top().get()));

		LLVMSetValueName(startVar, ("@ranged_for_counter_" + tmpIdxStr).c_str());
	} else {
		generator = co_await compileNode(node->start, info->start);
		if (info->generatorType->referenceLevel() > 0) {
			generator = co_await loadRef(generator, info->generatorType, 1);
		} else {
			generator = co_await tmpify(generator, info->generatorType, true);
		}
		genType = info->generatorType->destroyReferences();

		LLVMSetValueName(generator, ("@ranged_for_iterator_" + tmpIdxStr).c_str());
	}

	auto entryState = inSuspendableFunction() ? _suspendableStateIndex.top() : SIZE_MAX;

	LLVMBuildBr(_builders.top().get(), condBlock);

	LLVMPositionBuilderAtEnd(_builders.top().get(), condBlock);

	// before we go for another iteration, reset the state to what it was when we created the loop.
	if (inSuspendableFunction()) {
		co_await updateSuspendableStateIndex(entryState);
	}

	pushStack(ScopeStack::Type::Other);
	pushLoop(doneBlock, nextBlock);

	LLVMValueRef cmp = NULL;

	LLVMValueRef iteratorCall = NULL;

	// build the condition
	if (info->end) {
		auto llend = co_await compileNode(node->end, info->end);
		auto castedEnd = co_await cast(llend, AltaCore::DET::Type::getUnderlyingType(info->end.get()), info->counterType->type, true, additionalCopyInfo(node->end, info->end), false, &node->end->position);
		auto endVal = co_await loadRef(castedEnd, info->counterType->type);

		auto startVal = co_await loadRef(startVar, info->counterType->type->reference());

		if (info->counterType->type->isFloatingPoint()) {
			LLVMRealPredicate pred;

			if (node->decrement) {
				if (node->inclusive) {
					pred = LLVMRealUGE;
				} else {
					pred = LLVMRealUGT;
				}
			} else {
				if (node->inclusive) {
					pred = LLVMRealULE;
				} else {
					pred = LLVMRealULT;
				}
			}

			cmp = LLVMBuildFCmp(_builders.top().get(), pred, startVal, endVal, ("@ranged_for_cmp_" + tmpIdxStr).c_str());
		} else {
			LLVMIntPredicate pred;
			bool isSigned = info->counterType->type->isSigned();

			if (node->decrement) {
				if (node->inclusive) {
					pred = isSigned ? LLVMIntSGE : LLVMIntUGE;
				} else {
					pred = isSigned ? LLVMIntSGT : LLVMIntUGT;
				}
			} else {
				if (node->inclusive) {
					pred = isSigned ? LLVMIntSLE : LLVMIntULE;
				} else {
					pred = isSigned ? LLVMIntSLT : LLVMIntULT;
				}
			}

			cmp = LLVMBuildICmp(_builders.top().get(), pred, startVal, endVal, ("@ranged_for_cmp_" + tmpIdxStr).c_str());
		}
	} else {
		auto [nextFuncType, nextFunc] = co_await declareFunction(info->next);

		std::array<LLVMValueRef, 1> args {
			generator,
		};

		iteratorCall = LLVMBuildCall2(_builders.top().get(), nextFuncType, nextFunc, args.data(), args.size(), ("@ranged_for_iterator_next_" + tmpIdxStr).c_str());
		cmp = LLVMBuildExtractValue(_builders.top().get(), iteratorCall, 0, ("@ranged_for_iterator_val_present_" + tmpIdxStr).c_str());
	}

	LLVMBuildCondBr(_builders.top().get(), cmp, bodyBlock, endBlock);

	// build the end block now so we can cleanup the scope without having to use `startBranch` and `endBranch`
	LLVMPositionBuilderAtEnd(_builders.top().get(), endBlock);
	co_await currentStack().cleanup();
	LLVMBuildBr(_builders.top().get(), doneBlock);

	LLVMPositionBuilderAtEnd(_builders.top().get(), nextBlock);

	if (info->end) {
		auto counterType = co_await translateType(info->counterType->type);
		auto llcounter = LLVMBuildLoad2(_builders.top().get(), co_await translateType(info->counterType->type), startVar, ("@ranged_for_" + tmpIdxStr + "_counter_load").c_str());
		bool isFP = info->counterType->type->isFloatingPoint();
		auto llone = isFP ? LLVMConstReal(counterType, 1) : LLVMConstInt(counterType, 1, false);
		auto llop = LLVMBuildBinOp(_builders.top().get(), node->decrement ? (isFP ? LLVMFSub : LLVMSub) : (isFP ? LLVMFAdd : LLVMAdd), llcounter, llone, ("@ranged_for_" + tmpIdxStr + (node->decrement ? "_dec" : "_inc")).c_str());
		LLVMBuildStore(_builders.top().get(), llop, startVar);
	} else {
		// for iterator-based ranged-for loops, we just go to the condition;
		// it will take care of advancing the iterator.
	}

	LLVMBuildBr(_builders.top().get(), condBlock);

	// now build the body
	LLVMPositionBuilderAtEnd(_builders.top().get(), bodyBlock);

	if (!info->end) {
		auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
		auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());

		auto tmpified = co_await tmpify(iteratorCall, info->next->returnType, true);

		std::array<LLVMValueRef, 2> valGEPIndices {
			LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
			LLVMConstInt(gepStructIndexType, 1, false), // @Optional@::value
		};

		auto valGEP = LLVMBuildGEP2(_builders.top().get(), co_await translateType(info->next->returnType, true, false), tmpified, valGEPIndices.data(), valGEPIndices.size(), ("@ranged_for_iterator_val_" + tmpIdxStr).c_str());
		auto val = co_await cast(valGEP, info->next->returnType->optionalTarget->reference(true), info->next->returnType->optionalTarget, true, std::make_pair(true, false), false, &node->position);
		auto valVar = co_await tmpify(val, info->next->returnType->optionalTarget, true, true);

		if (!info->counterType->type->isExactlyCompatibleWith(*info->next->returnType->optionalTarget)) {
			auto casted = co_await cast(valVar, info->next->returnType->optionalTarget->reference(true), info->counterType->type, true, std::make_pair(true, false), false, &node->position);
			valVar = co_await tmpify(casted, info->counterType->type, true, true);
		}

		_definedVariables[info->counter->id] = valVar;
	}

	co_await compileNode(node->body, info->body);
	co_await currentStack().cleanup();
	LLVMBuildBr(_builders.top().get(), nextBlock);

	popLoop();
	co_await popStack();

	// now build the done block
	LLVMPositionBuilderAtEnd(_builders.top().get(), doneBlock);
	co_await currentStack().cleanup();

	// after a loop, we have to create a new state for the merged control flow (just like we do for conditionals)
	if (inSuspendableFunction()) {
		co_await updateSuspendableStateIndex(++_suspendableStateIndex.top());
	}

	co_await popStack();

	popDebugLocation();
	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileUnaryOperation(std::shared_ptr<AltaCore::AST::UnaryOperation> node, std::shared_ptr<AltaCore::DH::UnaryOperation> info) {
	pushDebugLocation(node->position, info->inputScope);

	LLVMValueRef result = NULL;
	auto compiled = co_await compileNode(node->target, info->target);
	auto tmpIdx = nextTemp();
	auto tmpIdxStr = std::to_string(tmpIdx);

	if (info->operatorMethod) {
		if (info->targetType->referenceLevel() == 0) {
			compiled = co_await tmpify(compiled, info->targetType);
		}

		auto [llfuncType, llfunc] = co_await declareFunction(info->operatorMethod);

		std::array<LLVMValueRef, 1> args {
			co_await cast(compiled, info->targetType->reference(), info->operatorMethod->parentClassType, false, std::make_pair(false, false), false, &node->target->position),
		};

		result = LLVMBuildCall2(_builders.top().get(), llfuncType, llfunc, args.data(), args.size(), ("@unop_call_" + tmpIdxStr).c_str());
	} else {
		switch (node->type) {
			case AltaCore::Shared::UOperatorType::Not: {
				auto llasBool = co_await cast(compiled, info->targetType, std::make_shared<AltaCore::DET::Type>(AltaCore::DET::NativeType::Bool), false, additionalCopyInfo(node->target, info->target), false, &node->target->position);
				result = LLVMBuildNot(_builders.top().get(), llasBool, ("@unop_not_" + tmpIdxStr).c_str());
			} break;

			case AltaCore::Shared::UOperatorType::Plus: {
				// no-op... almost
				result = co_await loadRef(compiled, info->targetType);
			} break;

			case AltaCore::Shared::UOperatorType::Minus: {
				compiled = co_await loadRef(compiled, info->targetType);
				result = (info->targetType->isFloatingPoint() ? LLVMBuildFNeg : LLVMBuildNeg)(_builders.top().get(), compiled, ("@unop_neg_" + tmpIdxStr).c_str());
			} break;

			case AltaCore::Shared::UOperatorType::PreIncrement:
			case AltaCore::Shared::UOperatorType::PreDecrement:
			case AltaCore::Shared::UOperatorType::PostIncrement:
			case AltaCore::Shared::UOperatorType::PostDecrement: {
				auto tmp = co_await loadRef(compiled, info->targetType);
				auto origTmp = tmp;
				auto lltype = co_await translateType(info->targetType->destroyReferences());
				auto llorigType = lltype;

				bool isDecrement = (node->type == AltaCore::Shared::UOperatorType::PreDecrement) || (node->type == AltaCore::Shared::UOperatorType::PostDecrement);
				bool isPost = (node->type == AltaCore::Shared::UOperatorType::PostIncrement) || (node->type == AltaCore::Shared::UOperatorType::PostDecrement);
				bool isFP = info->targetType->isFloatingPoint();

				if (info->targetType->pointerLevel() > 0) {
					lltype = LLVMInt64TypeInContext(_llcontext.get());
					tmp = LLVMBuildPtrToInt(_builders.top().get(), tmp, lltype, ("@unop_incdec_" + tmpIdxStr + "_cast_source").c_str());
				}

				auto buildOp = (isDecrement ? (isFP ? LLVMBuildFSub : LLVMBuildSub) : (isFP ? LLVMBuildFAdd : LLVMBuildAdd));

				auto inc = buildOp(_builders.top().get(), tmp, LLVMConstInt(lltype, 1, false), ("@unop_incdec_" + tmpIdxStr).c_str());

				if (info->targetType->pointerLevel() > 0) {
					inc = LLVMBuildIntToPtr(_builders.top().get(), inc, llorigType, ("@unop_incdec_" + tmpIdxStr + "_cast_dest").c_str());
				}

				if (info->targetType->referenceLevel() > 0) {
					LLVMBuildStore(_builders.top().get(), inc, compiled);
				}

				result = (isPost ? origTmp : inc);
			} break;

			case AltaCore::Shared::UOperatorType::BitwiseNot: {
				compiled = co_await loadRef(compiled, info->targetType);
				result = LLVMBuildNot(_builders.top().get(), compiled, ("@unop_bitnot_" + tmpIdxStr).c_str());
			} break;
		}
	}

	popDebugLocation();
	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileSizeofOperation(std::shared_ptr<AltaCore::AST::SizeofOperation> node, std::shared_ptr<AltaCore::DH::SizeofOperation> info) {
	co_return LLVMSizeOf(co_await translateType(info->target->type));
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileFloatingPointLiteralNode(std::shared_ptr<AltaCore::AST::FloatingPointLiteralNode> node, std::shared_ptr<AltaCore::DH::FloatingPointLiteralNode> info) {
	auto type = AltaCore::DET::Type::getUnderlyingType(info.get());
	co_return LLVMConstRealOfStringAndSize(co_await translateType(type), node->raw.c_str(), node->raw.size());
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileStructureDefinitionStatement(std::shared_ptr<AltaCore::AST::StructureDefinitionStatement> node, std::shared_ptr<AltaCore::DH::StructureDefinitionStatement> info) {
	auto llstructType = co_await defineClassType(info->structure);
	std::vector<LLVMTypeRef> members;

	for (const auto& member: info->memberTypes) {
		members.push_back(co_await translateType(member->type));
	}

	LLVMStructSetBody(llstructType, members.data(), members.size(), false);

	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileVariableDeclarationStatement(std::shared_ptr<AltaCore::AST::VariableDeclarationStatement> node, std::shared_ptr<AltaCore::DH::VariableDeclarationStatement> info) {
	bool inModuleRoot = !info->variable->parentScope.expired() && !info->variable->parentScope.lock()->parentModule.expired();
	auto lltype = co_await translateType(info->variable->type);
	auto mangled = mangleName(info->variable);
	LLVMValueRef memory = NULL;

	if (!(inModuleRoot && info->variable->isLiteral)) {
		throw AltaCore::Errors::ValidationError("Variable declarations are only support for global variables", node->position);
	}

	if (!_definedVariables[info->variable->id]) {
		memory = LLVMAddGlobal(_llmod.get(), lltype, mangled.c_str());
		LLVMSetLinkage(memory, LLVMExternalLinkage);

		_definedVariables[info->variable->id] = memory;
	} else {
		memory = _definedVariables[info->variable->id];
	}

	auto gve = LLVMDIBuilderCreateGlobalVariableExpression(_debugBuilder.get(), translateParentScope(info->variable), info->variable->name.c_str(), info->variable->name.size(), mangled.c_str(), mangled.size(), debugFileForScopeItem(info->variable), node->position.line, translateTypeDebug(info->variable->type), false, LLVMDIBuilderCreateExpression(_debugBuilder.get(), NULL, 0), NULL, 0);
	LLVMGlobalSetMetadata(memory, llvm::LLVMContext::MD_dbg, gve);

	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileDeleteStatement(std::shared_ptr<AltaCore::AST::DeleteStatement> node, std::shared_ptr<AltaCore::DH::DeleteStatement> info) {
	auto target = co_await compileNode(node->target, info->target);
	auto targetType = info->targetType;

	target = co_await loadRef(target, info->targetType, (info->persistent) ? 0 : 1);
	targetType = targetType->destroyReferences();

	if (info->persistent) {
		targetType = targetType->follow();
	} else if (info->targetType->referenceLevel() == 0) {
		throw std::runtime_error("TODO: support non-referential non-persistent delete statements");
	}

	co_await doDtor(target, targetType);

	if (info->persistent) {
		auto root = co_await getRootInstance(target, info->persistent ? targetType->reference() : targetType);
		LLVMBuildFree(_builders.top().get(), root);
	}

	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileControlDirective(std::shared_ptr<AltaCore::AST::ControlDirective> node, std::shared_ptr<AltaCore::DH::Node> info) {
	pushDebugLocation(node->position, info->inputScope);

	if (_loops.empty()) {
		throw std::runtime_error("Found control directive (break/continue) but no loop found");
	}

	auto& loop = currentLoop();

	// clean up all the scopes up to and including the loop's root scope
	for (auto rit = _stacks.rbegin(); rit != _stacks.rend(); ++rit) {
		co_await rit->cleanup();
		if (&*rit == loop.rootScope) {
			break;
		}
	}

	// now branch to the target block
	LLVMBuildBr(_builders.top().get(), node->isBreak ? loop.breakBlock : loop.continueBlock);

	// insert a dummy block in case we generate code after the continue or break.
	//
	// this is just for the sake of being able to compile; this could would never actually be executed.
	auto tmpIdx = nextTemp();
	auto tmpIdxStr = std::to_string(tmpIdx);
	auto dummy = LLVMAppendBasicBlockInContext(_llcontext.get(), LLVMGetBasicBlockParent(LLVMGetInsertBlock(_builders.top().get())), ("@control_dummy_block_" + tmpIdxStr).c_str());
	LLVMPositionBuilderAtEnd(_builders.top().get(), dummy);

	popDebugLocation();
	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileTryCatchBlock(std::shared_ptr<AltaCore::AST::TryCatchBlock> node, std::shared_ptr<AltaCore::DH::TryCatchBlock> info) {
	// TODO
	std::cerr << "TODO: TryCatchBlock" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileThrowStatement(std::shared_ptr<AltaCore::AST::ThrowStatement> node, std::shared_ptr<AltaCore::DH::ThrowStatement> info) {
	// TODO
	std::cerr << "TODO: ThrowStatement" << std::endl;
	LLVMBuildUnreachable(_builders.top().get());
	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileNullptrExpression(std::shared_ptr<AltaCore::AST::NullptrExpression> node, std::shared_ptr<AltaCore::DH::NullptrExpression> info) {
	auto type = AltaCore::DET::Type::getUnderlyingType(info.get());
	auto lltype = co_await translateType(type);
	co_return LLVMConstNull(lltype);
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileCodeLiteralNode(std::shared_ptr<AltaCore::AST::CodeLiteralNode> node, std::shared_ptr<AltaCore::DH::CodeLiteralNode> info) {
	throw std::runtime_error("CodeLiteralNode is not supported when compiling to machine code");
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileBitfieldDefinitionNode(std::shared_ptr<AltaCore::AST::BitfieldDefinitionNode> node, std::shared_ptr<AltaCore::DH::BitfieldDefinitionNode> info) {
	auto lltype = co_await defineClassType(info->bitfield);
	// XXX: is there anything else we should define/declare for bitfields?
	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileLambdaExpression(std::shared_ptr<AltaCore::AST::LambdaExpression> node, std::shared_ptr<AltaCore::DH::LambdaExpression> info) {
	auto lllambdaType = co_await translateType(AltaCore::DET::Type::getUnderlyingType(info.get()));

	auto retType = co_await translateType(info->function->returnType, true, false);

	if (info->function->isGenerator || info->function->isAsync) {
		throw std::runtime_error("TODO: generator lambdas");
	}

	_suspendableContext.push(nullptr);

	std::vector<LLVMTypeRef> lambdaStateMembers {
		_definedTypes["_Alta_basic_lambda_state"],
	};

	for (const auto& ref: info->function->referencedVariables) {
		// TODO: filter out copied variables from the referenced variables vector in a higher layer (e.g. AltaCore)
		bool found = false;
		for (const auto& copy: info->function->copiedVariables) {
			if (copy->id == ref->id) {
				found = true;
				break;
			}
		}
		if (found) {
			continue;
		}

		auto target = co_await translateType(ref->type);
		lambdaStateMembers.push_back(LLVMPointerType(target, 0));
	}

	for (const auto& copy: info->function->copiedVariables) {
		auto target = co_await translateType(copy->type);
		lambdaStateMembers.push_back(target);
	}

	auto lambdaStateType = LLVMStructTypeInContext(_llcontext.get(), lambdaStateMembers.data(), lambdaStateMembers.size(), false);

	auto mangled = mangleName(info->function);
	auto [llfuncType, llfunc] = co_await declareFunction(info->function);

	auto llfuncDebug = translateFunction(info->function, true);
	LLVMSetSubprogram(llfunc, llfuncDebug);

	LLVMSetLinkage(llfunc, LLVMInternalLinkage);

	auto entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "@entry");
	auto builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
	LLVMPositionBuilderAtEnd(builder.get(), entryBlock);

	_builders.push(builder);
	pushStack(ScopeStack::Type::Function);
	pushUnknownDebugLocation(info->function->scope);

	auto lambdaState = LLVMGetParam(llfunc, 0);

	auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
	auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());

	size_t refCount = 0;

	for (size_t i = 0; i < info->function->referencedVariables.size(); ++i) {
		const auto& var = info->function->referencedVariables[i];

		bool found = false;
		for (const auto& copy: info->function->copiedVariables) {
			if (copy->id == var->id) {
				found = true;
				break;
			}
		}
		if (found) {
			continue;
		}

		++refCount;

		auto type = co_await translateType(var->type);
		std::array<LLVMValueRef, 2> gepIndices {
			LLVMConstInt(gepIndexType, 0, false),
			LLVMConstInt(gepStructIndexType, i + 1, false),
		};
		auto gep = LLVMBuildGEP2(_builders.top().get(), lambdaStateType, lambdaState, gepIndices.data(), gepIndices.size(), ("@lambda_ref_gep_" + std::to_string(i)).c_str());
		auto ptr = LLVMBuildLoad2(_builders.top().get(), LLVMPointerType(type, 0), gep, ("@lambda_ref@" + mangleName(var)).c_str());
		_definedVariables["@lambda_ref@" + mangleName(info->function) + "@" + var->id] = ptr;
	}

	for (size_t i = 0; i < info->function->copiedVariables.size(); ++i) {
		const auto& var = info->function->copiedVariables[i];
		std::array<LLVMValueRef, 2> gepIndices {
			LLVMConstInt(gepIndexType, 0, false),
			LLVMConstInt(gepStructIndexType, refCount + i + 1, false),
		};
		auto gep = LLVMBuildGEP2(_builders.top().get(), lambdaStateType, lambdaState, gepIndices.data(), gepIndices.size(), ("@lambda_copy@" + mangleName(var)).c_str());
		_definedVariables["@lambda_copy@" + mangleName(info->function) + "@" + var->id] = gep;
	}

	size_t llparamIndex = 1;
	for (size_t i = 0; i < info->parameters.size(); ++i) {
		const auto& param = node->parameters[i];
		const auto& paramInfo = info->parameters[i];
		const auto& var = info->function->parameterVariables[i];

		auto llparam = LLVMGetParam(llfunc, llparamIndex);
		++llparamIndex;

		auto paramName = (param->isVariable ? "@len_" : "") + param->name;
		auto paramNameDebug = std::regex_replace(paramName, std::regex("@"), "__");

		LLVMSetValueName(llparam, paramName.c_str());

		auto paramTypeDebug = param->isVariable ? LLVMDIBuilderCreateBasicType(_debugBuilder.get(), "u64", 3, 64, llvm::dwarf::DW_ATE_unsigned, LLVMDIFlagZero) : translateTypeDebug(var->type);
		auto llparamDebug = LLVMDIBuilderCreateParameterVariable(_debugBuilder.get(), translateScope(info->function->scope), paramNameDebug.c_str(), paramNameDebug.size(), llparamIndex, debugFileForScope(info->function->scope), param->position.line, paramTypeDebug, false, LLVMDIFlagZero);

		auto paramLoc = LLVMDIBuilderCreateDebugLocation(_llcontext.get(), param->position.line, param->position.column, translateScope(info->function->scope), NULL);

		LLVMValueRef llparamLen = NULL;

		if (param->isVariable) {
			auto tmp = co_await tmpify(llparam, std::make_shared<AltaCore::DET::Type>(AltaCore::DET::NativeType::Integer, std::vector<uint8_t> { static_cast<uint8_t>(AltaCore::DET::TypeModifierFlag::Long), static_cast<uint8_t>(AltaCore::DET::TypeModifierFlag::Long) }), false, true);

			LLVMDIBuilderInsertDeclareAtEnd(_debugBuilder.get(), tmp, llparamDebug, LLVMDIBuilderCreateExpression(_debugBuilder.get(), NULL, 0), paramLoc, entryBlock);

			llparamLen = llparam;
			llparam = LLVMGetParam(llfunc, llparamIndex);
			++llparamIndex;

			LLVMSetValueName(llparam, param->name.c_str());

			paramNameDebug = std::regex_replace(param->name, std::regex("@"), "__");

			auto elmDebugType = translateTypeDebug(var->type->followBlindly());
			paramTypeDebug = LLVMDIBuilderCreateArrayType(_debugBuilder.get(), 0, LLVMDITypeGetAlignInBits(elmDebugType), elmDebugType, NULL, 0);
			llparamDebug = LLVMDIBuilderCreateParameterVariable(_debugBuilder.get(), translateScope(info->function->scope), paramNameDebug.c_str(), paramNameDebug.size(), llparamIndex, debugFileForScope(info->function->scope), param->position.line, paramTypeDebug, false, LLVMDIFlagZero);
		}

		if (!param->isVariable) {
			llparam = co_await tmpify(llparam, var->type, false, true);
			currentStack().pushItem(llparam, var->type);
			LLVMSetValueName(llparam, (param->name + "@on_stack").c_str());
		} else if (param->isVariable) {
			currentStack().pushRuntimeArray(llparam, llparamLen, var->type, LLVMInt64TypeInContext(_llcontext.get()));
		}

		LLVMDIBuilderInsertDeclareAtEnd(_debugBuilder.get(), llparam, llparamDebug, LLVMDIBuilderCreateExpression(_debugBuilder.get(), NULL, 0), paramLoc, entryBlock);

		_definedVariables[var->id] = llparam;
		if (llparamLen) {
			_definedVariables["_Alta_array_length_" + var->id] = llparamLen;
		}
	}

	pushDebugLocation(node->body->position, info->function->scope);

	co_await compileNode(node->body, info->body);

	popDebugLocation();

	auto functionReturnType = info->function->isGenerator ? info->function->generatorReturnType : (info->function->isAsync ? info->function->coroutineReturnType : info->function->returnType);

	if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(_builders.top().get()))) {
		if (functionReturnType && *functionReturnType == AltaCore::DET::Type(AltaCore::DET::NativeType::Void)) {
			// insert an implicit return
			co_await currentStack().cleanup();

			LLVMBuildRetVoid(_builders.top().get());
		} else {
			LLVMBuildUnreachable(_builders.top().get());
		}
	}

	popDebugLocation();
	co_await popStack();
	_builders.pop();

	auto lambdaFunc = llfunc;

	auto mangledDtor = "@dtor@" + mangled;
	std::array<LLVMTypeRef, 1> dtorParams {
		LLVMPointerType(lambdaStateType, 0),
	};
	auto lambdaDtorFunc = llfunc = LLVMAddFunction(_llmod.get(), mangledDtor.c_str(), LLVMFunctionType(LLVMVoidTypeInContext(_llcontext.get()), dtorParams.data(), dtorParams.size(), false));
	_definedFunctions[mangledDtor] = llfunc;

	LLVMSetLinkage(llfunc, LLVMInternalLinkage);

	entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "@entry");
	builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
	LLVMPositionBuilderAtEnd(builder.get(), entryBlock);

	_builders.push(builder);
	pushStack(ScopeStack::Type::Function);
	pushUnknownDebugLocation(info->function->scope);

	lambdaState = LLVMGetParam(llfunc, 0);

	for (size_t i = info->function->copiedVariables.size(); i > 0; --i) {
		auto idx = i - 1;
		const auto& var = info->function->copiedVariables[idx];
		std::array<LLVMValueRef, 2> gepIndices {
			LLVMConstInt(gepIndexType, 0, false),
			LLVMConstInt(gepStructIndexType, refCount + idx + 1, false),
		};
		auto gep = LLVMBuildGEP2(_builders.top().get(), lambdaStateType, lambdaState, gepIndices.data(), gepIndices.size(), ("@lambda_copy@" + mangleName(var)).c_str());

		co_await doDtor(gep, var->type);
	}

	co_await currentStack().cleanup();

	LLVMBuildFree(_builders.top().get(), lambdaState);

	LLVMBuildRetVoid(_builders.top().get());

	popDebugLocation();
	co_await popStack();
	_builders.pop();

	lambdaState = LLVMBuildMalloc(_builders.top().get(), lambdaStateType, ("@lambda_state@" + mangled).c_str());

	std::array<LLVMValueRef, 2> basicStateGEPIndices {
		LLVMConstInt(gepIndexType, 0, false), // the first "array" member
		LLVMConstInt(gepStructIndexType, 0, false), // <lambda state>::_Alta_basic_lambda_state
	};
	auto basicState = LLVMBuildGEP2(_builders.top().get(), lambdaStateType, lambdaState, basicStateGEPIndices.data(), basicStateGEPIndices.size(), ("@lambda_basic_state@" + mangled).c_str());
	std::array<LLVMValueRef, 2> basicStateInitMembers {
		LLVMConstInt(LLVMInt64TypeInContext(_llcontext.get()), 1, false),
		lambdaDtorFunc,
	};
	LLVMBuildStore(_builders.top().get(), LLVMConstNamedStruct(_definedTypes["_Alta_basic_lambda_state"], basicStateInitMembers.data(), basicStateInitMembers.size()), basicState);

	for (size_t i = 0; i < info->function->referencedVariables.size(); ++i) {
		const auto& var = info->function->referencedVariables[i];

		bool found = false;
		for (const auto& copy: info->function->copiedVariables) {
			if (copy->id == var->id) {
				found = true;
				break;
			}
		}
		if (found) {
			continue;
		}

		std::array<LLVMValueRef, 2> gepIndices {
			LLVMConstInt(gepIndexType, 0, false),
			LLVMConstInt(gepStructIndexType, i + 1, false),
		};
		auto gep = LLVMBuildGEP2(_builders.top().get(), lambdaStateType, lambdaState, gepIndices.data(), gepIndices.size(), ("@lambda_init_ref_gep@" + std::to_string(i) + "@" + mangled).c_str());
		LLVMBuildStore(_builders.top().get(), _definedVariables[var->id], gep);
	}

	for (size_t i = 0; i < info->function->copiedVariables.size(); ++i) {
		const auto& var = info->function->copiedVariables[i];

		std::array<LLVMValueRef, 2> gepIndices {
			LLVMConstInt(gepIndexType, 0, false),
			LLVMConstInt(gepStructIndexType, refCount + i + 1, false),
		};
		auto gep = LLVMBuildGEP2(_builders.top().get(), lambdaStateType, lambdaState, gepIndices.data(), gepIndices.size(), ("@lambda_init_copy_gep@" + std::to_string(i) + "@" + mangled).c_str());
		bool didCopy = false;
		auto copy = co_await doCopyCtor(_definedVariables[var->id], var->type->reference(true), std::make_pair(true, false), &didCopy);
		if (!didCopy) {
			copy = LLVMBuildLoad2(_builders.top().get(), co_await translateType(var->type), _definedVariables[var->id], ("@lambda_var_load@" + mangleName(var)).c_str());
		}
		LLVMBuildStore(_builders.top().get(), copy, gep);
	}

	auto voidPointerType = LLVMPointerType(LLVMVoidTypeInContext(_llcontext.get()), 0);
	auto lambda = LLVMGetUndef(_definedTypes["_Alta_basic_function"]);
	lambda = LLVMBuildInsertValue(_builders.top().get(), lambda, lambdaFunc, 0, ("@lambda_init_func@" + mangled).c_str());
	lambda = LLVMBuildInsertValue(_builders.top().get(), lambda, lambdaState, 1, ("@lambda@" + mangled).c_str());

	_suspendableContext.pop();

	co_return lambda;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileSpecialFetchExpression(std::shared_ptr<AltaCore::AST::SpecialFetchExpression> node, std::shared_ptr<AltaCore::DH::SpecialFetchExpression> info) {
	pushDebugLocation(node->position, info->inputScope);

	if (invalidValueExpressionTable.find(info->id) != invalidValueExpressionTable.end()) {
		popDebugLocation();
		co_return LLVMConstNull(co_await translateType(invalidValueExpressionTable[info->id]));
	} else if (info->items.size() == 1 && info->items.front()->id == AltaCore::Util::getModule(info->inputScope.get()).lock()->internal.schedulerVariable->id) {
		throw std::runtime_error("TODO: scheduler fetch");
	} else if (info->items.size() == 1 && info->items.front()->name == "$coroutine") {
		throw std::runtime_error("TODO: coroutine fetch");
	} else {
		auto result = _definedVariables[info->items.front()->id];

		if (!result) {
			throw std::runtime_error("special fetch variable not defined");
		}

		popDebugLocation();
		co_return result;
	}
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileClassOperatorDefinitionStatement(std::shared_ptr<AltaCore::AST::ClassOperatorDefinitionStatement> node, std::shared_ptr<AltaCore::DH::ClassOperatorDefinitionStatement> info) {
	auto [llfuncType, llfunc] = co_await declareFunction(info->method);

	_suspendableContext.push(nullptr);

	auto entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "@entry");
	auto builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
	LLVMPositionBuilderAtEnd(builder.get(), entryBlock);

	auto llfuncDebug = translateFunction(info->method, true);
	LLVMSetSubprogram(llfunc, llfuncDebug);

	_builders.push(builder);
	pushStack(ScopeStack::Type::Function);
	pushUnknownDebugLocation(info->method->scope);

	auto selfParam = LLVMGetParam(llfunc, 0);
	LLVMSetValueName(selfParam, "@this");

	_thisContextValue.push(selfParam);

	auto tmpThis = co_await tmpify(selfParam, info->method->parentClassType, false, true);

	auto thisDebugType = LLVMDIBuilderCreateObjectPointerType(_debugBuilder.get(), translateTypeDebug(info->method->parentClassType));
	auto thisDebugVar = LLVMDIBuilderCreateParameterVariable(_debugBuilder.get(), translateScope(info->method->scope), "this", sizeof("this") - 1, 1, debugFileForScopeItem(info->method), node->position.line, thisDebugType, false, LLVMDIFlagZero);
	auto thisDebugLoc = LLVMDIBuilderCreateDebugLocation(_llcontext.get(), node->position.line, node->position.column, translateScope(info->method->scope), NULL);

	LLVMDIBuilderInsertDeclareAtEnd(_debugBuilder.get(), tmpThis, thisDebugVar, LLVMDIBuilderCreateExpression(_debugBuilder.get(), NULL, 0), thisDebugLoc, LLVMGetInsertBlock(_builders.top().get()));

	if (info->method->parameterVariables.size() > 0) {
		const auto& var = info->method->parameterVariables.front();
		auto llparam = LLVMGetParam(llfunc, 1);

		LLVMSetValueName(llparam, "@value");

		llparam = co_await tmpify(llparam, var->type, false, true);
		currentStack().pushItem(llparam, var->type);
		LLVMSetValueName(llparam, "@value@on_stack");

		auto debugVar = LLVMDIBuilderCreateParameterVariable(_debugBuilder.get(), translateParentScope(var), "__value", sizeof("__value") - 1, 2, debugFileForScopeItem(var), node->position.line, translateTypeDebug(var->type), false, LLVMDIFlagZero);
		auto debugLoc = LLVMDIBuilderCreateDebugLocation(_llcontext.get(), node->position.line, node->position.column, translateParentScope(var), NULL);

		LLVMDIBuilderInsertDeclareAtEnd(_debugBuilder.get(), llparam, debugVar, LLVMDIBuilderCreateExpression(_debugBuilder.get(), NULL, 0), debugLoc, LLVMGetInsertBlock(_builders.top().get()));

		_definedVariables[var->id] = llparam;
	}

	pushDebugLocation(node->block->position, info->method->scope);
	co_await compileNode(node->block, info->block);
	popDebugLocation();

	if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(_builders.top().get()))) {
		auto functionReturnType = info->method->isGenerator ? info->method->generatorReturnType : (info->method->isAsync ? info->method->coroutineReturnType : info->method->returnType);

		if (functionReturnType && *functionReturnType == AltaCore::DET::Type(AltaCore::DET::NativeType::Void)) {
			// insert an implicit return
			co_await currentStack().cleanup();

			LLVMBuildRetVoid(_builders.top().get());
		} else {
			LLVMBuildUnreachable(_builders.top().get());
		}
	}

	_thisContextValue.pop();

	popDebugLocation();
	co_await popStack();
	_builders.pop();

	_suspendableContext.pop();

	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileEnumerationDefinitionNode(std::shared_ptr<AltaCore::AST::EnumerationDefinitionNode> node, std::shared_ptr<AltaCore::DH::EnumerationDefinitionNode> info) {
	auto lltype = co_await translateType(info->memberType);
	std::vector<LLVMMetadataRef> debugMembers;

	if (!info->memberType->isNative) {
		throw std::runtime_error("Support non-native enums");
	}

	auto prevVal = LLVMConstInt(lltype, 0, false);

	for (size_t i = 0; i < node->members.size(); ++i) {
		const auto& [key, value] = node->members[i];
		const auto& valueInfo = info->memberDetails[key];
		const auto& memberVar = info->memberVariables[key];

		if (!_definedVariables[memberVar->id]) {
			auto mangled = mangleName(memberVar);
			_definedVariables[memberVar->id] = LLVMAddGlobal(_llmod.get(), lltype, mangled.c_str());
		}

		enterInitFunction();
		auto llval = value ? co_await compileNode(value, valueInfo) : LLVMConstAdd(prevVal, LLVMConstInt(lltype, 1, false));
		exitInitFunction();

		if (!LLVMIsConstant(llval)) {
			throw std::runtime_error("Support non-constant enum values");
		}

		if (value) {
			auto valueType = AltaCore::DET::Type::getUnderlyingType(valueInfo.get());

			if (valueType->referenceLevel() == info->memberType->referenceLevel() + 1) {
				auto initializer = LLVMGetInitializer(llval);
				if (!initializer) {
					throw std::runtime_error("no support for initializing enum values with external constants");
				}
				llval = initializer;
			}
		}

		LLVMSetInitializer(_definedVariables[memberVar->id], llval);
		LLVMSetGlobalConstant(_definedVariables[memberVar->id], true);

		debugMembers.push_back(LLVMDIBuilderCreateEnumerator(_debugBuilder.get(), key.c_str(), key.size(), memberVar->type->isSigned() ? LLVMConstIntGetSExtValue(llval) : (int64_t)LLVMConstIntGetZExtValue(llval), !memberVar->type->isSigned()));

		prevVal = llval;
	}

	LLVMDIBuilderCreateEnumerationType(_debugBuilder.get(), translateParentScope(info->ns), info->ns->name.c_str(), info->ns->name.size(), debugFileForScopeItem(info->ns), node->position.line, LLVMStoreSizeOfType(_targetData.get(), lltype) * 8, LLVMABIAlignmentOfType(_targetData.get(), lltype) * 8, debugMembers.data(), debugMembers.size(), translateTypeDebug(info->memberType));

	// now let's build the lookup functions
	{
		auto [llfuncType, llfunc] = co_await declareFunction(info->ns->enumerationLookupFunction);
		LLVMSetLinkage(llfunc, LLVMInternalLinkage);

		auto param = LLVMGetParam(llfunc, 0);

		auto entry = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "@test0");
		auto builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
		LLVMPositionBuilderAtEnd(builder.get(), entry);
		_builders.push(builder);

		for (size_t i = 0; i < node->members.size(); ++i) {
			const auto& [key, value] = node->members[i];
			const auto& valueInfo = info->memberDetails[key];
			const auto& memberVar = info->memberVariables[key];
			auto var = _definedVariables[memberVar->id];

			auto cmp = buildRawStringsAreEqual(_builders.top(), param, LLVMBuildGlobalStringPtr(_builders.top().get(), key.c_str(), ""));

			auto cmpTrue = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@ret" + std::to_string(i)).c_str());
			auto cmpFalse = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@test" + std::to_string(i + 1)).c_str());

			LLVMBuildCondBr(_builders.top().get(), cmp, cmpTrue, cmpFalse);

			LLVMPositionBuilderAtEnd(_builders.top().get(), cmpTrue);
			auto val = LLVMConstNull(LLVMGetReturnType(llfuncType));
			val = LLVMBuildInsertValue(_builders.top().get(), val, LLVMConstInt(LLVMInt1TypeInContext(_llcontext.get()), 1, false), 0, ("@ret_present_" + std::to_string(i)).c_str());
			val = LLVMBuildInsertValue(_builders.top().get(), val, var, 1, ("@ret_insert_" + std::to_string(i)).c_str());
			LLVMBuildRet(_builders.top().get(), val);

			LLVMPositionBuilderAtEnd(_builders.top().get(), cmpFalse);
		}

		LLVMSetValueName(LLVMBasicBlockAsValue(LLVMGetInsertBlock(_builders.top().get())), "@not_found");
		LLVMBuildRet(_builders.top().get(), LLVMConstNull(LLVMGetReturnType(llfuncType)));

		_builders.pop();
	}

	{
		auto [llfuncType, llfunc] = co_await declareFunction(info->ns->enumerationReverseLookupFunction);
		LLVMSetLinkage(llfunc, LLVMInternalLinkage);

		auto entry = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "@test0");
		auto builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
		LLVMPositionBuilderAtEnd(builder.get(), entry);
		_builders.push(builder);

		pushStack(ScopeStack::Type::Function);

		std::shared_ptr<AltaCore::DET::Function> cmpFunc = nullptr;

		if (info->memberType->klass) {
			for (const auto& op: info->memberType->klass->operators) {
				if (op->operatorType != AltaCore::Shared::ClassOperatorType::Equality) continue;
				if (!op->parameterVariables[0]->type->isCompatibleWith(*info->memberType->reference())) continue;
				if (!AltaCore::DET::Type(AltaCore::DET::NativeType::Bool).isCompatibleWith(*op->returnType)) continue;

				// this is the operator we want
				cmpFunc = op;
				break;
			}
		}

		if (!info->memberType->klass || cmpFunc) {
			auto param = LLVMGetParam(llfunc, 0);
			auto paramOnStack = cmpFunc ? co_await tmpify(param, info->ns->enumerationReverseLookupFunction->parameterVariables[0]->type, true) : NULL;

			for (size_t i = 0; i < node->members.size(); ++i) {
				const auto& [key, value] = node->members[i];
				const auto& valueInfo = info->memberDetails[key];
				const auto& memberVar = info->memberVariables[key];
				auto var = _definedVariables[memberVar->id];

				LLVMValueRef cmp = NULL;

				if (info->memberType->klass) {
					auto [llcmpFuncType, llcmpFunc] = co_await declareFunction(cmpFunc);

					pushStack(ScopeStack::Type::Temporary);

					std::array<LLVMValueRef, 2> args {
						var,
						co_await cast(paramOnStack, info->ns->enumerationReverseLookupFunction->parameterVariables[0]->type->reference(), cmpFunc->parameterVariables[0]->type, true, std::make_pair(true, false), false, &node->position),
					};

					auto ret = LLVMBuildCall2(_builders.top().get(), llcmpFuncType, llcmpFunc, args.data(), args.size(), ("@cmp" + std::to_string(i)).c_str());
					cmp = co_await cast(ret, cmpFunc->returnType, std::make_shared<AltaCore::DET::Type>(AltaCore::DET::NativeType::Bool), false, std::make_pair(false, false), false, &node->position);

					co_await currentStack().cleanup();
					co_await popStack();
				} else {
					auto lhs = LLVMBuildLoad2(_builders.top().get(), lltype, var, ("@load" + std::to_string(i)).c_str());

					if (info->memberType->isFloatingPoint()) {
						cmp = LLVMBuildFCmp(_builders.top().get(), LLVMRealUEQ, lhs, param, ("@cmp" + std::to_string(i)).c_str());
					} else {
						cmp = LLVMBuildICmp(_builders.top().get(), LLVMIntEQ, lhs, param, ("@cmp" + std::to_string(i)).c_str());
					}
				}

				auto cmpTrue = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@ret" + std::to_string(i)).c_str());
				auto cmpFalse = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@test" + std::to_string(i + 1)).c_str());

				LLVMBuildCondBr(_builders.top().get(), cmp, cmpTrue, cmpFalse);

				LLVMPositionBuilderAtEnd(_builders.top().get(), cmpTrue);
				auto val = LLVMConstNull(LLVMGetReturnType(llfuncType));
				val = LLVMBuildInsertValue(_builders.top().get(), val, LLVMConstInt(LLVMInt1TypeInContext(_llcontext.get()), 1, false), 0, ("@ret_present_" + std::to_string(i)).c_str());
				val = LLVMBuildInsertValue(_builders.top().get(), val, LLVMBuildGlobalStringPtr(_builders.top().get(), key.c_str(), ""), 1, ("@ret_insert_" + std::to_string(i)).c_str());
				LLVMBuildRet(_builders.top().get(), val);

				LLVMPositionBuilderAtEnd(_builders.top().get(), cmpFalse);
			}

			LLVMSetValueName(LLVMBasicBlockAsValue(LLVMGetInsertBlock(_builders.top().get())), "@not_found");
			LLVMBuildRet(_builders.top().get(), LLVMConstNull(LLVMGetReturnType(llfuncType)));
		} else {
			LLVMBuildUnreachable(_builders.top().get());
		}

		co_await currentStack().cleanup();
		co_await popStack();

		_builders.pop();
	}

	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileYieldExpression(std::shared_ptr<AltaCore::AST::YieldExpression> node, std::shared_ptr<AltaCore::DH::YieldExpression> info) {
	pushDebugLocation(node->position, info->inputScope);

	LLVMValueRef expr = NULL;

	if (node->target) {
		bool copy = info->generator->generatorReturnType->referenceLevel() == 0;
		expr = co_await compileNode(node->target, info->target);
		expr = co_await cast(expr, AltaCore::DET::Type::getUnderlyingType(info->target.get()), info->generator->generatorReturnType->makeOptional(), copy, additionalCopyInfo(node->target, info->target), false, &node->position);
	}

	auto llfunc = LLVMGetBasicBlockParent(LLVMGetInsertBlock(_builders.top().get()));

	auto stateIndex = ++_suspendableStateIndex.top();

	co_await updateSuspendableStateIndex(stateIndex);

	auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
	auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());

	// find the root function stack (we'll need it more than once later)
	size_t rootFuncStack = SIZE_MAX;
	for (size_t i = _stacks.size(); i > 0; --i) {
		auto idx = i - 1;
		if (_stacks[idx].type == ScopeStack::Type::Function) {
			rootFuncStack = idx;
			break;
		}
	}

	if (rootFuncStack == SIZE_MAX) {
		throw std::runtime_error("Failed to find root function stack");
	}

	// let's interrupt the expression build to create the stack unwind block for this state in the suspendable function's destructor
	auto dtorUnwindBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), _suspendableDestructor.top().function, ("@state" + std::to_string(stateIndex) + "_unwind").c_str());
	LLVMPositionBuilderAtEnd(_suspendableDestructor.top().builder.get(), dtorUnwindBlock);
	_suspendableDestructor.top().stateUnwindBlocks[stateIndex] = dtorUnwindBlock;
	_builders.push(_suspendableDestructor.top().builder);
	_suspendableContext.push(LLVMGetParam(_suspendableDestructor.top().function, 0));
	for (size_t i = _stacks.size(); i > 0; --i) {
		auto idx = i - 1;

		co_await _stacks[idx].cleanup(true);

		if (idx == rootFuncStack) {
			break;
		}
	}
	LLVMBuildBr(_builders.top().get(), _suspendableDestructor.top().exit);
	_suspendableContext.pop();
	_builders.pop();

	if (expr) {
		LLVMBuildRet(_builders.top().get(), expr);
	} else {
		LLVMBuildRetVoid(_builders.top().get());
	}

	auto continuation = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@state" + std::to_string(stateIndex)).c_str());
	_suspendableStateBlocks.top()[stateIndex] = continuation;
	LLVMPositionBuilderAtEnd(_builders.top().get(), continuation);

	// now we need to reload every stack
	LLVMBuildBr(_builders.top().get(), _stacks[rootFuncStack].suspendableReloadBlock);

	auto theRealContinuation = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@state" + std::to_string(stateIndex) + "@continue").c_str());

	for (size_t i = rootFuncStack; i < _stacks.size(); ++i) {
		auto nextBlock = (i + 1 == _stacks.size()) ? theRealContinuation : _stacks[i + 1].suspendableReloadBlock;
		_stacks[i].suspendableContinuations.emplace_back(stateIndex, nextBlock);
	}

	LLVMPositionBuilderAtEnd(_builders.top().get(), theRealContinuation);

	LLVMValueRef result = NULL;

	if (info->generator->generatorParameterType) {
		std::array<LLVMValueRef, 2> inputGEPIndices {
			LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
			LLVMConstInt(gepStructIndexType, 1, false), // @Suspendable@::suspendable_input
		};
		auto inputGEP = LLVMBuildGEP2(_builders.top().get(), co_await translateType(info->generator->returnType, true, false), currentSuspendableContext(), inputGEPIndices.data(), inputGEPIndices.size(), "@generator_input_ref");

		auto lloptionalParamType = co_await translateType(info->generator->generatorParameterType->makeOptional());

		std::array<LLVMValueRef, 2> vals;
		vals[0] = LLVMConstInt(LLVMInt1TypeInContext(_llcontext.get()), 0, false);
		vals[1] = LLVMGetPoison(co_await translateType(info->generator->generatorParameterType));
		auto nullParam = LLVMConstNamedStruct(lloptionalParamType, vals.data(), vals.size());

		result = LLVMBuildLoad2(_builders.top().get(), lloptionalParamType, inputGEP, "@generator_input");
		LLVMBuildStore(_builders.top().get(), nullParam, inputGEP);
	}

	popDebugLocation();
	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileAssertionStatement(std::shared_ptr<AltaCore::AST::AssertionStatement> node, std::shared_ptr<AltaCore::DH::AssertionStatement> info) {
	auto tmpIdx = nextTemp();
	auto tmpIdxStr = std::to_string(tmpIdx);

	pushStack(ScopeStack::Type::Temporary);

	auto val = co_await compileNode(node->test, info->test);
	auto asBool = co_await cast(val, AltaCore::DET::Type::getUnderlyingType(info->test.get()), std::make_shared<AltaCore::DET::Type>(AltaCore::DET::NativeType::Bool), false, additionalCopyInfo(node->test, info->test), false, &node->position);

	auto llfunc = LLVMGetBasicBlockParent(LLVMGetInsertBlock(_builders.top().get()));

	auto trueBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@assert_true_" + tmpIdxStr).c_str());
	auto falseBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@assert_false_" + tmpIdxStr).c_str());

	LLVMBuildCondBr(_builders.top().get(), asBool, trueBlock, falseBlock);

	LLVMPositionBuilderAtEnd(_builders.top().get(), falseBlock);

	auto abortFuncType = LLVMFunctionType(LLVMVoidTypeInContext(_llcontext.get()), nullptr, 0, false);
	if (!_definedFunctions["abort"]) {
		_definedFunctions["abort"] = LLVMAddFunction(_llmod.get(), "abort", abortFuncType);
	}

	LLVMBuildCall2(_builders.top().get(), abortFuncType, _definedFunctions["abort"], nullptr, 0, "");
	LLVMBuildUnreachable(_builders.top().get());

	LLVMPositionBuilderAtEnd(_builders.top().get(), trueBlock);

	co_await currentStack().cleanup();
	co_await popStack();

	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileAwaitExpression(std::shared_ptr<AltaCore::AST::AwaitExpression> node, std::shared_ptr<AltaCore::DH::AwaitExpression> info) {
	pushDebugLocation(node->position, info->inputScope);

	LLVMValueRef result = NULL;

	auto tmpIdx = nextTemp();
	auto tmpIdxStr = std::to_string(tmpIdx);

	auto instanceVal = co_await compileNode(node->target, info->target);
	auto instanceType = AltaCore::DET::Type::getUnderlyingType(info->target.get());
	auto coClass = instanceType->klass;

	if (instanceType->indirectionLevel() == 0) {
		instanceVal = co_await tmpify(instanceVal, instanceType, true, false);
		instanceType = instanceType->reference();
	}

	while (instanceType->indirectionLevel() > 1) {
		instanceVal = LLVMBuildLoad2(_builders.top().get(), co_await translateType(instanceType->followBlindly()), instanceVal, ("@await_deref_" + tmpIdxStr).c_str());
	}

	if (inSuspendableFunction()) {
		auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
		auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());

		std::array<LLVMValueRef, 3> waitingForGEPIndices {
			LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
			LLVMConstInt(gepStructIndexType, 0, false), // @Suspendable@::basic_context
			LLVMConstInt(gepStructIndexType, 1, false), // _Alta_basic_coroutine::waiting_for
		};
		auto waitingForGEP = LLVMBuildGEP2(_builders.top().get(), co_await translateType(info->coroutine->returnType, true, false), currentSuspendableContext(), waitingForGEPIndices.data(), waitingForGEPIndices.size(), ("@await_waiting_for_gep_" + tmpIdxStr).c_str());

		// store a pointer to the coroutine we need to wait for within the `waiting_for` member
		LLVMBuildStore(_builders.top().get(), instanceVal, waitingForGEP);

		auto stateIndex = ++_suspendableStateIndex.top();

		co_await updateSuspendableStateIndex(stateIndex);

		// find the root function stack (we'll need it more than once later)
		size_t rootFuncStack = SIZE_MAX;
		for (size_t i = _stacks.size(); i > 0; --i) {
			auto idx = i - 1;
			if (_stacks[idx].type == ScopeStack::Type::Function) {
				rootFuncStack = idx;
				break;
			}
		}

		if (rootFuncStack == SIZE_MAX) {
			throw std::runtime_error("Failed to find root function stack");
		}

		// let's interrupt the expression build to create the stack unwind block for this state in the suspendable function's destructor
		auto dtorUnwindBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), _suspendableDestructor.top().function, ("@state" + std::to_string(stateIndex) + "_unwind").c_str());
		LLVMPositionBuilderAtEnd(_suspendableDestructor.top().builder.get(), dtorUnwindBlock);
		_suspendableDestructor.top().stateUnwindBlocks[stateIndex] = dtorUnwindBlock;
		_builders.push(_suspendableDestructor.top().builder);
		_suspendableContext.push(LLVMGetParam(_suspendableDestructor.top().function, 0));
		for (size_t i = _stacks.size(); i > 0; --i) {
			auto idx = i - 1;

			co_await _stacks[idx].cleanup(true);

			if (idx == rootFuncStack) {
				break;
			}
		}
		LLVMBuildBr(_builders.top().get(), _suspendableDestructor.top().exit);
		_suspendableContext.pop();
		_builders.pop();

		LLVMBuildRetVoid(_builders.top().get());

		auto llfunc = LLVMGetBasicBlockParent(LLVMGetInsertBlock(_builders.top().get()));

		auto continuation = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@state" + std::to_string(stateIndex)).c_str());
		_suspendableStateBlocks.top()[stateIndex] = continuation;
		LLVMPositionBuilderAtEnd(_builders.top().get(), continuation);

		// the first thing we need to do is check if the coroutine we were waiting for is done
		auto waitingForGEPAfterReturn = LLVMBuildGEP2(_builders.top().get(), co_await translateType(info->coroutine->returnType, true, false), currentSuspendableContext(), waitingForGEPIndices.data(), waitingForGEPIndices.size(), ("@await_waiting_for_gep_after_return_" + tmpIdxStr).c_str());
		auto instanceAfterReturn = LLVMBuildLoad2(_builders.top().get(), LLVMPointerTypeInContext(_llcontext.get(), 0), waitingForGEPAfterReturn, ("@await_waiting_for_after_return_" + tmpIdxStr).c_str());
		instanceAfterReturn = LLVMBuildPointerCast(_builders.top().get(), instanceAfterReturn, co_await translateType(instanceType), ("@await_instance_" + tmpIdxStr).c_str());

		std::array<LLVMValueRef, 4> doneGEPIndices {
			LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
			LLVMConstInt(gepStructIndexType, 0, false), // @Suspendable@::basic_context
			LLVMConstInt(gepStructIndexType, 0, false), // _Alta_basic_coroutine::basic_state
			LLVMConstInt(gepStructIndexType, 2, false), // _Alta_basic_suspendable::done
		};

		auto doneGEP = LLVMBuildGEP2(_builders.top().get(), co_await defineClassType(coClass), instanceAfterReturn, doneGEPIndices.data(), doneGEPIndices.size(), ("@await_instance_done_ref_" + tmpIdxStr).c_str());
		auto done = LLVMBuildLoad2(_builders.top().get(), LLVMInt1TypeInContext(_llcontext.get()), doneGEP, ("@await_instance_done_" + tmpIdxStr).c_str());

		auto ifDone = _stacks[rootFuncStack].suspendableReloadBlock;
		auto ifNotDone = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@state" + std::to_string(stateIndex) + "@not_done").c_str());

		LLVMBuildCondBr(_builders.top().get(), done, ifDone, ifNotDone);

		// if we're not done, let's just return until we are
		LLVMPositionBuilderAtEnd(_builders.top().get(), ifNotDone);

		// before we do that, however, we have to partially reload every stack until we reach the topmost stack.
		// by "partially", i mean we just need to execute `reload_continue`.
		// this is done so that if the coroutine instance is destroyed before we're done,
		// it sees the valid topmost stack.
		for (size_t i = rootFuncStack; i < _stacks.size(); ++i) {
			buildSuspendableReloadContinue(_builders.top(), currentSuspendableContext());
		}

		LLVMBuildRetVoid(_builders.top().get());

		// if we're done, we first need to reload every stack
		// (we already started this above by branching to the root stack's reload block)

		auto theRealContinuation = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, ("@state" + std::to_string(stateIndex) + "@continue").c_str());

		for (size_t i = rootFuncStack; i < _stacks.size(); ++i) {
			auto nextBlock = (i + 1 == _stacks.size()) ? theRealContinuation : _stacks[i + 1].suspendableReloadBlock;
			_stacks[i].suspendableContinuations.emplace_back(stateIndex, nextBlock);
		}

		LLVMPositionBuilderAtEnd(_builders.top().get(), theRealContinuation);

		bool returnsVal = *coClass->suspendableOutput != AltaCore::DET::Type(AltaCore::DET::NativeType::Void);

		waitingForGEPAfterReturn = LLVMBuildGEP2(_builders.top().get(), co_await translateType(info->coroutine->returnType, true, false), currentSuspendableContext(), waitingForGEPIndices.data(), waitingForGEPIndices.size(), ("@await_waiting_for_gep_after_return_" + tmpIdxStr).c_str());

		if (returnsVal) {
			instanceAfterReturn = LLVMBuildLoad2(_builders.top().get(), LLVMPointerTypeInContext(_llcontext.get(), 0), waitingForGEPAfterReturn, ("@await_waiting_for_after_return_" + tmpIdxStr).c_str());
			instanceAfterReturn = LLVMBuildPointerCast(_builders.top().get(), instanceAfterReturn, co_await translateType(instanceType), ("@await_instance_" + tmpIdxStr).c_str());
		}

		LLVMBuildStore(_builders.top().get(), LLVMConstNull(LLVMPointerTypeInContext(_llcontext.get(), 0)), waitingForGEPAfterReturn);

		if (returnsVal) {
			auto valAccessorFunc = std::dynamic_pointer_cast<AltaCore::DET::Function>(coClass->scope->items[2]);
			auto [accFuncType, accFunc] = co_await declareFunction(valAccessorFunc);

			std::array<LLVMValueRef, 1> accArgs {
				instanceAfterReturn,
			};

			auto wrapped = LLVMBuildCall2(_builders.top().get(), accFuncType, accFunc, accArgs.data(), accArgs.size(), ("@await_fetch_val_" + tmpIdxStr).c_str());
			result = LLVMBuildExtractValue(_builders.top().get(), wrapped, 1, ("@await_opt_unwrap_" + tmpIdxStr).c_str());
		}
	} else {
		throw std::runtime_error("TODO: support `await` outside of coroutines");
	}

	popDebugLocation();
	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileVoidExpression(std::shared_ptr<AltaCore::AST::VoidExpression> node, std::shared_ptr<AltaCore::DH::VoidExpression> info) {
	auto lltype = LLVMStructTypeInContext(_llcontext.get(), NULL, 0, false);
	co_return LLVMConstNamedStruct(lltype, NULL, 0);
};

void AltaLL::Compiler::compile(std::shared_ptr<AltaCore::AST::RootNode> root) {
	_currentFile = root->info->module->path;

	auto filenameStr = _currentFile.basename();
	auto dirnameStr = _currentFile.dirname().toString();
	_debugFiles[_currentFile] = LLVMDIBuilderCreateFile(_debugBuilder.get(), filenameStr.c_str(), filenameStr.size(), dirnameStr.c_str(), dirnameStr.size());
	_compileUnits[_currentFile] = LLVMDIBuilderCreateCompileUnit(_debugBuilder.get(), LLVMDWARFSourceLanguageC_plus_plus, currentDebugFile(), ALTA_COMPILER_NAME, sizeof(ALTA_COMPILER_NAME), /* TODO */ false, "", 0, 0x01000000, "", 0, LLVMDWARFEmissionFull, 0, true, false, "", 0, "", 0);
	_rootDebugScopes[_currentFile] = _debugFiles[_currentFile];

	if (!_definedTypes["_Alta_class_destructor"]) {
		std::array<LLVMTypeRef, 1> dtorParams {
			LLVMPointerType(LLVMVoidTypeInContext(_llcontext.get()), 0),
		};
		_definedTypes["_Alta_class_destructor"] = LLVMFunctionType(LLVMVoidTypeInContext(_llcontext.get()), dtorParams.data(), dtorParams.size(), false);

		auto debugVoidType = LLVMDIBuilderCreateUnspecifiedType(_debugBuilder.get(), "void", 4);

		std::array<LLVMMetadataRef, 2> dtorDebugParams {
			debugVoidType,
			LLVMDIBuilderCreatePointerType(_debugBuilder.get(), debugVoidType, _pointerBits, _pointerBits, 0, "", 0),
		};

		_definedDebugTypes["_Alta_class_destructor"] = LLVMDIBuilderCreateSubroutineType(_debugBuilder.get(), _unknownDebugFile, dtorDebugParams.data(), dtorDebugParams.size(), LLVMDIFlagZero);
	}

	if (!_definedTypes["_Alta_lambda_destructor"]) {
		std::array<LLVMTypeRef, 1> dtorParams {
			LLVMPointerType(LLVMVoidTypeInContext(_llcontext.get()), 0),
		};
		_definedTypes["_Alta_lambda_destructor"] = LLVMFunctionType(LLVMVoidTypeInContext(_llcontext.get()), dtorParams.data(), dtorParams.size(), false);

		auto debugVoidType = LLVMDIBuilderCreateUnspecifiedType(_debugBuilder.get(), "void", 4);

		std::array<LLVMMetadataRef, 2> dtorDebugParams {
			debugVoidType,
			LLVMDIBuilderCreatePointerType(_debugBuilder.get(), debugVoidType, _pointerBits, _pointerBits, 0, "", 0),
		};

		_definedDebugTypes["_Alta_lambda_destructor"] = LLVMDIBuilderCreateSubroutineType(_debugBuilder.get(), _unknownDebugFile, dtorDebugParams.data(), dtorDebugParams.size(), LLVMDIFlagZero);
	}

	if (!_definedTypes["_Alta_virtual_entry"]) {
		/*
		struct _Alta_virtual_entry {
			const char* name;
			void* function;
		};
		*/
		auto stringType = LLVMPointerType(LLVMInt8TypeInContext(_llcontext.get()), 0);
		auto voidPtrType = LLVMPointerTypeInContext(_llcontext.get(), 0);
		std::array<LLVMTypeRef, 2> members {
			stringType,
			voidPtrType,
		};
		auto virtEntryType = _definedTypes["_Alta_virtual_entry"] = LLVMStructTypeInContext(_llcontext.get(), members.data(), members.size(), false);

		auto debugCharType = LLVMDIBuilderCreateBasicType(_debugBuilder.get(), "byte", 4, 8, llvm::dwarf::DW_ATE_unsigned_char, LLVMDIFlagZero);
		auto debugStringType = LLVMDIBuilderCreatePointerType(_debugBuilder.get(), debugCharType, _pointerBits, _pointerBits, 0, "", 0);
		auto debugVoidType = LLVMDIBuilderCreateUnspecifiedType(_debugBuilder.get(), "void", 4);
		auto debugVoidPtrType = LLVMDIBuilderCreatePointerType(_debugBuilder.get(), debugVoidType, _pointerBits, _pointerBits, 0, "", 0);

		std::array<LLVMMetadataRef, 2> debugMembers {
			LLVMDIBuilderCreateMemberType(_debugBuilder.get(), _unknownDebugFile, "name", sizeof("name") - 1, _unknownDebugFile, 0, LLVMDITypeGetSizeInBits(debugStringType), LLVMDITypeGetAlignInBits(debugStringType), LLVMOffsetOfElement(_targetData.get(), virtEntryType, 0) * 8, LLVMDIFlagPublic, debugStringType),
			LLVMDIBuilderCreateMemberType(_debugBuilder.get(), _unknownDebugFile, "function", sizeof("function") - 1, _unknownDebugFile, 0, LLVMDITypeGetSizeInBits(debugVoidPtrType), LLVMDITypeGetAlignInBits(debugVoidPtrType), LLVMOffsetOfElement(_targetData.get(), virtEntryType, 1) * 8, LLVMDIFlagPublic, debugVoidPtrType),
		};

		_definedDebugTypes["_Alta_virtual_entry"] = LLVMDIBuilderCreateStructType(_debugBuilder.get(), _unknownDebugFile, "_Alta_virtual_entry", sizeof("_Alta_virtual_entry") - 1, _unknownDebugFile, 0, LLVMStoreSizeOfType(_targetData.get(), virtEntryType) * 8, LLVMABIAlignmentOfType(_targetData.get(), virtEntryType) * 8, LLVMDIFlagZero, NULL, debugMembers.data(), debugMembers.size(), 0, NULL, "", 0);
	}

	if (!_definedTypes["_Alta_class_info"]) {
		// TODO: optimize virtual lookups by making the structure a trie instead of a table (for faster lookups)
		/*
		struct _Alta_class_info {
			const char* type_name;
			void (*destructor)(void*);
			const char* child_name;
			uint64_t offset_from_real;
			uint64_t offset_from_base;
			uint64_t offset_from_owner;
			uint64_t offset_to_next;
			const struct _Alta_virtual_entry* virtual_table;
		};
		*/
		static const std::array<const char*, 8> memberNames {
			"type_name",
			"destructor",
			"child_name",
			"offset_from_real",
			"offset_from_base",
			"offset_from_owner",
			"offset_to_next",
			"virtual_table",
		};

		std::array<LLVMTypeRef, 8> members;
		auto stringType = LLVMPointerType(LLVMInt8TypeInContext(_llcontext.get()), 0);
		auto i64Type = LLVMInt64TypeInContext(_llcontext.get());
		members[0] = stringType;
		members[1] = LLVMPointerType(_definedTypes["_Alta_class_destructor"], 0);
		members[2] = stringType;
		members[3] = i64Type;
		members[4] = i64Type;
		members[5] = i64Type;
		members[6] = i64Type;
		members[7] = LLVMPointerType(_definedTypes["_Alta_virtual_entry"], 0);
		auto classInfoType = _definedTypes["_Alta_class_info"] = LLVMStructTypeInContext(_llcontext.get(), members.data(), members.size(), false);

		auto debugCharType = LLVMDIBuilderCreateBasicType(_debugBuilder.get(), "byte", 4, 8, llvm::dwarf::DW_ATE_unsigned_char, LLVMDIFlagZero);
		auto debugStringType = LLVMDIBuilderCreatePointerType(_debugBuilder.get(), debugCharType, _pointerBits, _pointerBits, 0, "", 0);
		auto debugI64Type = LLVMDIBuilderCreateBasicType(_debugBuilder.get(), "u64", 3, 64, llvm::dwarf::DW_ATE_unsigned, LLVMDIFlagZero);
		auto debugDtorType = _definedDebugTypes["_Alta_class_destructor"];
		auto debugDtorPtrType = LLVMDIBuilderCreatePointerType(_debugBuilder.get(), debugDtorType, _pointerBits, _pointerBits, 0, "", 0);
		auto debugVirtEntryPtrType = LLVMDIBuilderCreatePointerType(_debugBuilder.get(), _definedDebugTypes["_Alta_virtual_entry"], _pointerBits, _pointerBits, 0, "", 0);

		auto mapType = [&](size_t index) {
			switch (index) {
				case 0:
				case 2:
					return debugStringType;

				case 3:
				case 4:
				case 5:
				case 6:
					return debugI64Type;

				case 1:
					return debugDtorPtrType;

				case 7:
					return debugVirtEntryPtrType;

				default:
					throw std::out_of_range("bad index");
			}
		};

		std::array<LLVMMetadataRef, 7> debugMembers;
		for (size_t i = 0; i < debugMembers.size(); ++i) {
			auto debugType = mapType(i);
			debugMembers[i] = LLVMDIBuilderCreateMemberType(_debugBuilder.get(), _unknownDebugFile, memberNames[i], strlen(memberNames[i]), _unknownDebugFile, 0, LLVMDITypeGetSizeInBits(debugType), LLVMDITypeGetAlignInBits(debugType), LLVMOffsetOfElement(_targetData.get(), classInfoType, i) * 8, LLVMDIFlagPublic, debugType);
		}

		_definedDebugTypes["_Alta_class_info"] = LLVMDIBuilderCreateStructType(_debugBuilder.get(), _unknownDebugFile, "_Alta_class_info", sizeof("_Alta_class_info") - 1, _unknownDebugFile, 0, LLVMStoreSizeOfType(_targetData.get(), classInfoType) * 8, LLVMABIAlignmentOfType(_targetData.get(), classInfoType) * 8, LLVMDIFlagZero, NULL, debugMembers.data(), debugMembers.size(), 0, NULL, "", 0);
	}

	if (!_definedTypes["_Alta_instance_info"]) {
		/*
		struct _Alta_instance_info {
			const struct _Alta_class_info* class_info;
		};
		*/
		std::array<LLVMTypeRef, 1> members;
		members[0] = LLVMPointerType(_definedTypes["_Alta_class_info"], 0);
		auto instanceInfoStruct = _definedTypes["_Alta_instance_info"] = LLVMStructTypeInContext(_llcontext.get(), members.data(), members.size(), false);

		auto debugClassInfo = _definedDebugTypes["_Alta_class_info"];
		auto debugClassInfoPtr = LLVMDIBuilderCreatePointerType(_debugBuilder.get(), debugClassInfo, _pointerBits, _pointerBits, 0, NULL, 0);

		std::array<LLVMMetadataRef, 1> debugElements {
			LLVMDIBuilderCreateMemberType(_debugBuilder.get(), _unknownDebugFile, "class_info", sizeof("class_info") - 1, _unknownDebugFile, 0, LLVMDITypeGetSizeInBits(debugClassInfo), LLVMDITypeGetAlignInBits(debugClassInfo), 0, LLVMDIFlagPublic, debugClassInfoPtr),
		};

		_definedDebugTypes["_Alta_instance_info"] = LLVMDIBuilderCreateStructType(_debugBuilder.get(), _unknownDebugFile, "_Alta_instance_info", sizeof("_Alta_instance_info") - 1, _unknownDebugFile, 0, LLVMStoreSizeOfType(_targetData.get(), instanceInfoStruct) * 8, LLVMABIAlignmentOfType(_targetData.get(), instanceInfoStruct) * 8, LLVMDIFlagZero, NULL, debugElements.data(), debugElements.size(), 0, NULL, "", 0);
	}

	if (!_definedTypes["_Alta_basic_class"]) {
		/*
		struct _Alta_basic_class {
			struct _Alta_instance_info instance_info;
		};
		*/
		std::array<LLVMTypeRef, 1> members;
		members[0] = _definedTypes["_Alta_instance_info"];
		_definedTypes["_Alta_basic_class"] = LLVMStructTypeInContext(_llcontext.get(), members.data(), members.size(), false);
	}

	if (!_definedTypes["_Alta_basic_function"]) {
		/*
		struct _Alta_basic_function {
			void* function_pointer;
			void* lambda_state;
		};
		*/

		// XXX: when i first added lambdas to Alta, they had retain-release semantics on copying/destruction
		//      (i.e. when copying a lambda, you would instead just get the same lambda with an increased reference count).
		//      we still have that and that's what we implement here, but perhaps we should have proper copy semantics for lambdas,
		//      with the option to use refcounting semantics via some attribute on the lambda.

		//auto asPlain = type->copy();
		//asPlain->isRawFunction = true;
		//auto llplain = co_await translateType(asPlain);

		auto voidPointer = LLVMPointerType(LLVMVoidTypeInContext(_llcontext.get()), 0);
		auto debugVoidPointer = LLVMDIBuilderCreatePointerType(_debugBuilder.get(), LLVMDIBuilderCreateUnspecifiedType(_debugBuilder.get(), "void", 4), _pointerBits, _pointerBits, 0, "", 0);

		std::array<LLVMTypeRef, 2> members {
			// function pointer
			voidPointer,

			// lambda state pointer
			voidPointer,
		};

		auto funcType = _definedTypes["_Alta_basic_function"] = LLVMStructTypeInContext(_llcontext.get(), members.data(), members.size(), false);

		std::array<LLVMMetadataRef, 2> debugMembers {
			LLVMDIBuilderCreateMemberType(_debugBuilder.get(), _unknownDebugFile, "function", sizeof("function") - 1, _unknownDebugFile, 0, _pointerBits, _pointerBits, LLVMOffsetOfElement(_targetData.get(), funcType, 0) * 8, LLVMDIFlagPublic, debugVoidPointer),
			LLVMDIBuilderCreateMemberType(_debugBuilder.get(), _unknownDebugFile, "lambda_state", sizeof("lambda_state") - 1, _unknownDebugFile, 0, _pointerBits, _pointerBits, LLVMOffsetOfElement(_targetData.get(), funcType, 1) * 8, LLVMDIFlagPublic, debugVoidPointer),
		};

		_definedDebugTypes["_Alta_basic_function"] = LLVMDIBuilderCreateStructType(_debugBuilder.get(), _unknownDebugFile, "_Alta_basic_function", sizeof("_Alta_basic_function") - 1, _unknownDebugFile, 0, LLVMStoreSizeOfType(_targetData.get(), funcType) * 8, LLVMABIAlignmentOfType(_targetData.get(), funcType) * 8, LLVMDIFlagZero, NULL, debugMembers.data(), debugMembers.size(), 0, NULL, "", 0);
	}

	if (!_definedTypes["_Alta_basic_lambda_state"]) {
		/*
		struct _Alta_basic_lambda_state {
			uint64_t reference_count;
			void (*destructor)(void*);
		};
		*/

		std::array<LLVMTypeRef, 2> members {
			LLVMInt64TypeInContext(_llcontext.get()),
			LLVMPointerType(_definedTypes["_Alta_lambda_destructor"], 0),
		};

		_definedTypes["_Alta_basic_lambda_state"] = LLVMStructTypeInContext(_llcontext.get(), members.data(), members.size(), false);
	}

	if (!_definedTypes["_Alta_basic_suspendable_stack"]) {
		/*
		struct _Alta_basic_suspendable_stack {
			uint64_t size;
			_Alta_basic_suspendable_stack* next;
			_Alta_basic_suspendable_stack* prev;
		};
		*/

		auto voidPointerType = LLVMPointerTypeInContext(_llcontext.get(), 0);
		std::array<LLVMTypeRef, 3> members {
			LLVMInt64TypeInContext(_llcontext.get()),
			voidPointerType,
			voidPointerType,
		};

		auto structType = _definedTypes["_Alta_basic_suspendable_stack"] = LLVMStructTypeInContext(_llcontext.get(), members.data(), members.size(), false);

		members[1] = LLVMPointerType(_definedTypes["_Alta_basic_suspendable_stack"], 0);
		members[2] = members[1];
		LLVMStructSetBody(_definedTypes["_Alta_basic_suspendable_stack"], members.data(), members.size(), false);

		auto debugI64Type = LLVMDIBuilderCreateBasicType(_debugBuilder.get(), "u64", 3, 64, llvm::dwarf::DW_ATE_unsigned, LLVMDIFlagZero);

		auto debugType = LLVMDIBuilderCreateReplaceableCompositeType(_debugBuilder.get(), _compositeCounter++, "_Alta_basic_suspendable_stack", sizeof("_Alta_basic_suspendable_stack") - 1, _unknownDebugFile, _unknownDebugFile, 0, 0, LLVMStoreSizeOfType(_targetData.get(), structType), LLVMABIAlignmentOfType(_targetData.get(), structType), LLVMDIFlagZero, NULL, 0);
		auto debugTypePtr = LLVMDIBuilderCreatePointerType(_debugBuilder.get(), debugType, _pointerBits, _pointerBits, 0, NULL, 0);

		std::array<LLVMMetadataRef, 3> debugMembers {
			LLVMDIBuilderCreateMemberType(_debugBuilder.get(), _unknownDebugFile, "size", sizeof("size") - 1, _unknownDebugFile, 0, LLVMStoreSizeOfType(_targetData.get(), members[0]) * 8, LLVMABIAlignmentOfType(_targetData.get(), members[0]) * 8, LLVMOffsetOfElement(_targetData.get(), structType, 0) * 8, LLVMDIFlagPublic, debugI64Type),
			LLVMDIBuilderCreateMemberType(_debugBuilder.get(), _unknownDebugFile, "next", sizeof("next") - 1, _unknownDebugFile, 0, LLVMStoreSizeOfType(_targetData.get(), members[1]) * 8, LLVMABIAlignmentOfType(_targetData.get(), members[1]) * 8, LLVMOffsetOfElement(_targetData.get(), structType, 1) * 8, LLVMDIFlagPublic, debugTypePtr),
			LLVMDIBuilderCreateMemberType(_debugBuilder.get(), _unknownDebugFile, "prev", sizeof("prev") - 1, _unknownDebugFile, 0, LLVMStoreSizeOfType(_targetData.get(), members[2]) * 8, LLVMABIAlignmentOfType(_targetData.get(), members[2]) * 8, LLVMOffsetOfElement(_targetData.get(), structType, 2) * 8, LLVMDIFlagPublic, debugTypePtr),
		};

		auto permaDebugType = _definedDebugTypes["_Alta_basic_suspendable_stack"] = LLVMDIBuilderCreateStructType(_debugBuilder.get(), _unknownDebugFile, "_Alta_basic_suspendable_stack", sizeof("_Alta_basic_suspendable_stack") - 1, _unknownDebugFile, 0, LLVMStoreSizeOfType(_targetData.get(), structType) * 8, LLVMABIAlignmentOfType(_targetData.get(), structType) * 8, LLVMDIFlagZero, NULL, debugMembers.data(), debugMembers.size(), 0, NULL, "", 0);

		LLVMMetadataReplaceAllUsesWith(debugType, permaDebugType);
	}

	if (!_definedTypes["_Alta_basic_suspendable"]) {
		/*
		struct _Alta_basic_suspendable {
			struct _Alta_instance_info instance_info;
			uint64_t state;
			bool done;
			_Alta_basic_suspendable_stack* current_stack;
		};
		*/

		std::array<LLVMTypeRef, 4> members {
			_definedTypes["_Alta_instance_info"],
			LLVMInt64TypeInContext(_llcontext.get()),
			LLVMInt1TypeInContext(_llcontext.get()),
			LLVMPointerType(_definedTypes["_Alta_basic_suspendable_stack"], 0),
		};

		auto structType = _definedTypes["_Alta_basic_suspendable"] = LLVMStructTypeInContext(_llcontext.get(), members.data(), members.size(), false);

		auto debugI64Type = LLVMDIBuilderCreateBasicType(_debugBuilder.get(), "u64", 3, 64, llvm::dwarf::DW_ATE_unsigned, LLVMDIFlagZero);
		auto debugBoolType = LLVMDIBuilderCreateBasicType(_debugBuilder.get(), "bool", 4, 8, llvm::dwarf::DW_ATE_boolean, LLVMDIFlagZero);
		auto debugStackPtr = LLVMDIBuilderCreatePointerType(_debugBuilder.get(), _definedDebugTypes["_Alta_basic_suspendable_stack"], _pointerBits, _pointerBits, 0, NULL, 0);

		std::array<LLVMMetadataRef, 4> debugMembers {
			LLVMDIBuilderCreateMemberType(_debugBuilder.get(), _unknownDebugFile, "instance_info", sizeof("instance_info") - 1, _unknownDebugFile, 0, LLVMStoreSizeOfType(_targetData.get(), members[0]) * 8, LLVMABIAlignmentOfType(_targetData.get(), members[0]) * 8, LLVMOffsetOfElement(_targetData.get(), structType, 0) * 8, LLVMDIFlagPublic, _definedDebugTypes["_Alta_instance_info"]),
			LLVMDIBuilderCreateMemberType(_debugBuilder.get(), _unknownDebugFile, "state", sizeof("state") - 1, _unknownDebugFile, 0, LLVMStoreSizeOfType(_targetData.get(), members[1]) * 8, LLVMABIAlignmentOfType(_targetData.get(), members[1]) * 8, LLVMOffsetOfElement(_targetData.get(), structType, 1) * 8, LLVMDIFlagPublic, debugI64Type),
			LLVMDIBuilderCreateMemberType(_debugBuilder.get(), _unknownDebugFile, "done", sizeof("done") - 1, _unknownDebugFile, 0, LLVMStoreSizeOfType(_targetData.get(), members[2]) * 8, LLVMABIAlignmentOfType(_targetData.get(), members[2]) * 8, LLVMOffsetOfElement(_targetData.get(), structType, 2) * 8, LLVMDIFlagPublic, debugBoolType),
			LLVMDIBuilderCreateMemberType(_debugBuilder.get(), _unknownDebugFile, "current_stack", sizeof("current_stack") - 1, _unknownDebugFile, 0, LLVMStoreSizeOfType(_targetData.get(), members[3]) * 8, LLVMABIAlignmentOfType(_targetData.get(), members[3]) * 8, LLVMOffsetOfElement(_targetData.get(), structType, 3) * 8, LLVMDIFlagPublic, debugStackPtr),
		};

		_definedDebugTypes["_Alta_basic_suspendable"] = LLVMDIBuilderCreateStructType(_debugBuilder.get(), _unknownDebugFile, "_Alta_basic_suspendable", sizeof("_Alta_basic_suspendable") - 1, _unknownDebugFile, 0, LLVMStoreSizeOfType(_targetData.get(), structType) * 8, LLVMABIAlignmentOfType(_targetData.get(), structType) * 8, LLVMDIFlagZero, NULL, debugMembers.data(), debugMembers.size(), 0, NULL, NULL, 0);
	}

	if (!_definedTypes["_Alta_basic_generator"]) {
		/*
		struct _Alta_basic_generator {
			struct _Alta_basic_suspendable basic_suspendable;
		};
		*/

		std::array<LLVMTypeRef, 1> members {
			_definedTypes["_Alta_basic_suspendable"],
		};

		auto structType = _definedTypes["_Alta_basic_generator"] = LLVMStructTypeInContext(_llcontext.get(), members.data(), members.size(), false);

		std::array<LLVMMetadataRef, 1> debugMembers {
			LLVMDIBuilderCreateMemberType(_debugBuilder.get(), _unknownDebugFile, "basic_suspendable", sizeof("basic_suspendable") - 1, _unknownDebugFile, 0, LLVMStoreSizeOfType(_targetData.get(), members[0]) * 8, LLVMABIAlignmentOfType(_targetData.get(), members[0]) * 8, LLVMOffsetOfElement(_targetData.get(), structType, 0) * 8, LLVMDIFlagPublic, _definedDebugTypes["_Alta_basic_suspendable"]),
		};

		_definedDebugTypes["_Alta_basic_generator"] = LLVMDIBuilderCreateStructType(_debugBuilder.get(), _unknownDebugFile, "_Alta_basic_generator", sizeof("_Alta_basic_generator") - 1, _unknownDebugFile, 0, LLVMStoreSizeOfType(_targetData.get(), structType) * 8, LLVMABIAlignmentOfType(_targetData.get(), structType) * 8, LLVMDIFlagZero, NULL, debugMembers.data(), debugMembers.size(), 0, NULL, NULL, 0);
	}

	if (!_definedTypes["_Alta_basic_coroutine_next"]) {
		std::array<LLVMTypeRef, 1> params {
			LLVMPointerTypeInContext(_llcontext.get(), 0),
		};
		_definedTypes["_Alta_basic_coroutine_next"] = LLVMFunctionType(LLVMVoidTypeInContext(_llcontext.get()), params.data(), params.size(), false);

		auto debugVoidType = LLVMDIBuilderCreateUnspecifiedType(_debugBuilder.get(), "void", 4);

		std::array<LLVMMetadataRef, 2> debugParams {
			debugVoidType,
			LLVMDIBuilderCreatePointerType(_debugBuilder.get(), debugVoidType, _pointerBits, _pointerBits, 0, "", 0),
		};

		_definedDebugTypes["_Alta_basic_coroutine_next"] = LLVMDIBuilderCreateSubroutineType(_debugBuilder.get(), _unknownDebugFile, debugParams.data(), debugParams.size(), LLVMDIFlagZero);
	}

	if (!_definedTypes["_Alta_basic_coroutine"]) {
		/*
		struct _Alta_basic_coroutine {
			struct _Alta_basic_suspendable basic_suspendable;
			struct _Alta_basic_coroutine* waiting_for;
			_Alta_basic_coroutine_next next;
			uint64_t id;
		};
		*/

		auto ptrType = LLVMPointerTypeInContext(_llcontext.get(), 0);
		std::array<LLVMTypeRef, 4> members {
			_definedTypes["_Alta_basic_suspendable"],
			ptrType,
			LLVMPointerType(_definedTypes["_Alta_basic_coroutine_next"], 0),
			LLVMInt64TypeInContext(_llcontext.get()),
		};

		auto structType = _definedTypes["_Alta_basic_coroutine"] = LLVMStructTypeInContext(_llcontext.get(), members.data(), members.size(), false);

		auto fwd = LLVMDIBuilderCreateReplaceableCompositeType(_debugBuilder.get(), _compositeCounter++, "_Alta_basic_coroutine", sizeof("_Alta_basic_coroutine") - 1, _unknownDebugFile, _unknownDebugFile, 0, 0, LLVMStoreSizeOfType(_targetData.get(), structType) * 8, LLVMABIAlignmentOfType(_targetData.get(), structType) * 8, LLVMDIFlagZero, NULL, 0);
		auto fwdPtr = LLVMDIBuilderCreatePointerType(_debugBuilder.get(), fwd, _pointerBits, _pointerBits, 0, NULL, 0);
		auto debugCoroutineNext = LLVMDIBuilderCreatePointerType(_debugBuilder.get(), _definedDebugTypes["_Alta_basic_coroutine_next"], _pointerBits, _pointerBits, 0, NULL, 0);
		auto debugI64Type = LLVMDIBuilderCreateBasicType(_debugBuilder.get(), "u64", 3, 64, llvm::dwarf::DW_ATE_unsigned, LLVMDIFlagZero);

		std::array<LLVMMetadataRef, 4> debugMembers {
			LLVMDIBuilderCreateMemberType(_debugBuilder.get(), _unknownDebugFile, "basic_suspendable", sizeof("basic_suspendable") - 1, _unknownDebugFile, 0, LLVMStoreSizeOfType(_targetData.get(), members[0]) * 8, LLVMABIAlignmentOfType(_targetData.get(), members[0]) * 8, LLVMOffsetOfElement(_targetData.get(), structType, 0) * 8, LLVMDIFlagPublic, _definedDebugTypes["_Alta_basic_suspendable"]),
			LLVMDIBuilderCreateMemberType(_debugBuilder.get(), _unknownDebugFile, "waiting_for", sizeof("waiting_for") - 1, _unknownDebugFile, 0, _pointerBits, _pointerBits, LLVMOffsetOfElement(_targetData.get(), structType, 1) * 8, LLVMDIFlagPublic, fwdPtr),
			LLVMDIBuilderCreateMemberType(_debugBuilder.get(), _unknownDebugFile, "next", sizeof("next") - 1, _unknownDebugFile, 0, _pointerBits, _pointerBits, LLVMOffsetOfElement(_targetData.get(), structType, 2) * 8, LLVMDIFlagPublic, debugCoroutineNext),
			LLVMDIBuilderCreateMemberType(_debugBuilder.get(), _unknownDebugFile, "id", sizeof("id") - 1, _unknownDebugFile, 0, 64, 64, LLVMOffsetOfElement(_targetData.get(), structType, 3) * 8, LLVMDIFlagPublic, debugI64Type),
		};

		auto debugType = _definedDebugTypes["_Alta_basic_coroutine"] = LLVMDIBuilderCreateStructType(_debugBuilder.get(), _unknownDebugFile, "_Alta_basic_coroutine", sizeof("_Alta_basic_coroutine") - 1, _unknownDebugFile, 0, LLVMStoreSizeOfType(_targetData.get(), structType) * 8, LLVMABIAlignmentOfType(_targetData.get(), structType) * 8, LLVMDIFlagZero, NULL, debugMembers.data(), debugMembers.size(), 0, NULL, NULL, 0);

		LLVMMetadataReplaceAllUsesWith(fwd, debugType);
	}

	for (size_t i = 0; i < root->statements.size(); ++i) {
		auto co = compileNode(root->statements[i], root->info->statements[i]);
		co.coroutine.resume();
	}

	_currentFile = {};
};

void AltaLL::Compiler::finalize() {
	if (_initFunction) {
		_builders.push(_initFunctionBuilder);

		auto tmp = _initFunctionScopeStack->cleanup();
		tmp.coroutine.resume();

		_builders.pop();

		LLVMBuildRetVoid(_initFunctionBuilder.get());

		_initFunctionBuilder = nullptr;
		_initFunctionScopeStack = ALTACORE_NULLOPT;
	}

	if (_deinitFunction) {
		_builders.push(_deinitFunctionBuilder);

		auto tmp = _deinitFunctionScopeStack->cleanup();
		tmp.coroutine.resume();

		_builders.pop();

		LLVMBuildRetVoid(_deinitFunctionBuilder.get());

		_deinitFunctionBuilder = nullptr;
		_deinitFunctionScopeStack = ALTACORE_NULLOPT;
	}

	LLVMDIBuilderFinalize(_debugBuilder.get());
};
