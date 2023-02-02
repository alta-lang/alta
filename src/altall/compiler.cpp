#include <altall/compiler.hpp>
#include <altall/mangle.hpp>
#include <altall/altall.hpp>
#include <bit>
#include <unordered_set>

// DEBUGGING/DEVELOPMENT
#include <iostream>

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

AltaLL::Compiler::Coroutine<void> AltaLL::Compiler::ScopeStack::endBranch(LLVMBasicBlockRef mergeBlock, std::vector<LLVMBasicBlockRef> mergingBlocks) {
	auto size = sizesDuringBranching.top();
	sizesDuringBranching.pop();

	for (size_t i = size; i < items.size(); ++i) {
		auto& item = items[i];
		auto lltype = co_await compiler.translateType(item.type->destroyReferences()->reference());
		auto nullVal = LLVMConstNull(lltype);

		auto merged = LLVMBuildPhi(compiler._builders.top().get(), lltype, "");

		for (size_t j = 0; j < mergingBlocks.size(); ++j) {
			if (mergingBlocks[j] == item.sourceBlock) {
				LLVMAddIncoming(merged, &item.source, &mergingBlocks[j], 1);
			} else {
				LLVMAddIncoming(merged, &nullVal, &mergingBlocks[j], 1);
			}
		}

		item.sourceBlock = mergeBlock;
		item.source = merged;
	}

	co_return;
};

AltaLL::Compiler::Coroutine<void> AltaLL::Compiler::ScopeStack::cleanup() {
	// TODO
	co_return;
};

void AltaLL::Compiler::ScopeStack::pushItem(LLVMValueRef memory, std::shared_ptr<AltaCore::DET::Type> type) {
	pushItem(memory, type, LLVMGetInsertBlock(compiler._builders.top().get()));
};

void AltaLL::Compiler::ScopeStack::pushItem(LLVMValueRef memory, std::shared_ptr<AltaCore::DET::Type> type, LLVMBasicBlockRef sourceBlock) {
	items.emplace_back(ScopeItem { sourceBlock, memory, type });
};

AltaLL::Compiler::Coroutine<LLVMTypeRef> AltaLL::Compiler::translateType(std::shared_ptr<AltaCore::DET::Type> type, bool usePointersToFunctions) {
	auto mangled = "_non_native_" + mangleType(type);

	if (usePointersToFunctions && type->isFunction && type->isRawFunction) {
		mangled = "_ptr_" + mangled;
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
			case AltaCore::DET::NativeType::Void:        result = LLVMVoidTypeInContext(_llcontext.get()); break;
			case AltaCore::DET::NativeType::Double:      result = LLVMDoubleTypeInContext(_llcontext.get()); break;
			case AltaCore::DET::NativeType::Float:       result = LLVMFloatTypeInContext(_llcontext.get()); break;
			case AltaCore::DET::NativeType::UserDefined: result = NULL; break;
		}
	} else if (type->isFunction) {
		if (type->isRawFunction) {
			auto retType = co_await translateType(type->returnType);
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
		size_t alignment = 0;

		members[0] = LLVMIntTypeInContext(_llcontext.get(), std::bit_width(type->unionOf.size() - 1));

		for (const auto& component: type->unionOf) {
			auto lltype = co_await translateType(component);
			auto llsize = LLVMStoreSizeOfType(_targetData.get(), lltype);
			auto llalign = LLVMPreferredAlignmentOfType(_targetData.get(), lltype);
			if (llsize > unionSize) {
				unionSize = llsize;
			}
			if (llalign > alignment) {
				alignment = llalign;
			}
		}

		// FIXME: we need to take into account the right alignment for members
		members[1] = LLVMArrayType(LLVMInt8TypeInContext(_llcontext.get()), unionSize);

		result = LLVMStructTypeInContext(_llcontext.get(), members.data(), members.size(), false);
	} else if (type->isOptional) {
		std::array<LLVMTypeRef, 2> members;

		members[0] = LLVMInt1TypeInContext(_llcontext.get());
		members[1] = co_await translateType(type->optionalTarget);

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
			expr = LLVMBuildPtrToInt(_builders.top().get(), expr, llintType, "");
		}
		if (exprType->pointerLevel() > 0 || !(*exprType == DET::Type(DET::NativeType::Bool))) {
			expr = LLVMBuildICmp(_builders.top().get(), LLVMIntNE, expr, LLVMConstInt(llintType, 0, false), "");
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
				result = LLVMBuildExtractValue(_builders.top().get(), result, 0, "");
				currentType = std::make_shared<DET::Type>(DET::NativeType::Bool);
				copy = false;
			} else if (component.special == SCT::EmptyOptional) {
				std::array<LLVMValueRef, 2> vals;
				vals[0] = LLVMConstInt(LLVMInt1TypeInContext(_llcontext.get()), 0, false);
				vals[1] = LLVMGetUndef(co_await translateType(dest->optionalTarget));
				result = LLVMConstNamedStruct(co_await translateType(dest), vals.data(), vals.size());
				currentType = dest;
				copy = false;
			} else if (component.special == SCT::WrapFunction) {
				throw std::runtime_error("TODO: support function wrapping");

				auto wrappedCopy = exprType->copy();
				wrappedCopy->isRawFunction = false;
				currentType = dest;
				copy = false;
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
				result = LLVMBuildPtrToInt(_builders.top().get(), result, lltarget, "");
			} else if (currentIsIntegral && targetIsPointer) {
				result = LLVMBuildIntToPtr(_builders.top().get(), result, lltarget, "");
			} else if (currentIsPointer && targetIsPointer) {
				result = LLVMBuildBitCast(_builders.top().get(), result, lltarget, "");
			} else if (currentIsFP && targetIsFP) {
				result = LLVMBuildFPCast(_builders.top().get(), result, lltarget, "");
			} else if (currentIsFP) {
				result = (component.target->isSigned() ? LLVMBuildFPToSI : LLVMBuildFPToUI)(_builders.top().get(), result, lltarget, "");
			} else if (targetIsFP) {
				result = (currentType->isSigned() ? LLVMBuildSIToFP : LLVMBuildUIToFP)(_builders.top().get(), result, lltarget, "");
			} else {
				result = LLVMBuildIntCast2(_builders.top().get(), result, lltarget, component.target->isSigned(), "");
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

			result = co_await doChildRetrieval(result, currentType, nextType, &didRetrieval);
			if (!didRetrieval) {
				throw std::runtime_error("supposed to be able to do child retrieval");
			}

			currentType = nextType;
			additionalCopyInfo = std::make_pair(false, false);
		} else if (component.type == CCT::Reference) {
			if (additionalCopyInfo.second) {
				result = co_await tmpify(result, currentType, false);
			}

			currentType = currentType->reference();
			copy = false;
			additionalCopyInfo = std::make_pair(false, true);
		} else if (component.type == CCT::Dereference) {
			result = LLVMBuildLoad2(_builders.top().get(), co_await translateType(currentType->dereference()), result, "");
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
			auto tmp = LLVMBuildInsertValue(_builders.top().get(), LLVMGetUndef(lltype), LLVMConstInt(LLVMInt1TypeInContext(_llcontext.get()), 1, false), 0, "");
			result = LLVMBuildInsertValue(_builders.top().get(), tmp, result, 1, "");

			copy = false;
			additionalCopyInfo = std::make_pair(false, true);
		} else if (component.type == CCT::Unwrap) {
			result = co_await loadRef(result, currentType);
			result = LLVMBuildExtractValue(_builders.top().get(), result, 1, "");
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
			auto allocated = LLVMBuildAlloca(_builders.top().get(), lltarget, "");

			std::array<LLVMTypeRef, 2> members;
			members[0] = LLVMIntTypeInContext(_llcontext.get(), std::bit_width(component.target->unionOf.size() - 1));
			members[1] = co_await translateType(component.via);
			auto llactual = LLVMStructType(members.data(), members.size(), false);

			auto casted = LLVMBuildBitCast(_builders.top().get(), allocated, LLVMPointerType(llactual, 0), "");

			auto tmp = LLVMBuildInsertValue(_builders.top().get(), LLVMGetUndef(llactual), LLVMConstInt(members[0], memberIndex, false), 0, "");
			result = LLVMBuildInsertValue(_builders.top().get(), tmp, result, 1, "");

			LLVMBuildStore(_builders.top().get(), result, casted);

			result = LLVMBuildLoad2(_builders.top().get(), lltarget, allocated, "");

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
			auto allocated = LLVMBuildAlloca(_builders.top().get(), llcurrent, "");

			LLVMBuildStore(_builders.top().get(), result, allocated);

			std::array<LLVMTypeRef, 2> members;
			members[0] = LLVMIntTypeInContext(_llcontext.get(), std::bit_width(currentType->unionOf.size() - 1));
			members[1] = co_await translateType(component.target);
			auto llactual = LLVMStructType(members.data(), members.size(), false);

			auto casted = LLVMBuildBitCast(_builders.top().get(), allocated, LLVMPointerType(llactual, 0), "");

			auto tmp = LLVMBuildLoad2(_builders.top().get(), llactual, casted, "");
			result = LLVMBuildExtractValue(_builders.top().get(), tmp, 1, "");

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
			result = LLVMBuildCall2(_builders.top().get(), lltype, llfunc, args.data(), args.size(), "");

			copy = false;
			additionalCopyInfo = std::make_pair(false, true);
			currentType = std::make_shared<DET::Type>(component.method->parentScope.lock()->parentClass.lock());
		} else if (component.type == CCT::To) {
			auto to = component.method;
			auto classType = std::make_shared<DET::Type>(component.method->parentScope.lock()->parentClass.lock());

			if (currentType->referenceLevel() == 0) {
				result = co_await tmpify(result, currentType, false);
			}

			auto [lltype, llfunc] = co_await declareFunction(component.method);

			std::array<LLVMValueRef, 1> args { result };
			result = LLVMBuildCall2(_builders.top().get(), lltype, llfunc, args.data(), args.size(), "");

			currentType = component.method->returnType;
			copy = false;
			additionalCopyInfo = std::make_pair(false, true);
		} else if (component.type == CCT::Multicast) {
			bool didCopy = false;

			if (copy && additionalCopyInfo.first && canCopy()) {
				result = co_await doCopyCtor(result, currentType, additionalCopyInfo, &didCopy);
				copy = false;
			}

			auto strType = LLVMPointerType(LLVMInt8TypeInContext(_llcontext.get()), 0);
			std::array<LLVMTypeRef, 2> params {
				strType,
				strType,
			};
			auto funcType_Alta_bad_cast = LLVMFunctionType(LLVMVoidTypeInContext(_llcontext.get()), params.data(), params.size(), false);

			if (!_definedFunctions["_Alta_bad_cast"]) {
				_definedFunctions["_Alta_bad_cast"] = LLVMAddFunction(_llmod.get(), "_Alta_bad_cast", funcType_Alta_bad_cast);
			}

			auto func_Alta_bad_cast = _definedFunctions["_Alta_bad_cast"];
			auto parentFunc = LLVMGetBasicBlockParent(LLVMGetInsertBlock(_builders.top().get()));
			std::vector<LLVMBasicBlockRef> phiBlocks;
			std::vector<LLVMValueRef> phiVals;

			currentStack().beginBranch();

			// first, add the default (bad cast) block
			auto badCastBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), parentFunc, "");

			// now start creating the switch
			if (!result) {
				throw std::runtime_error("This should be impossible (cast:multicast)");
			}
			auto val = LLVMBuildExtractValue(_builders.top().get(), co_await loadRef(result, currentType), 0, "");
			auto tmpified = co_await tmpify(result, currentType, false);
			auto switchInstr = LLVMBuildSwitch(_builders.top().get(), val, badCastBlock, currentType->unionOf.size());

			// now move the builder to the bad cast block and build it
			LLVMPositionBuilderAtEnd(_builders.top().get(), badCastBlock);
			auto currStr = currentType->toString();
			auto destStr = component.target->toString();
			std::array<LLVMValueRef, 2> args {
				LLVMBuildGlobalStringPtr(_builders.top().get(), currStr.c_str(), ""),
				LLVMBuildGlobalStringPtr(_builders.top().get(), destStr.c_str(), ""),
			};
			LLVMBuildCall2(_builders.top().get(), funcType_Alta_bad_cast, func_Alta_bad_cast, args.data(), args.size(), "");
			LLVMBuildUnreachable(_builders.top().get());

			// now create the block we go to after we're done here
			auto doneBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), parentFunc, "");

			auto idxType = LLVMIntTypeInContext(_llcontext.get(), std::bit_width(currentType->unionOf.size() - 1));

			for (size_t i = 0; i < currentType->unionOf.size(); ++i) {
				if (component.multicasts[i].second.size() == 0) {
					LLVMAddCase(switchInstr, LLVMConstInt(idxType, i, false), badCastBlock);
				} else {
					auto& unionMember = currentType->unionOf[i];
					auto thisBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), parentFunc, "");
					LLVMAddCase(switchInstr, LLVMConstInt(idxType, i, false), thisBlock);
					LLVMPositionBuilderAtEnd(_builders.top().get(), thisBlock);

					std::array<LLVMTypeRef, 2> members;
					members[0] = idxType;
					members[1] = co_await translateType(unionMember);
					auto llactual = LLVMStructType(members.data(), members.size(), false);

					auto castedUnion = LLVMBuildBitCast(_builders.top().get(), tmpified, LLVMPointerType(llactual, 0), "");
					auto loadedUnion = LLVMBuildLoad2(_builders.top().get(), llactual, castedUnion, "");
					auto member = LLVMBuildExtractValue(_builders.top().get(), loadedUnion, 1, "");

					auto casted = co_await cast(member, unionMember, component.target, false, std::make_pair(false, true), false, position);
					phiBlocks.push_back(LLVMGetInsertBlock(_builders.top().get()));
					phiVals.push_back(casted);

					LLVMBuildBr(_builders.top().get(), doneBlock);
				}
			}

			// now finish building the "done" block
			LLVMPositionBuilderAtEnd(_builders.top().get(), doneBlock);

			currentStack().endBranch(doneBlock, phiBlocks);

			result = LLVMBuildPhi(_builders.top().get(), co_await translateType(component.target), "");
			LLVMAddIncoming(result, phiVals.data(), phiBlocks.data(), phiVals.size());

			currentType = component.target;
			copy = false;
			additionalCopyInfo = std::make_pair(false, true);
		}
	}

	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::doCopyCtorInternal(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType, CopyInfo additionalCopyInfo, bool* didCopy) {
	auto result = expr;

	if (exprType->isNative) {
		if (exprType->isFunction && !exprType->isRawFunction) {
			// for now, lambdas always have retain-release semantics, so we just have to increment the reference count within the lambda state (if we have a lambda).

			result = co_await loadRef(result, exprType);

			auto lambdaState = LLVMBuildExtractValue(_builders.top().get(), result, 1, "");
			auto lambdaStateIsNotNull = LLVMBuildIsNotNull(_builders.top().get(), lambdaState, "");

			auto llfunc = LLVMGetBasicBlockParent(LLVMGetInsertBlock(_builders.top().get()));
			auto doIncRefBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "");
			auto doneBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "");

			LLVMBuildCondBr(_builders.top().get(), lambdaStateIsNotNull, doIncRefBlock, doneBlock);

			LLVMPositionBuilderAtEnd(_builders.top().get(), doIncRefBlock);

			auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
			auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());
			auto refCountType = LLVMInt64TypeInContext(_llcontext.get());

			std::array<LLVMValueRef, 2> gepIndices {
				LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
				LLVMConstInt(gepStructIndexType, 0, false), // _Alta_basic_lambda_state::reference_count
			};

			auto refCountPtr = LLVMBuildGEP2(_builders.top().get(), _definedTypes["_Alta_basic_lambda_state"], lambdaState, gepIndices.data(), gepIndices.size(), "");
			LLVMBuildAtomicRMW(_builders.top().get(), LLVMAtomicRMWBinOpAdd, refCountPtr, LLVMConstInt(refCountType, 1, false), LLVMAtomicOrderingRelease, false);
			LLVMBuildBr(_builders.top().get(), doneBlock);

			LLVMPositionBuilderAtEnd(_builders.top().get(), doneBlock);
		}
	} else {
		if (additionalCopyInfo.second && exprType->indirectionLevel() < 1) {
			result = co_await tmpify(result, exprType);
			exprType = exprType->reference();
		}

		LLVMValueRef copyFunc = NULL;
		LLVMTypeRef copyFuncType = NULL;

		if (exprType->isUnion()) {
			auto llunionType = co_await translateType(exprType);
			std::array<LLVMTypeRef, 1> params { LLVMPointerType(llunionType, 0) };
			copyFuncType = LLVMFunctionType(llunionType, params.data(), params.size(), false);

			if (!_definedFunctions["_Alta_copy_" + mangleType(exprType)]) {
				auto name = "_Alta_copy_" + mangleType(exprType);
				auto func = LLVMAddFunction(_llmod.get(), name.c_str(), copyFuncType);
				_definedFunctions["_Alta_copy_" + mangleType(exprType)] = func;
			}

			copyFunc = _definedFunctions["_Alta_copy_" + mangleType(exprType)];
		} else if (exprType->isOptional) {
			auto lloptionalType = co_await translateType(exprType);
			std::array<LLVMTypeRef, 1> params { LLVMPointerType(lloptionalType, 0) };
			copyFuncType = LLVMFunctionType(lloptionalType, params.data(), params.size(), false);

			if (!_definedFunctions["_Alta_copy_" + mangleType(exprType)]) {
				auto name = "_Alta_copy_" + mangleType(exprType);
				auto func = LLVMAddFunction(_llmod.get(), name.c_str(), copyFuncType);
				_definedFunctions["_Alta_copy_" + mangleType(exprType)] = func;
			}

			copyFunc = _definedFunctions["_Alta_copy_" + mangleType(exprType)];
		} else if (
			// make sure we have a copy constructor,
			// otherwise, there's no point in continuing
			exprType->klass->copyConstructor
		) {
			auto [lltype, llfunc] = co_await declareFunction(exprType->klass->copyConstructor);
			copyFuncType = lltype;
			copyFunc = llfunc;
		}

		if (copyFunc && copyFuncType) {
			std::array<LLVMValueRef, 1> args { result };
			result = LLVMBuildCall2(_builders.top().get(), copyFuncType, copyFunc, args.data(), args.size(), "");

			if (didCopy) {
				*didCopy = true;
			}
		}
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
		nodeType != AltaCore::AST::NodeType::FunctionCallExpression
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

		if (!exprType->isRawFunction) {
			throw std::runtime_error("TODO: support non-raw function pointers");
		} else if (exprType->isUnion()) {
			throw std::runtime_error("TODO: support unions");
		} else if (exprType->isOptional) {
			auto loaded = co_await loadRef(result, exprType);
			LLVMValueRef contained = NULL;
			auto containedType = exprType->optionalTarget;

			auto present = LLVMBuildExtractValue(_builders.top().get(), loaded, 0, "");

			if (exprType->referenceLevel() > 0) {
				std::array<LLVMValueRef, 2> gepIndices {
					LLVMConstInt(gepIndexType, 0, false), // the first entry in the "array"
					LLVMConstInt(gepStructIndexType, 1, false), // the contained value
				};

				contained = LLVMBuildGEP2(_builders.top().get(), co_await translateType(exprType->destroyReferences()), result, gepIndices.data(), gepIndices.size(), "");
				containedType = containedType->reference();
			} else {
				contained = LLVMBuildExtractValue(_builders.top().get(), result, 1, "");
			}

			auto llfunc = LLVMGetBasicBlockParent(LLVMGetInsertBlock(_builders.top().get()));
			auto doDtorBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "");
			auto doneBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "");

			currentStack().beginBranch();

			auto originBlock = LLVMGetInsertBlock(_builders.top().get());
			LLVMBuildCondBr(_builders.top().get(), present, doDtorBlock, doneBlock);

			LLVMPositionBuilderAtEnd(_builders.top().get(), doDtorBlock);
			co_await doDtor(contained, containedType, nullptr, false);
			auto exitDtorBlock = LLVMGetInsertBlock(_builders.top().get());
			LLVMBuildBr(_builders.top().get(), doneBlock);

			LLVMPositionBuilderAtEnd(_builders.top().get(), doneBlock);
			currentStack().endBranch(doneBlock, {
				originBlock,
				exitDtorBlock,
			});

			result = NULL;
		} else {
			// TODO: check if the class info pointer is null; if it is, skip destruction.

			result = co_await getRootInstance(result, exprType);
			result = LLVMBuildPointerCast(_builders.top().get(), result, LLVMPointerType(_definedTypes["_Alta_basic_class"], 0), "");

			std::array<LLVMValueRef, 3> gepIndices {
				LLVMConstInt(gepIndexType, 0, false), // the first entry in the "array"
				LLVMConstInt(gepStructIndexType, 0, false), // _Alta_basic_class::instance_info
				LLVMConstInt(gepStructIndexType, 0, false), // _Alta_instance_info::class_info
			};

			auto classInfoPtr = LLVMPointerType(_definedTypes["_Alta_class_info"], 0);
			auto classInfoPtrPtr = LLVMPointerType(classInfoPtr, 0);

			auto dtor = LLVMBuildGEP2(_builders.top().get(), _definedTypes["_Alta_basic_class"], result, gepIndices.data(), gepIndices.size(), "");
			dtor = LLVMBuildLoad2(_builders.top().get(), classInfoPtr, dtor, "");

			std::array<LLVMValueRef, 2> gepIndices2 {
				LLVMConstInt(gepIndexType, 0, false), // the first entry in the "array"
				LLVMConstInt(gepStructIndexType, 1, false), // _Alta_class_info::destructor
			};

			auto dtorType = _definedTypes["_Alta_class_destructor"];
			auto dtorPtr = LLVMPointerType(dtorType, 0);
			auto dtorPtrPtr = LLVMPointerType(dtorPtr, 0);

			dtor = LLVMBuildGEP2(_builders.top().get(), _definedTypes["_Alta_class_info"], dtor, gepIndices2.data(), gepIndices2.size(), "");
			dtor = LLVMBuildLoad2(_builders.top().get(), dtorPtr, dtor, "");

			std::array<LLVMValueRef, 1> args {
				LLVMBuildPointerCast(_builders.top().get(), result, LLVMPointerType(LLVMVoidTypeInContext(_llcontext.get()), 0), ""),
			};
			result = LLVMBuildCall2(_builders.top().get(), dtorType, dtor, args.data(), args.size(), "");
		}

		if (didDtor) {
			*didDtor = true;
		}
	}

	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::loadRef(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType) {
	for (size_t i = 0; i < exprType->referenceLevel(); ++i) {
		exprType = exprType->dereference();
		expr = LLVMBuildLoad2(_builders.top().get(), co_await translateType(exprType), expr, "");
	}
	co_return expr;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::getRealInstance(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType) {
	auto result = expr;

	if (
		!exprType->isNative &&
		!exprType->isUnion() &&
		exprType->klass
	) {
		auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
		auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());
		auto lltype = co_await translateType(exprType);
		auto lltypeRef = co_await translateType(exprType->destroyReferences()->reference());

		if (exprType->indirectionLevel() == 0) {
			result = co_await tmpify(result, exprType, false);
		}

		std::array<LLVMValueRef, 3> accessGEP {
			LLVMConstInt(gepIndexType, 0, false), // first element in "array"
			LLVMConstInt(gepStructIndexType, 0, false), // instance_info member within class structure
			LLVMConstInt(gepStructIndexType, 0, false), // class_info pointer within instance info
		};
		auto classInfoPtrType = LLVMPointerType(_definedTypes["_Alta_class_info"], 0);
		auto tmp = LLVMBuildGEP2(_builders.top().get(), co_await defineClassType(exprType->klass), result, accessGEP.data(), accessGEP.size(), "");
		auto classInfoPointer = LLVMBuildLoad2(_builders.top().get(), classInfoPtrType, tmp, "");

		std::array<LLVMValueRef, 2> offsetGEP {
			LLVMConstInt(gepIndexType, 0, false), // first element in "array"
			LLVMConstInt(gepStructIndexType, 3, false), // offset_from_real member within class_info
		};
		auto i64Type = LLVMInt64TypeInContext(_llcontext.get());
		auto offsetFromRealPointer = LLVMBuildGEP2(_builders.top().get(), _definedTypes["_Alta_class_info"], classInfoPointer, offsetGEP.data(), offsetGEP.size(), "");
		auto offsetFromReal = LLVMBuildLoad2(_builders.top().get(), i64Type, offsetFromRealPointer, "");

		auto classPointerAsInt = LLVMBuildPtrToInt(_builders.top().get(), result, i64Type, "");
		auto subtracted = LLVMBuildSub(_builders.top().get(), classPointerAsInt, offsetFromReal, "");
		result = LLVMBuildIntToPtr(_builders.top().get(), subtracted, lltypeRef, "");

		if (exprType->indirectionLevel() == 0) {
			result = LLVMBuildLoad2(_builders.top().get(), lltype, result, "");
		}
	}

	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::getRootInstance(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType) {
	auto result = expr;

	if (
		!exprType->isNative &&
		!exprType->isUnion() &&
		exprType->klass
	) {
		auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
		auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());

		if (exprType->indirectionLevel() == 0) {
			result = co_await tmpify(result, exprType, false);
		}

		std::array<LLVMValueRef, 3> accessGEP {
			LLVMConstInt(gepIndexType, 0, false), // first element in "array"
			LLVMConstInt(gepStructIndexType, 0, false), // instance_info member within class structure
			LLVMConstInt(gepStructIndexType, 0, false), // class_info pointer within instance info
		};
		auto classInfoPtrType = LLVMPointerType(_definedTypes["_Alta_class_info"], 0);
		auto tmp = LLVMBuildGEP2(_builders.top().get(), co_await defineClassType(exprType->klass), result, accessGEP.data(), accessGEP.size(), "");
		auto classInfoPointer = LLVMBuildLoad2(_builders.top().get(), classInfoPtrType, tmp, "");

		std::array<LLVMValueRef, 2> offsetGEP {
			LLVMConstInt(gepIndexType, 0, false), // first element in "array"
			LLVMConstInt(gepStructIndexType, 4, false), // offset_from_base member within class_info
		};
		auto i64Type = LLVMInt64TypeInContext(_llcontext.get());
		auto offsetFromBasePointer = LLVMBuildGEP2(_builders.top().get(), _definedTypes["_Alta_class_info"], classInfoPointer, offsetGEP.data(), offsetGEP.size(), "");
		auto offsetFromBase = LLVMBuildLoad2(_builders.top().get(), i64Type, offsetFromBasePointer, "");

		auto classPointerAsInt = LLVMBuildPtrToInt(_builders.top().get(), result, i64Type, "");
		auto subtracted = LLVMBuildSub(_builders.top().get(), classPointerAsInt, offsetFromBase, "");
		result = LLVMBuildIntToPtr(_builders.top().get(), subtracted, LLVMPointerType(LLVMVoidTypeInContext(_llcontext.get()), 0), "");
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
				gepIndices.push_back(LLVMConstInt(gepStructIndexType, i, false));
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
			result = co_await tmpify(result, exprType, false);
		}

		auto refTargetType = targetType->indirectionLevel() > 0 ? targetType : targetType->reference();
		auto lltype = co_await translateType(refTargetType);
		result = LLVMBuildGEP2(_builders.top().get(), co_await defineClassType(exprType->klass), result, gepIndices.data(), gepIndices.size(), "");
		result = co_await getRealInstance(result, refTargetType);

		if (targetType->indirectionLevel() == 0) {
			result = LLVMBuildLoad2(_builders.top().get(), co_await translateType(targetType), result, "");
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
		targetType->klass->hasParent(exprType->klass)
	) {
		std::vector<std::shared_ptr<AltaCore::DET::Class>> parentAccessors;
		std::stack<size_t> idxs;

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

		auto voidPointerType = LLVMPointerType(LLVMVoidTypeInContext(_llcontext.get()), 0);
		auto i64Type = LLVMInt64TypeInContext(_llcontext.get());
		std::array<LLVMTypeRef, 2> funcParams_Alta_get_child { voidPointerType, i64Type };
		auto funcType_Alta_get_child = LLVMFunctionType(voidPointerType, funcParams_Alta_get_child.data(), funcParams_Alta_get_child.size(), true);

		if (!_definedFunctions["_Alta_get_child"]) {
			_definedFunctions["_Alta_get_child"] = LLVMAddFunction(_llmod.get(), "_Alta_get_child", funcType_Alta_get_child);
		}

		auto func_Alta_get_child = _definedFunctions["_Alta_get_child"];

		std::vector<LLVMValueRef> args { LLVMBuildPointerCast(_builders.top().get(), result, voidPointerType, ""), LLVMConstInt(i64Type, parentAccessors.size() - 1, false) };

		for (size_t i = parentAccessors.size() - 1; i > 0; i--) {
			auto mangled = mangleName(parentAccessors[i - 1]);
			args.push_back(LLVMConstStringInContext(_llcontext.get(), mangled.c_str(), mangled.size(), false));
		}

		result = LLVMBuildCall2(_builders.top().get(), funcType_Alta_get_child, func_Alta_get_child, args.data(), args.size(), "");

		if (didRetrieval != nullptr) {
			*didRetrieval = true;
		}
	}

	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::tmpify(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> type, bool withStack) {
	if (_inGenerator) {
		throw std::runtime_error("TODO: tmpify in generators");
	}

	auto result = expr;

	if (type->referenceLevel() == 0) {
		auto adjustedType = type->deconstify();
		auto lltype = co_await translateType(adjustedType);

		auto var = LLVMBuildAlloca(_builders.top().get(), lltype, "");

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
			expr->nodeType() == AltaCore::AST::NodeType::ConditionalExpression
		)
	) {
		result = co_await tmpify(result, type, true);
	}

	co_return result;
};

AltaLL::Compiler::Coroutine<void> AltaLL::Compiler::processArgs(std::vector<ALTACORE_VARIANT<std::pair<std::shared_ptr<AltaCore::AST::ExpressionNode>, std::shared_ptr<AltaCore::DH::ExpressionNode>>, std::vector<std::pair<std::shared_ptr<AltaCore::AST::ExpressionNode>, std::shared_ptr<AltaCore::DH::ExpressionNode>>>>> adjustedArguments, std::vector<std::tuple<std::string, std::shared_ptr<AltaCore::DET::Type>, bool, std::string>> parameters, AltaCore::Errors::Position* position, std::vector<LLVMValueRef>& outputArgs) {
	for (size_t i = 0; i < adjustedArguments.size(); ++i) {
		auto& arg = adjustedArguments[i];
		auto [name, targetType, isVariable, id] = parameters[i];

		if (auto solo = ALTACORE_VARIANT_GET_IF<std::pair<std::shared_ptr<AltaCore::AST::ExpressionNode>, std::shared_ptr<AltaCore::DH::ExpressionNode>>>(&arg)) {
			auto& [arg, info] = *solo;
			auto compiled = co_await compileNode(arg, info);
			auto exprType = AltaCore::DET::Type::getUnderlyingType(info.get());
			auto result = co_await cast(compiled, exprType, targetType, true, additionalCopyInfo(arg, info), false, position);
			outputArgs.push_back(result);
		} else if (auto multi = ALTACORE_VARIANT_GET_IF<std::vector<std::pair<std::shared_ptr<AltaCore::AST::ExpressionNode>, std::shared_ptr<AltaCore::DH::ExpressionNode>>>>(&arg)) {
			if (varargTable[id]) {
				for (auto& [arg, info]: *multi) {
					auto compiled = co_await compileNode(arg, info);
					auto exprType = AltaCore::DET::Type::getUnderlyingType(info.get());
					auto result = co_await cast(compiled, exprType, targetType, true, additionalCopyInfo(arg, info), false, position);
					outputArgs.push_back(result);
				}
			} else {
				std::vector<LLVMValueRef> arrayItems;

				for (auto& [arg, info]: *multi) {
					auto compiled = co_await compileNode(arg, info);
					auto exprType = AltaCore::DET::Type::getUnderlyingType(info.get());
					auto result = co_await cast(compiled, exprType, targetType, true, additionalCopyInfo(arg, info), false, position);
					arrayItems.push_back(result);
				}

				auto i64Type = LLVMInt64TypeInContext(_llcontext.get());

				auto lltype = co_await translateType(targetType);
				auto llcount = LLVMConstInt(i64Type, arrayItems.size(), false);
				auto result = LLVMBuildArrayAlloca(_builders.top().get(), lltype, llcount, "");

				for (size_t i = 0; i < arrayItems.size(); ++i) {
					std::array<LLVMValueRef, 1> gepIndices {
						LLVMConstInt(i64Type, i, false),
					};
					auto gep = LLVMBuildGEP2(_builders.top().get(), lltype, result, gepIndices.data(), gepIndices.size(), "");
					LLVMBuildStore(_builders.top().get(), arrayItems[i], gep);
				}

				outputArgs.push_back(llcount);
				outputArgs.push_back(result);
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

	if (!klass->isStructure && !klass->isBitfield) {
		// every class requires an instance info structure
		members.push_back(_definedTypes["_Alta_instance_info"]);

		// the parents are included as members from the start of the structure
		for (const auto& parent: klass->parents) {
			members.push_back(co_await defineClassType(parent));
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
		auto entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), _initFunction, "");
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

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::returnNull() {
	co_return NULL;
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
	co_await compileNode(node->expression, info->expression);
	co_await currentStack().cleanup();
	popStack();

	if (modInitFunc) {
		exitInitFunction();
	}

	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileBlockNode(std::shared_ptr<AltaCore::AST::BlockNode> node, std::shared_ptr<AltaCore::DH::BlockNode> info) {
	for (size_t i = 0; i < node->statements.size(); ++i) {
		co_await compileNode(node->statements[i], info->statements[i]);
	}
	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileFunctionDefinitionNode(std::shared_ptr<AltaCore::AST::FunctionDefinitionNode> node, std::shared_ptr<AltaCore::DH::FunctionDefinitionNode> mainInfo) {
	bool isGeneric = mainInfo->function->genericParameterCount > 0;
	size_t iterationCount = isGeneric ? mainInfo->genericInstantiations.size() : 1;

	for (size_t i = 0; i < iterationCount; ++i) {
		auto info = isGeneric ? mainInfo->genericInstantiations[i] : mainInfo;

		auto retType = co_await translateType(info->function->returnType);

		if (info->function->isGenerator || info->function->isAsync) {
			std::cerr << "TODO: generator functions" << std::endl;
			continue;
		}

		auto mangled = mangleName(info->function);
		auto [llfuncType, llfunc] = co_await declareFunction(info->function);
		auto origLLFuncType = llfuncType;
		auto origLLFunc = llfunc;

		LLVMSetLinkage(llfunc, mangled == "main" ? LLVMExternalLinkage : LLVMInternalLinkage);

		auto entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "");
		auto builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
		LLVMPositionBuilderAtEnd(builder.get(), entryBlock);

		_builders.push(builder);
		pushStack(ScopeStack::Type::Function);

		size_t llparamIndex = info->function->isMethod ? 1 : 0;
		for (size_t i = 0; i < info->parameters.size(); ++i) {
			const auto& param = node->parameters[i];
			const auto& paramInfo = info->parameters[i];
			const auto& var = info->function->parameterVariables[i];

			auto llparam = LLVMGetParam(llfunc, llparamIndex);
			++llparamIndex;

			LLVMValueRef llparamLen = NULL;

			if (param->isVariable) {
				llparamLen = llparam;
				llparam = LLVMGetParam(llfunc, llparamIndex);
				++llparamIndex;
			}

			if (var->type->indirectionLevel() == 0) {
				llparam = co_await tmpify(llparam, var->type, false);
				currentStack().pushItem(llparam, var->type);
			}

			_definedVariables[var->id] = llparam;
			if (llparamLen) {
				_definedVariables["_Alta_array_length_" + var->id] = llparamLen;
			}
		}

		co_await compileNode(node->body, info->body);

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

		popStack();
		_builders.pop();

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

			auto entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "");
			auto builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
			LLVMPositionBuilderAtEnd(builder.get(), entryBlock);

			_builders.push(builder);
			pushStack(ScopeStack::Type::Function);

			std::vector<LLVMValueRef> args;

			if (info->function->isMethod) {
				args.push_back(LLVMGetParam(llfunc, 0));
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
					args.push_back(LLVMGetParam(llfunc, variantIndex + (info->function->isMethod ? 1 : 0)));
					++variantIndex;
				}
			}

			auto call = LLVMBuildCall2(_builders.top().get(), origLLFuncType, origLLFunc, args.data(), args.size(), "");

			currentStack().cleanup();

			if (*functionReturnType == AltaCore::DET::Type(AltaCore::DET::NativeType::Void)) {
				LLVMBuildRetVoid(_builders.top().get());
			} else {
				LLVMBuildRet(_builders.top().get(), call);
			}

			popStack();
			_builders.pop();
		}
	}

	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileReturnDirectiveNode(std::shared_ptr<AltaCore::AST::ReturnDirectiveNode> node, std::shared_ptr<AltaCore::DH::ReturnDirectiveNode> info) {
	LLVMValueRef expr = NULL;
	auto exprType = node->expression ? AltaCore::DET::Type::getUnderlyingType(info->expression.get()) : nullptr;

	if (node->expression) {
		auto functionReturnType = info->parentFunction ? (info->parentFunction->isGenerator ? info->parentFunction->generatorReturnType : (info->parentFunction->isAsync ? info->parentFunction->coroutineReturnType : info->parentFunction->returnType)) : nullptr;

		pushStack(ScopeStack::Type::Temporary);
		expr = co_await compileNode(node->expression, info->expression);
		co_await currentStack().cleanup();
		popStack();

		if (functionReturnType && functionReturnType->referenceLevel() > 0) {
			// if we're returning a reference, there's no need to copy anything
		} else {
			expr = co_await cast(expr, exprType, functionReturnType, true, additionalCopyInfo(node->expression, info->expression), false, &node->expression->position);
		}
	}

	for (auto rit = _stacks.rbegin(); rit != _stacks.rend(); ++rit) {
		auto& stack = *rit;

		co_await stack.cleanup();

		if (stack.type == ScopeStack::Type::Function) {
			break;
		}
	}

	if (node->expression) {
		if (!expr) {
			throw std::runtime_error("Invalid return value");
		}

		if (*exprType == AltaCore::DET::Type(AltaCore::DET::NativeType::Void)) {
			LLVMBuildRetVoid(_builders.top().get());
		} else {
			LLVMBuildRet(_builders.top().get(), expr);
		}
	} else {
		LLVMBuildRetVoid(_builders.top().get());
	}

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

	if (!_definedVariables[info->variable->id]) {
		if (inModuleRoot) {
			memory = LLVMAddGlobal(_llmod.get(), lltype, mangled.c_str());
			LLVMSetLinkage(memory, LLVMInternalLinkage);
		} else {
			memory = LLVMBuildAlloca(_builders.top().get(), lltype, mangled.c_str());
		}

		_definedVariables[info->variable->id] = memory;
	} else {
		memory = _definedVariables[info->variable->id];
	}

	if (!_stacks.empty()) {
		currentStack().pushItem(memory, info->variable->type);
	}

	if (node->initializationExpression) {
		auto exprType = AltaCore::DET::Type::getUnderlyingType(info->initializationExpression.get());

		if (inModuleRoot) {
			pushStack(ScopeStack::Type::Temporary);
			auto compiled = co_await compileNode(node->initializationExpression, info->initializationExpression);
			auto casted = co_await cast(compiled, exprType, info->variable->type, true, additionalCopyInfo(node->initializationExpression, info->initializationExpression), false, &node->initializationExpression->position);
			popStack();
			if (LLVMIsConstant(casted)) {
				LLVMSetInitializer(memory, casted);
			} else {
				LLVMSetInitializer(memory, LLVMGetUndef(lltype));
				LLVMBuildStore(_builders.top().get(), casted, memory);
			}
		} else {
			pushStack(ScopeStack::Type::Temporary);
			auto compiled = co_await compileNode(node->initializationExpression, info->initializationExpression);
			auto casted = co_await cast(compiled, exprType, info->variable->type, true, additionalCopyInfo(node->initializationExpression, info->initializationExpression), false, &node->initializationExpression->position);
			LLVMBuildStore(_builders.top().get(), casted, memory);
			co_await currentStack().cleanup();
			popStack();
		}
	} else if (!inModuleRoot && info->variable->type->klass && info->variable->type->indirectionLevel() == 0) {
		LLVMValueRef init = NULL;
		if (info->variable->type->klass->isBitfield || info->variable->type->klass->isStructure) {
			init = LLVMConstNull(co_await translateType(info->variable->type));
		} else {
			auto [llfuncType, llfunc] = co_await declareFunction(info->variable->type->klass->defaultConstructor);
			init = LLVMBuildCall2(_builders.top().get(), llfuncType, llfunc, nullptr, 0, "");
		}
		LLVMBuildStore(_builders.top().get(), init, memory);
	} else {
		auto null = LLVMConstNull(lltype);
		if (inModuleRoot) {
			LLVMSetInitializer(memory, null);
		} else {
			LLVMBuildStore(_builders.top().get(), null, memory);
		}
	}

	co_return memory;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileAccessor(std::shared_ptr<AltaCore::AST::Accessor> node, std::shared_ptr<AltaCore::DH::Accessor> info) {
	LLVMValueRef result = NULL;

	if (info->getsVariableLength) {
		auto targetInfo = std::dynamic_pointer_cast<AltaCore::DH::Fetch>(info->target);
		co_return _definedVariables["_Alta_array_length_" + targetInfo->narrowedTo->id];
	} else if (info->narrowedTo) {
		if (info->accessesNamespace) {
			result = _definedVariables[info->narrowedTo->id] ? _definedVariables[info->narrowedTo->id] : _definedFunctions[info->narrowedTo->id];

			if (info->narrowedTo->nodeType() == AltaCore::DET::NodeType::Function) {
				auto func = std::dynamic_pointer_cast<AltaCore::DET::Function>(info->narrowedTo);
				auto [llfuncType, llfunc] = co_await declareFunction(func);

				result = llfunc;

				if (func->isAccessor) {
					result = LLVMBuildCall2(_builders.top().get(), llfuncType, result, nullptr, 0, "");
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

			co_return result;
		} else if (auto func = std::dynamic_pointer_cast<AltaCore::DET::Function>(info->narrowedTo)) {
			auto [llfuncType, llfunc] = co_await declareFunction(func);
			co_return llfunc;
		} else {
			auto parentFunc = AltaCore::Util::getFunction(info->inputScope).lock();

			if (parentFunc && parentFunc->isLambda) {
				throw std::runtime_error("TODO: support lambdas");
			}

			result = co_await tmpify(node->target, info->target);
		}
	} else if (info->readAccessor) {
		if (!info->accessesNamespace) {
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
			gepIndices.push_back(LLVMConstInt(gepStructIndexType, index, false));
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

		for (size_t i = 1; i < info->targetType->pointerLevel(); ++i) {
			result = LLVMBuildLoad2(_builders.top().get(), co_await translateType(info->targetType->follow()), result, "");
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

			result = co_await loadRef(result, info->targetType);
			result = LLVMBuildAnd(_builders.top().get(), result, LLVMConstInt(underlyingType, mask, false), "");
			result = LLVMBuildLShr(_builders.top().get(), result, LLVMConstInt(underlyingType, start, false), "");
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
				LLVMConstInt(gepStructIndexType, index, false), // access the member
			};

			result = LLVMBuildGEP2(_builders.top().get(), co_await translateType(info->targetType->destroyIndirection()), result, gepIndices.data(), gepIndices.size(), "");
		}
	} else if (info->readAccessor) {
		auto tmpVar = LLVMBuildAlloca(_builders.top().get(), co_await translateType(info->readAccessor->returnType), "");

		if (canPush(info->readAccessor->returnType)) {
			currentStack().pushItem(tmpVar, info->readAccessor->returnType);
		}

		std::vector<LLVMValueRef> args;

		if (!info->accessesNamespace) {
			auto selfCopyInfo = additionalCopyInfo(node->target, info->target);
			auto selfType = AltaCore::DET::Type::getUnderlyingType(info->target.get());

			if (selfCopyInfo.second && node->target->nodeType() != AltaCore::AST::NodeType::FunctionCallExpression) {
				result = co_await tmpify(result, selfType);
			}

			for (size_t i = 1; i < selfType->pointerLevel(); ++i) {
				result = LLVMBuildLoad2(_builders.top().get(), co_await translateType(selfType->follow()), result, "");
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

		if (info->readAccessor->isVirtual()) {
			throw std::runtime_error("TODO: support virtual read accessors");
		} else {
			auto [llfuncType, llfunc] = co_await declareFunction(info->readAccessor);
			result = LLVMBuildCall2(_builders.top().get(), llfuncType, llfunc, args.data(), args.size(), "");
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
		throw std::runtime_error("Unable to determine accessor result");
	}

	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileFetch(std::shared_ptr<AltaCore::AST::Fetch> node, std::shared_ptr<AltaCore::DH::Fetch> info) {
	if (!info->narrowedTo) {
		if (info->readAccessor) {
			auto [llfuncType, llfunc] = co_await declareFunction(info->readAccessor);
			co_return LLVMBuildCall2(_builders.top().get(), llfuncType, llfunc, nullptr, 0, "");
		}

		throw std::runtime_error("Invalid fetch: must be narrowed");
	}

	if (info->narrowedTo->nodeType() == AltaCore::DET::NodeType::Variable && info->narrowedTo->name == "this") {
		auto var = std::dynamic_pointer_cast<AltaCore::DET::Variable>(info->narrowedTo);
		if (!var->parentScope.expired() && !var->parentScope.lock()->parentClass.expired()) {
			auto llfunc = LLVMGetBasicBlockParent(LLVMGetInsertBlock(_builders.top().get()));
			co_return LLVMGetParam(llfunc, 0);
		}
	}

	auto result = _definedVariables[info->narrowedTo->id] ? _definedVariables[info->narrowedTo->id] : _definedFunctions[info->narrowedTo->id];

	if (info->narrowedTo->nodeType() == AltaCore::DET::NodeType::Function) {
		auto func = std::dynamic_pointer_cast<AltaCore::DET::Function>(info->narrowedTo);
		auto [llfuncType, llfunc] = co_await declareFunction(func);

		result = llfunc;

		if (func->isAccessor) {
			result = LLVMBuildCall2(_builders.top().get(), llfuncType, result, nullptr, 0, "");
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

	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileAssignmentExpression(std::shared_ptr<AltaCore::AST::AssignmentExpression> node, std::shared_ptr<AltaCore::DH::AssignmentExpression> info) {
	auto lhs = co_await compileNode(node->target, info->target);
	LLVMValueRef result = NULL;

	bool isNullopt = info->targetType->isOptional && info->targetType->pointerLevel() < 1 && node->value->nodeType() == AltaCore::AST::NodeType::NullptrExpression;
	bool canCopy = !isNullopt && (!info->targetType->isNative || !info->targetType->isRawFunction) && info->targetType->pointerLevel() < 1 && (!info->strict || info->targetType->indirectionLevel() < 1) && (!info->targetType->klass || info->targetType->klass->copyConstructor);
	bool canDestroyVar = !info->operatorMethod && !info->strict && canDestroy(info->targetType);

	auto rhs = co_await compileNode(node->value, info->value);
	auto rhsType = AltaCore::DET::Type::getUnderlyingType(info->value.get());
	auto rhsTargetType = info->operatorMethod ? info->operatorMethod->parameterVariables.front()->type : info->targetType->destroyReferences();
	bool didRetrieval = false;

	if (auto acc = std::dynamic_pointer_cast<AltaCore::DH::Accessor>(info->target)) {
		if (auto var = std::dynamic_pointer_cast<AltaCore::DET::Variable>(acc->narrowedTo)) {
			if (var->isBitfieldEntry) {
				// "get operand" accesses the operand of the `lshr`, so we're left with the `and`
				auto bitfieldFetchMasked = LLVMGetOperand(lhs, 0);
				auto [start, end] = var->bitfieldBits;
				auto bitCount = (end - start) + 1;
				uint64_t mask = UINT64_MAX >> (64 - bitCount);
				auto underlyingType = co_await translateType(info->targetType->bitfield->underlyingBitfieldType.lock());

				rhs = co_await loadRef(rhs, rhsType);

				result = LLVMBuildShl(_builders.top().get(), rhs, LLVMConstInt(underlyingType, start, false), "");
				result = LLVMBuildOr(_builders.top().get(), result, bitfieldFetchMasked, "");

				co_return result;
			}
		}
	}

	if (canCopy) {
		rhs = co_await cast(rhs, rhsType, rhsTargetType, true, additionalCopyInfo(node->value, info->value), false, &node->value->position);
	} else if (isNullopt) {
		std::array<LLVMValueRef, 2> vals;
		vals[0] = LLVMConstInt(LLVMInt1TypeInContext(_llcontext.get()), 0, false);
		vals[1] = LLVMGetUndef(co_await translateType(info->targetType->optionalTarget));
		rhs = LLVMConstNamedStruct(co_await translateType(info->targetType), vals.data(), vals.size());
	}

	if (canDestroyVar) {
		co_await doDtor(lhs, info->targetType);
	}

	if (info->operatorMethod) {
		auto [llfuncType, llfunc] = co_await declareFunction(info->operatorMethod);

		std::array<LLVMValueRef, 2> args {
			co_await cast(lhs, info->targetType, info->operatorMethod->parentClassType, false, additionalCopyInfo(node->target, info->target), false, &node->target->position),
			rhs,
		};
		result = LLVMBuildCall2(_builders.top().get(), llfuncType, llfunc, args.data(), args.size(), "");
	} else {
		result = LLVMBuildStore(_builders.top().get(), rhs, lhs);
		result = lhs; // the result of the assignment is a reference to the assignee
	}

	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileBooleanLiteralNode(std::shared_ptr<AltaCore::AST::BooleanLiteralNode> node, std::shared_ptr<AltaCore::DH::BooleanLiteralNode> info) {
	co_return LLVMConstInt(LLVMInt1TypeInContext(_llcontext.get()), node->value ? 1 : 0, false);
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileBinaryOperation(std::shared_ptr<AltaCore::AST::BinaryOperation> node, std::shared_ptr<AltaCore::DH::BinaryOperation> info) {
	auto lhs = co_await compileNode(node->left, info->left);
	auto rhs = co_await compileNode(node->right, info->right);
	LLVMValueRef result = NULL;

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

		result = LLVMBuildCall2(_builders.top().get(), llfuncType, llfunc, args.data(), args.size(), "");
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

			case AltaCore::Shared::OperatorType::LogicalAnd:
			case AltaCore::Shared::OperatorType::LogicalOr:
			case AltaCore::Shared::OperatorType::EqualTo:
			case AltaCore::Shared::OperatorType::NotEqualTo:
			case AltaCore::Shared::OperatorType::GreaterThan:
			case AltaCore::Shared::OperatorType::LessThan:
			case AltaCore::Shared::OperatorType::GreaterThanOrEqualTo:
			case AltaCore::Shared::OperatorType::LessThanOrEqualTo:
				arithmetic = false;
				break;
		}

		if (arithmetic && info->commonOperandType->pointerLevel() > 0) {
			pointerArithmetic = true;
			lhs = LLVMBuildPtrToInt(_builders.top().get(), lhs, i64Type, "");
			rhs = LLVMBuildPtrToInt(_builders.top().get(), rhs, i64Type, "");
		}

		switch (node->type) {
			case AltaCore::Shared::OperatorType::Addition: {
				result = (info->commonOperandType->isFloatingPoint() ? LLVMBuildFAdd : LLVMBuildAdd)(_builders.top().get(), lhs, rhs, "");
			} break;
			case AltaCore::Shared::OperatorType::Subtraction: {
				result = (info->commonOperandType->isFloatingPoint() ? LLVMBuildFSub : LLVMBuildSub)(_builders.top().get(), lhs, rhs, "");
			} break;
			case AltaCore::Shared::OperatorType::Multiplication: {
				result = (info->commonOperandType->isFloatingPoint() ? LLVMBuildFMul : LLVMBuildMul)(_builders.top().get(), lhs, rhs, "");
			} break;
			case AltaCore::Shared::OperatorType::Division: {
				result = (info->commonOperandType->isFloatingPoint() ? LLVMBuildFDiv : (info->commonOperandType->isSigned() ? LLVMBuildSDiv : LLVMBuildUDiv))(_builders.top().get(), lhs, rhs, "");
			} break;
			case AltaCore::Shared::OperatorType::Modulo: {
				result = (info->commonOperandType->isFloatingPoint() ? LLVMBuildFRem : (info->commonOperandType->isSigned() ? LLVMBuildSRem : LLVMBuildURem))(_builders.top().get(), lhs, rhs, "");
			} break;
			case AltaCore::Shared::OperatorType::LeftShift: {
				result = LLVMBuildLShr(_builders.top().get(), lhs, rhs, "");
			} break;
			case AltaCore::Shared::OperatorType::RightShift: {
				result = (info->commonOperandType->isSigned() ? LLVMBuildAShr : LLVMBuildLShr)(_builders.top().get(), lhs, rhs, "");
			} break;

			case AltaCore::Shared::OperatorType::LogicalAnd:
				// we've already cast both the operands to bool, so we can just take the bitwise AND of the results to get the logical AND
			case AltaCore::Shared::OperatorType::BitwiseAnd: {
				result = LLVMBuildAnd(_builders.top().get(), lhs, rhs, "");
			} break;

			case AltaCore::Shared::OperatorType::LogicalOr:
				// same as for LogicalAnd
			case AltaCore::Shared::OperatorType::BitwiseOr: {
				result = LLVMBuildOr(_builders.top().get(), lhs, rhs, "");
			} break;

			case AltaCore::Shared::OperatorType::BitwiseXor: {
				result = LLVMBuildXor(_builders.top().get(), lhs, rhs, "");
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

					result = LLVMBuildFCmp(_builders.top().get(), pred, lhs, rhs, "");
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

					result = LLVMBuildICmp(_builders.top().get(), pred, lhs, rhs, "");
				}
			} break;
		}

		if (pointerArithmetic) {
			result = LLVMBuildIntToPtr(_builders.top().get(), result, opaquePtr, "");
		}
	}

	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileFunctionCallExpression(std::shared_ptr<AltaCore::AST::FunctionCallExpression> node, std::shared_ptr<AltaCore::DH::FunctionCallExpression> info) {
	LLVMValueRef result = NULL;

	if (info->isMethodCall) {
		result = co_await tmpify(info->methodClassTarget, info->methodClassTargetInfo);
	} else {
		result = co_await compileNode(node->target, info->target);
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

		for (size_t i = 1; i < selfType->pointerLevel(); ++i) {
			result = LLVMBuildLoad2(_builders.top().get(), co_await translateType(selfType->follow()), result, "");
		}

		bool didRetrieval = false;

		auto targetType = std::make_shared<AltaCore::DET::Type>(info->targetType->methodParent)->reference();

		if (virtFunc) {
			throw std::runtime_error("TODO: support virtual functions");
		} else {
			result = co_await doParentRetrieval(result, selfType, targetType, &didRetrieval);
			args.push_back(result);
		}
	} else if (!info->targetType->isRawFunction) {
		result = co_await loadRef(result, info->targetType);
	}

	if (info->isSpecialScheduleMethod) {
		std::vector<std::tuple<std::string, std::shared_ptr<AltaCore::DET::Type>, bool, std::string>> params;
		params.push_back(std::make_tuple<std::string, std::shared_ptr<AltaCore::DET::Type>, bool, std::string>("", AltaCore::DET::Type::getUnderlyingType(info->arguments[0].get()), false, ""));
		co_await processArgs(info->adjustedArguments, params, &node->position, args);
	} else {
		co_await processArgs(info->adjustedArguments, info->targetType->parameters, &node->position, args);
	}

	if (info->isMethodCall) {
		if (virtFunc) {
			throw std::runtime_error("TODO: virtual functions");
		} else {
			auto func = std::dynamic_pointer_cast<AltaCore::DET::Function>(accInfo->narrowedTo);
			auto [llfuncType, llfunc] = co_await declareFunction(func);
			result = LLVMBuildCall2(_builders.top().get(), llfuncType, llfunc, args.data(), args.size(), "");
		}
	} else if (!info->targetType->isRawFunction) {
		auto targetType = info->targetType->destroyReferences();
		auto target = result;
		auto basicFunctionType = co_await translateType(targetType);

		auto targetTypeAsRaw = targetType->copy();
		targetTypeAsRaw->isRawFunction = true;
		auto rawFunctionType = co_await translateType(targetTypeAsRaw, false);

		auto opaquePtr = LLVMPointerTypeInContext(_llcontext.get(), 0);

		std::vector<LLVMTypeRef> params(LLVMCountParamTypes(rawFunctionType) + 1);
		params[0] = opaquePtr;
		LLVMGetParamTypes(rawFunctionType, &params.data()[1]);

		auto lambdaFunctionType = LLVMFunctionType(LLVMGetReturnType(rawFunctionType), params.data(), params.size(), false);

		bool isVoid = *targetTypeAsRaw->returnType == AltaCore::DET::Type(AltaCore::DET::NativeType::Void);

		auto lambdaState = LLVMBuildExtractValue(_builders.top().get(), target, 1, "");

		args.insert(args.begin(), lambdaState);

		auto functionPointer = LLVMBuildExtractValue(_builders.top().get(), target, 0, "");

		auto lambdaStateIsNotNull = LLVMBuildIsNotNull(_builders.top().get(), lambdaState, "");

		auto llfunc = LLVMGetBasicBlockParent(LLVMGetInsertBlock(_builders.top().get()));
		auto doLambdaBlock = LLVMAppendBasicBlock(llfunc, "");
		auto doRawBlock = LLVMAppendBasicBlock(llfunc, "");
		auto doneBlock = LLVMAppendBasicBlock(llfunc, "");

		LLVMBuildCondBr(_builders.top().get(), lambdaStateIsNotNull, doLambdaBlock, doRawBlock);

		LLVMPositionBuilderAtEnd(_builders.top().get(), doLambdaBlock);
		auto lambdaResult = LLVMBuildCall2(_builders.top().get(), lambdaFunctionType, functionPointer, args.data(), args.size(), "");
		LLVMBuildBr(_builders.top().get(), doneBlock);

		LLVMPositionBuilderAtEnd(_builders.top().get(), doRawBlock);
		auto rawResult = LLVMBuildCall2(_builders.top().get(), rawFunctionType, functionPointer, &args.data()[1], args.size() - 1, "");
		LLVMBuildBr(_builders.top().get(), doneBlock);

		LLVMPositionBuilderAtEnd(_builders.top().get(), doneBlock);

		if (isVoid) {
			result = NULL;
		} else {
			result = LLVMBuildPhi(_builders.top().get(), LLVMGetReturnType(rawFunctionType), "");
			LLVMAddIncoming(result, &lambdaResult, &doLambdaBlock, 1);
			LLVMAddIncoming(result, &rawResult, &doRawBlock, 1);
		}
	} else {
		auto funcType = co_await translateType(AltaCore::DET::Type::getUnderlyingType(info->target.get())->destroyIndirection(), false);

		if (!result) {
			throw std::runtime_error("Call target is null?");
		}
		result = LLVMBuildCall2(_builders.top().get(), funcType, result, args.data(), args.size(), "");
	}

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
	auto entryBlock = LLVMGetInsertBlock(_builders.top().get());
	auto llfunc = LLVMGetBasicBlockParent(entryBlock);
	auto finalBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "");
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

		auto trueBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "");
		auto falseBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "");

		if (i == info->alternatives.size() && node->finalResult) {
			// this is the last alternative we have but we have an "else" case;
			elseBlock = falseBlock;
		}

		LLVMBuildCondBr(_builders.top().get(), lltest, trueBlock, falseBlock);

		// first, cleanup the scope in the "false" block (so that we only cleanup anything produced by the test)
		LLVMPositionBuilderAtEnd(_builders.top().get(), falseBlock);

		co_await currentStack().cleanup();

		// now, evaluate the result in the "true" block
		LLVMPositionBuilderAtEnd(_builders.top().get(), trueBlock);

		co_await compileNode(result, resultInfo);

		// now cleanup the scope in the true block
		co_await currentStack().cleanup();

		// finally, we can pop the scope stack off
		popStack();

		// we're still in the "true" block; let's go to the final block (if we don't terminate otherwise)
		if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(_builders.top().get()))) {
			LLVMBuildBr(_builders.top().get(), finalBlock);
		}

		// now let's position the builder in the "false" block so we can continue with other alternatives
		LLVMPositionBuilderAtEnd(_builders.top().get(), falseBlock);

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

		popStack();

		if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(_builders.top().get()))) {
			LLVMBuildBr(_builders.top().get(), finalBlock);
		}
	}

	LLVMPositionBuilderAtEnd(_builders.top().get(), finalBlock);

	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileConditionalExpression(std::shared_ptr<AltaCore::AST::ConditionalExpression> node, std::shared_ptr<AltaCore::DH::ConditionalExpression> info) {
	LLVMValueRef result = NULL;
	auto llfunc = LLVMGetBasicBlockParent(LLVMGetInsertBlock(_builders.top().get()));
	auto trueBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "");
	auto falseBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "");
	auto doneBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "");

	auto testType = AltaCore::DET::Type::getUnderlyingType(info->test.get());
	auto primaryType = AltaCore::DET::Type::getUnderlyingType(info->primaryResult.get());
	auto secondaryType = AltaCore::DET::Type::getUnderlyingType(info->secondaryResult.get());

	auto test = co_await tmpify(node->test, info->test);
	test = co_await cast(test, testType, std::make_shared<AltaCore::DET::Type>(AltaCore::DET::NativeType::Bool), false, additionalCopyInfo(node->test, info->test), false, &node->test->position);

	LLVMBuildCondBr(_builders.top().get(), test, trueBlock, falseBlock);

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

	currentStack().endBranch(doneBlock, { finalTrueBlock, finalFalseBlock });

	result = LLVMBuildPhi(_builders.top().get(), co_await translateType(info->commonType), "");
	LLVMAddIncoming(result, &primaryResult, &finalTrueBlock, 1);
	LLVMAddIncoming(result, &secondaryResult, &finalFalseBlock, 1);

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

		LLVMSetLinkage(initFunc, LLVMInternalLinkage);

		_builders.push(llwrap(LLVMCreateBuilderInContext(_llcontext.get())));
		pushStack(ScopeStack::Type::Function);

		auto entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), initFunc, "");
		LLVMPositionBuilderAtEnd(_builders.top().get(), entryBlock);

		auto llself = LLVMGetParam(initFunc, 0);
		auto llisRoot = LLVMGetParam(initFunc, 1);
		auto llshouldInitMembers = LLVMGetParam(initFunc, 2);

		auto initInfosBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), initFunc, "");
		auto initMembersBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), initFunc, "");
		auto doMemberInitBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), initFunc, "");
		auto exitBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), initFunc, "");

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
			std::array<LLVMValueRef, 7> lastMembers;
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
			auto infoVar = LLVMAddGlobal(_llmod.get(), _definedTypes["_Alta_class_info"], "");
			auto i64Type = LLVMInt64TypeInContext(_llcontext.get());
			auto strType = LLVMPointerType(LLVMInt8TypeInContext(_llcontext.get()), 0);
			auto dtorType = LLVMPointerType(_definedTypes["_Alta_class_destructor"], 0);

			LLVMValueRef dtor = NULL;

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
			};
			*/
			std::array<LLVMValueRef, 7> members {
				LLVMBuildGlobalStringPtr(_builders.top().get(), mangled.c_str(), ""),
				info->klass->destructor ? _definedFunctions[info->klass->destructor->id] : LLVMConstNull(dtorType),
				stackEntry.child ? LLVMBuildGlobalStringPtr(_builders.top().get(), mangledChild.c_str(), "") : LLVMConstNull(strType),
				nullptr, // filled in later
				LLVMConstInt(i64Type, stackEntry.offsetFromRoot, false),
				LLVMConstInt(i64Type, stackEntry.offsetFromOwner, false),
				LLVMConstInt(i64Type, 0, false), // updated later
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
			std::vector<LLVMValueRef> gepIndices {
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

		popStack();
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
		llretType = co_await translateType(info->specialType->type);
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

	auto entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "");
	auto builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
	LLVMPositionBuilderAtEnd(builder.get(), entryBlock);

	_builders.push(builder);
	pushStack(ScopeStack::Type::Function);

	if (isCtor) {
		size_t llparamIndex = 1;
		for (size_t i = 0; i < info->parameters.size(); ++i) {
			const auto& param = node->parameters[i];
			const auto& paramInfo = info->parameters[i];
			const auto& var = info->method->parameterVariables[i];

			auto llparam = LLVMGetParam(llfunc, llparamIndex);
			++llparamIndex;

			LLVMValueRef llparamLen = NULL;

			if (param->isVariable) {
				llparamLen = llparam;
				llparam = LLVMGetParam(llfunc, llparamIndex);
				++llparamIndex;
			}

			if (var->type->indirectionLevel() == 0) {
				llparam = co_await tmpify(llparam, var->type, false);
				currentStack().pushItem(llparam, var->type);
			}

			_definedVariables[var->id] = llparam;
			if (llparamLen) {
				_definedVariables["_Alta_array_length_" + var->id] = llparamLen;
			}
		}
	} else if (isFrom) {
		auto llparam = LLVMGetParam(llfunc, 1);
		const auto& var = info->method->parameterVariables[0];

		if (var->type->indirectionLevel() == 0) {
			llparam = co_await tmpify(llparam, var->type, false);
			currentStack().pushItem(llparam, var->type);
		}

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

			auto selfMember = LLVMBuildGEP2(_builders.top().get(), llclassType, llself, gepIndices.data(), gepIndices.size(), "");
			auto sourceMember = LLVMBuildGEP2(_builders.top().get(), llclassType, llsource, gepIndices.data(), gepIndices.size(), "");

			if (member->type->indirectionLevel() > 0) {
				LLVMBuildStore(_builders.top().get(), LLVMBuildLoad2(_builders.top().get(), co_await translateType(member->type), sourceMember, ""), selfMember);
			} else {
				auto copy = co_await doCopyCtor(sourceMember, member->type->reference(), std::make_pair(true, false));
				LLVMBuildStore(_builders.top().get(), copy, selfMember);
			}
		}
	} else {
		co_await compileNode(node->body, info->body);
	}

	if (isDtor) {
		auto llself = LLVMGetParam(llfunc, 0);

		auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
		auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());

		// first, destroy members

		for (size_t i = 0; i < info->klass->members.size(); ++i) {
			const auto& member = info->klass->members[i];

			std::array<LLVMValueRef, 2> gepIndices {
				LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
				LLVMConstInt(gepStructIndexType, 1 + info->klass->parents.size() + i, false), // the member
			};

			auto llmember = LLVMBuildGEP2(_builders.top().get(), llclassType, llself, gepIndices.data(), gepIndices.size(), "");

			if (member->type->indirectionLevel() == 0) {
				co_await doDtor(llmember, member->type->reference(), nullptr, true);
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

			auto llparent = LLVMBuildGEP2(_builders.top().get(), llclassType, llself, gepIndices.data(), gepIndices.size(), "");

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

	popStack();
	_builders.pop();

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

		auto entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc2, "");
		auto builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
		LLVMPositionBuilderAtEnd(builder.get(), entryBlock);

		_builders.push(builder);

		auto llclassRefType = LLVMPointerType(llclassType, 0);
		auto llboolType = LLVMInt1TypeInContext(_llcontext.get());
		std::array<LLVMTypeRef, 3> initFuncParams {
			llclassRefType,
			llboolType,
			llboolType,
		};

		auto instance = LLVMBuildAlloca(_builders.top().get(), llclassType, "");
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

		_builders.pop();

		// now build the persistent version
		methodID2 = "_persistent_" + methodID2;

		llfuncType2 = LLVMFunctionType(llclassRefType, llparams2.data(), llparams2.size(), false);

		if (!_definedFunctions[methodID2]) {
			auto mangled = "_persistent_" + mangleName(info->method);
			_definedFunctions[methodID2] = LLVMAddFunction(_llmod.get(), mangled.c_str(), llfuncType2);
		}

		llfunc2 = _definedFunctions[methodID2];

		entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc2, "");
		builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
		LLVMPositionBuilderAtEnd(builder.get(), entryBlock);

		_builders.push(builder);

		instance = LLVMBuildMalloc(_builders.top().get(), llclassType, "");

		args.clear();
		args.push_back(instance);

		for (size_t i = 0; i < llparams2.size(); ++i) {
			args.push_back(LLVMGetParam(llfunc2, i));
		}

		initArgs[0] = instance;

		LLVMBuildCall2(_builders.top().get(), initFuncType, initFunc, initArgs.data(), initArgs.size(), "");
		LLVMBuildCall2(_builders.top().get(), llfuncType, llfunc, args.data(), args.size(), "");

		LLVMBuildRet(_builders.top().get(), instance);

		_builders.pop();

		if (info->isCastConstructor) {
			auto [castFuncType, castFunc] = co_await declareFunction(info->correspondingMethod);

			auto entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), castFunc, "");
			auto builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
			LLVMPositionBuilderAtEnd(builder.get(), entryBlock);

			auto llval = LLVMGetParam(castFunc, 0);

			auto result = LLVMBuildCall2(builder.get(), llfuncTypeReturnsInstance, llfuncReturnsInstance, &llval, 1, "");

			LLVMBuildRet(builder.get(), result);
		}
	}

	if (info->optionalVariantFunctions.size() > 0) {
		std::cerr << "TODO: handle special class methods with optionals/defaults" << std::endl;
	}

	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileClassInstantiationExpression(std::shared_ptr<AltaCore::AST::ClassInstantiationExpression> node, std::shared_ptr<AltaCore::DH::ClassInstantiationExpression> info) {
	LLVMValueRef result = NULL;

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

		auto llparent = LLVMBuildGEP2(_builders.top().get(), llclassType, llself, gepIndices.data(), gepIndices.size(), "");

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

		auto doCtorBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "");
		auto doneBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "");

		// FIXME: this breaks short-circuiting expectations because we still evaluate the arguments even when we don't execute the call

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
			result = LLVMBuildInsertValue(_builders.top().get(), result, args[i], i, "");
		}

		if (info->persistent) {
			auto alloc = LLVMBuildMalloc(_builders.top().get(), lltype, "");
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

		auto llret = co_await translateType(retType);

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

		result = LLVMBuildCall2(_builders.top().get(), funcType, _definedFunctions[methodID], args.data(), args.size(), "");
	}

	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compilePointerExpression(std::shared_ptr<AltaCore::AST::PointerExpression> node, std::shared_ptr<AltaCore::DH::PointerExpression> info) {
	auto result = co_await compileNode(node->target, info->target);
	auto type = AltaCore::DET::Type::getUnderlyingType(info->target.get());

	if (type->referenceLevel() == 0) {
		result = co_await tmpify(result, type, true);
	}

	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileDereferenceExpression(std::shared_ptr<AltaCore::AST::DereferenceExpression> node, std::shared_ptr<AltaCore::DH::DereferenceExpression> info) {
	auto result = co_await compileNode(node->target, info->target);
	auto type = AltaCore::DET::Type::getUnderlyingType(info->target.get());

	if (type->pointerLevel() > 0) {
		result = co_await loadRef(result, type);
		// we return a reference to the pointed value, so we would only need to perform a pointer cast.
		// however, now that LLVM uses opaque pointers, we don't even need to do that; just pass the pointer on through.
		//result = LLVMBuildLoad2(_builders.top().get(), co_await translateType(type->destroyReferences()->follow()), result, "");
	} else if (type->isOptional) {
		if (type->referenceLevel() > 0) {
			auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
			auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());
			std::array<LLVMValueRef, 2> gepIndices {
				LLVMConstInt(gepIndexType, 0, false), // get the first "array" element
				LLVMConstInt(gepStructIndexType, 1, false), // get the contained type from the optional
			};
			result = LLVMBuildGEP2(_builders.top().get(), co_await translateType(type->destroyReferences()), result, gepIndices.data(), gepIndices.size(), "");
		} else {
			result = LLVMBuildExtractValue(_builders.top().get(), result, 1, "");
		}
	} else {
		throw std::runtime_error("Invalid dereference");
	}

	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileWhileLoopStatement(std::shared_ptr<AltaCore::AST::WhileLoopStatement> node, std::shared_ptr<AltaCore::DH::WhileLoopStatement> info) {
	// TODO
	std::cerr << "TODO: WhileLoopStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileCastExpression(std::shared_ptr<AltaCore::AST::CastExpression> node, std::shared_ptr<AltaCore::DH::CastExpression> info) {
	auto expr = co_await compileNode(node->target, info->target);
	auto exprType = AltaCore::DET::Type::getUnderlyingType(info->target.get());
	co_return co_await cast(expr, exprType, info->type->type, false, additionalCopyInfo(node->target, info->target), true, &node->position);
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
	LLVMValueRef result = NULL;

	if (info->enumeration) {
		throw std::runtime_error("Support enumeration lookups");
	} else {
		auto target = co_await compileNode(node->target, info->target);
		auto subscript = co_await compileNode(node->index, info->index);

		if (info->operatorMethod) {
			subscript = co_await cast(subscript, info->indexType, info->operatorMethod->parameterVariables.front()->type, true, additionalCopyInfo(node->index, info->index), false, &node->index->position);

			auto [llfuncType, llfunc] = co_await declareFunction(info->operatorMethod);

			std::array<LLVMValueRef, 2> args {
				target,
				subscript,
			};

			result = LLVMBuildCall2(_builders.top().get(), llfuncType, llfunc, args.data(), args.size(), "");
		} else {
			target = co_await loadRef(target, info->targetType);
			subscript = co_await loadRef(subscript, info->indexType);
			result = LLVMBuildGEP2(_builders.top().get(), co_await translateType(info->targetType->destroyReferences()->follow()), target, &subscript, 1, "");
		}
	}

	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileSuperClassFetch(std::shared_ptr<AltaCore::AST::SuperClassFetch> node, std::shared_ptr<AltaCore::DH::SuperClassFetch> info) {
	// TODO
	std::cerr << "TODO: SuperClassFetch" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileInstanceofExpression(std::shared_ptr<AltaCore::AST::InstanceofExpression> node, std::shared_ptr<AltaCore::DH::InstanceofExpression> info) {
	auto targetType = AltaCore::DET::Type::getUnderlyingType(info->target.get());
	auto& testType = info->type->type;

	LLVMValueRef result = co_await compileNode(node->target, info->target);
	auto llfalse = LLVMConstInt(LLVMInt1TypeInContext(_llcontext.get()), 0, false);
	auto lltrue = LLVMConstInt(LLVMInt1TypeInContext(_llcontext.get()), 1, false);

	if (testType->isUnion()) {
		if (targetType->isUnion()) {
			result = co_await loadRef(result, targetType);
			result = LLVMBuildExtractValue(_builders.top().get(), result, 0, "");

			auto targetIndex = result;

			auto targetIndexType = LLVMIntTypeInContext(_llcontext.get(), std::bit_width(targetType->unionOf.size() - 1));

			// check if any of the type IDs match
			auto testResult = llfalse;
			for (size_t i = 0; i < testType->unionOf.size(); ++i) {
				const auto& currTest = testType->unionOf[i];

				for (size_t j = 0; j < targetType->unionOf.size(); ++j) {
					const auto& currTarget = targetType->unionOf[j];

					if (*currTest == *currTarget) {
						auto test = LLVMBuildICmp(_builders.top().get(), LLVMIntEQ, targetIndex, LLVMConstInt(targetIndexType, j, false), "");
						testResult = LLVMBuildOr(_builders.top().get(), testResult, test, "");
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
		result = LLVMBuildExtractValue(_builders.top().get(), result, 0, "");

		auto targetIndex = result;

		auto targetIndexType = LLVMIntTypeInContext(_llcontext.get(), std::bit_width(targetType->unionOf.size() - 1));

		// check if any of the type IDs match
		auto testResult = llfalse;
		for (size_t j = 0; j < targetType->unionOf.size(); ++j) {
			const auto& currTarget = targetType->unionOf[j];

			if (*testType == *currTarget) {
				auto test = LLVMBuildICmp(_builders.top().get(), LLVMIntEQ, targetIndex, LLVMConstInt(targetIndexType, j, false), "");
				testResult = LLVMBuildOr(_builders.top().get(), testResult, test, "");
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

		result = co_await doChildRetrieval(result, targetType, testType->indirectionLevel() > 0 ? testType : testType->reference(), &didRetrieval);
		result = LLVMBuildIsNotNull(_builders.top().get(), result, "");
	} else {
		result = llfalse;
	}

	co_return result;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileForLoopStatement(std::shared_ptr<AltaCore::AST::ForLoopStatement> node, std::shared_ptr<AltaCore::DH::ForLoopStatement> info) {
	// TODO
	std::cerr << "TODO: ForLoopStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileRangedForLoopStatement(std::shared_ptr<AltaCore::AST::RangedForLoopStatement> node, std::shared_ptr<AltaCore::DH::RangedForLoopStatement> info) {
	auto llfunc = LLVMGetBasicBlockParent(LLVMGetInsertBlock(_builders.top().get()));

	if (node->end) {
		auto condBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "");
		auto bodyBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "");
		auto endBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "");
		auto doneBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "");

		pushStack(ScopeStack::Type::Other);

		auto llstart = co_await compileNode(node->start, info->start);
		auto castedStart = co_await cast(llstart, AltaCore::DET::Type::getUnderlyingType(info->start.get()), info->counterType->type, true, additionalCopyInfo(node->start, info->start), false, &node->start->position);
		auto startVar = co_await tmpify(castedStart, info->counterType->type, true);
		_definedVariables[info->counter->id] = startVar;

		LLVMBuildBr(_builders.top().get(), condBlock);

		pushStack(ScopeStack::Type::Other);

		LLVMPositionBuilderAtEnd(_builders.top().get(), condBlock);

		auto llend = co_await compileNode(node->end, info->end);
		auto castedEnd = co_await cast(llend, AltaCore::DET::Type::getUnderlyingType(info->end.get()), info->counterType->type, true, additionalCopyInfo(node->end, info->end), false, &node->end->position);
		auto endVal = co_await loadRef(castedEnd, info->counterType->type);

		auto startVal = co_await loadRef(startVar, info->counterType->type->reference());

		LLVMValueRef cmp = NULL;

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

			cmp = LLVMBuildFCmp(_builders.top().get(), pred, startVal, endVal, "");
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

			cmp = LLVMBuildICmp(_builders.top().get(), pred, startVal, endVal, "");
		}

		LLVMBuildCondBr(_builders.top().get(), cmp, bodyBlock, endBlock);

		// build the end block now so we can cleanup the scope without having to use `startBranch` and `endBranch`
		LLVMPositionBuilderAtEnd(_builders.top().get(), endBlock);
		co_await currentStack().cleanup();
		LLVMBuildBr(_builders.top().get(), doneBlock);

		// now build the body
		LLVMPositionBuilderAtEnd(_builders.top().get(), bodyBlock);
		co_await compileNode(node->body, info->body);
		co_await currentStack().cleanup();
		LLVMBuildBr(_builders.top().get(), condBlock);

		popStack();

		// now build the done block
		LLVMPositionBuilderAtEnd(_builders.top().get(), doneBlock);
		co_await currentStack().cleanup();

		popStack();
	} else {
		std::cerr << "TODO: support generator-based for-loops" << std::endl;
	}

	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileUnaryOperation(std::shared_ptr<AltaCore::AST::UnaryOperation> node, std::shared_ptr<AltaCore::DH::UnaryOperation> info) {
	LLVMValueRef result = NULL;
	auto compiled = co_await compileNode(node->target, info->target);

	if (info->operatorMethod) {
		if (info->targetType->referenceLevel() == 0) {
			compiled = co_await tmpify(compiled, info->targetType);
		}

		auto [llfuncType, llfunc] = co_await declareFunction(info->operatorMethod);

		std::array<LLVMValueRef, 1> args {
			co_await cast(compiled, info->targetType->reference(), info->operatorMethod->parentClassType, false, std::make_pair(false, false), false, &node->target->position),
		};

		result = LLVMBuildCall2(_builders.top().get(), llfuncType, llfunc, args.data(), args.size(), "");
	} else {
		switch (node->type) {
			case AltaCore::Shared::UOperatorType::Not: {
				auto llasBool = co_await cast(compiled, info->targetType, std::make_shared<AltaCore::DET::Type>(AltaCore::DET::NativeType::Bool), false, additionalCopyInfo(node->target, info->target), false, &node->target->position);
				result = LLVMBuildNot(_builders.top().get(), llasBool, "");
			} break;

			case AltaCore::Shared::UOperatorType::Plus: {
				// no-op... almost
				result = co_await loadRef(compiled, info->targetType);;
			} break;

			case AltaCore::Shared::UOperatorType::Minus: {
				compiled = co_await loadRef(compiled, info->targetType);
				result = (info->targetType->isFloatingPoint() ? LLVMBuildFNeg : LLVMBuildNeg)(_builders.top().get(), compiled, "");
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
					tmp = LLVMBuildPtrToInt(_builders.top().get(), tmp, lltype, "");
				}

				auto buildOp = (isDecrement ? (isFP ? LLVMBuildFSub : LLVMBuildSub) : (isFP ? LLVMBuildFAdd : LLVMBuildAdd));

				auto inc = buildOp(_builders.top().get(), tmp, LLVMConstInt(lltype, 1, false), "");

				if (info->targetType->pointerLevel() > 0) {
					inc = LLVMBuildIntToPtr(_builders.top().get(), inc, llorigType, "");
				}

				if (info->targetType->referenceLevel() > 0) {
					LLVMBuildStore(_builders.top().get(), inc, compiled);
				}

				result = (isPost ? origTmp : inc);
			} break;

			case AltaCore::Shared::UOperatorType::BitwiseNot: {
				compiled = co_await loadRef(compiled, info->targetType);
				result = LLVMBuildNot(_builders.top().get(), compiled, "");
			} break;
		}
	}

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
	// TODO
	std::cerr << "TODO: VariableDeclarationStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileDeleteStatement(std::shared_ptr<AltaCore::AST::DeleteStatement> node, std::shared_ptr<AltaCore::DH::DeleteStatement> info) {
	// TODO
	std::cerr << "TODO: DeleteStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileControlDirective(std::shared_ptr<AltaCore::AST::ControlDirective> node, std::shared_ptr<AltaCore::DH::Node> info) {
	// TODO
	std::cerr << "TODO: ControlDirective" << std::endl;
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
	// TODO
	std::cerr << "TODO: BitfieldDefinitionNode" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileLambdaExpression(std::shared_ptr<AltaCore::AST::LambdaExpression> node, std::shared_ptr<AltaCore::DH::LambdaExpression> info) {
	// TODO
	std::cerr << "TODO: LambdaExpression" << std::endl;

	auto lllambdaType = co_await translateType(AltaCore::DET::Type::getUnderlyingType(info.get()));
	co_return LLVMConstNull(lllambdaType);
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileSpecialFetchExpression(std::shared_ptr<AltaCore::AST::SpecialFetchExpression> node, std::shared_ptr<AltaCore::DH::SpecialFetchExpression> info) {
	if (invalidValueExpressionTable.find(info->id) != invalidValueExpressionTable.end()) {
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

		co_return result;
	}
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileClassOperatorDefinitionStatement(std::shared_ptr<AltaCore::AST::ClassOperatorDefinitionStatement> node, std::shared_ptr<AltaCore::DH::ClassOperatorDefinitionStatement> info) {
	auto [llfuncType, llfunc] = co_await declareFunction(info->method);

	auto entryBlock = LLVMAppendBasicBlockInContext(_llcontext.get(), llfunc, "");
	auto builder = llwrap(LLVMCreateBuilderInContext(_llcontext.get()));
	LLVMPositionBuilderAtEnd(builder.get(), entryBlock);

	_builders.push(builder);
	pushStack(ScopeStack::Type::Function);

	if (info->method->parameterVariables.size() > 0) {
		const auto& var = info->method->parameterVariables.front();
		auto llparam = LLVMGetParam(llfunc, 1);

		if (var->type->indirectionLevel() == 0) {
			llparam = co_await tmpify(llparam, var->type, false);
			currentStack().pushItem(llparam, var->type);
		}

		_definedVariables[var->id] = llparam;
	}

	co_await compileNode(node->block, info->block);

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

	popStack();
	_builders.pop();

	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileEnumerationDefinitionNode(std::shared_ptr<AltaCore::AST::EnumerationDefinitionNode> node, std::shared_ptr<AltaCore::DH::EnumerationDefinitionNode> info) {
	auto lltype = co_await translateType(info->memberType);

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

		auto llval = value ? co_await compileNode(value, valueInfo) : LLVMConstAdd(prevVal, LLVMConstInt(lltype, 1, false));

		if (!LLVMIsConstant(llval)) {
			throw std::runtime_error("Support non-constant enum values");
		}

		LLVMSetInitializer(_definedVariables[memberVar->id], llval);

		prevVal = llval;
	}

	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileYieldExpression(std::shared_ptr<AltaCore::AST::YieldExpression> node, std::shared_ptr<AltaCore::DH::YieldExpression> info) {
	// TODO
	std::cerr << "TODO: YieldExpression" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileAssertionStatement(std::shared_ptr<AltaCore::AST::AssertionStatement> node, std::shared_ptr<AltaCore::DH::AssertionStatement> info) {
	// TODO
	std::cerr << "TODO: AssertionStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::LLCoroutine AltaLL::Compiler::compileAwaitExpression(std::shared_ptr<AltaCore::AST::AwaitExpression> node, std::shared_ptr<AltaCore::DH::AwaitExpression> info) {
	// TODO
	std::cerr << "TODO: AwaitExpression" << std::endl;
	co_return NULL;
};

void AltaLL::Compiler::compile(std::shared_ptr<AltaCore::AST::RootNode> root) {
	if (!_definedTypes["_Alta_class_destructor"]) {
		std::array<LLVMTypeRef, 1> dtorParams {
			LLVMPointerType(LLVMVoidTypeInContext(_llcontext.get()), 0),
		};
		_definedTypes["_Alta_class_destructor"] = LLVMFunctionType(LLVMVoidTypeInContext(_llcontext.get()), dtorParams.data(), dtorParams.size(), false);
	}

	if (!_definedTypes["_Alta_class_info"]) {
		/*
		struct _Alta_class_info {
			const char* type_name;
			void (*destructor)(void*);
			const char* child_name;
			uint64_t offset_from_real;
			uint64_t offset_from_base;
			uint64_t offset_from_owner;
			uint64_t offset_to_next;
		};
		*/
		std::array<LLVMTypeRef, 7> members;
		auto stringType = LLVMPointerType(LLVMInt8TypeInContext(_llcontext.get()), 0);
		auto i64Type = LLVMInt64TypeInContext(_llcontext.get());
		members[0] = stringType;
		members[1] = LLVMPointerType(_definedTypes["_Alta_class_destructor"], 0);
		members[2] = stringType;
		members[3] = i64Type;
		members[4] = i64Type;
		members[5] = i64Type;
		members[6] = i64Type;
		_definedTypes["_Alta_class_info"] = LLVMStructType(members.data(), members.size(), false);
	}

	if (!_definedTypes["_Alta_instance_info"]) {
		/*
		struct _Alta_instance_info {
			struct _Alta_class_info* class_info;
		};
		*/
		std::array<LLVMTypeRef, 1> members;
		members[0] = LLVMPointerType(_definedTypes["_Alta_class_info"], 0);
		_definedTypes["_Alta_instance_info"] = LLVMStructType(members.data(), members.size(), false);
	}

	if (!_definedTypes["_Alta_basic_class"]) {
		/*
		struct _Alta_basic_class {
			struct _Alta_instance_info instance_info;
		};
		*/
		std::array<LLVMTypeRef, 1> members;
		members[0] = _definedTypes["_Alta_instance_info"];
		_definedTypes["_Alta_basic_class"] = LLVMStructType(members.data(), members.size(), false);
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

		std::array<LLVMTypeRef, 2> members {
			// function pointer
			voidPointer,

			// lambda state pointer
			voidPointer,
		};

		_definedTypes["_Alta_basic_function"] = LLVMStructType(members.data(), members.size(), false);
	}

	if (!_definedTypes["_Alta_basic_lambda_state"]) {
		/*
		struct _Alta_basic_lambda_state {
			uint64_t reference_count;
		};
		*/

		std::array<LLVMTypeRef, 1> members {
			LLVMInt64TypeInContext(_llcontext.get()),
		};

		_definedTypes["_Alta_basic_lambda_state"] = LLVMStructType(members.data(), members.size(), false);
	}

	for (size_t i = 0; i < root->statements.size(); ++i) {
		auto co = compileNode(root->statements[i], root->info->statements[i]);
		co.coroutine.resume();
	}
};

void AltaLL::Compiler::finalize() {
	if (_initFunction) {
		auto tmp = _initFunctionScopeStack->cleanup();
		tmp.coroutine.resume();

		LLVMBuildRetVoid(_initFunctionBuilder.get());

		_initFunctionBuilder = nullptr;
		_initFunctionScopeStack = ALTACORE_NULLOPT;
	}
};
