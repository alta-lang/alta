#ifndef ALTALL_COMPILER_HPP
#define ALTALL_COMPILER_HPP

#include "altacore/det-shared.hpp"
#include "altacore/det/class.hpp"
#include "altacore/det/module.hpp"
#include "altacore/det/scope-item.hpp"
#include "altacore/det/scope.hpp"
#include "altacore/errors.hpp"
#include "altacore/optional.hpp"
#include "altacore/util.hpp"
#include <altall/util.hpp>
#include <altacore.hpp>
#include <altall/altall.hpp>
#include <altall/mangle.hpp>
#include <llvm-c/Core.h>
#include <llvm-c/DebugInfo.h>
#include <llvm-c/Types.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/Metadata.h>
#include <memory>
#include <stdexcept>

#if __has_include(<coroutine>)
	#include <coroutine>
	#define ALTALL_COROUTINE_NS std
#else
	#include <experimental/coroutine>
	#define ALTALL_COROUTINE_NS std::experimental
#endif

#define ALTA_COMPILER_NAME "altac_llvm"

namespace AltaLL {
	// adapted from and based on https://itnext.io/c-20-practical-coroutines-79202872ebba
	template <typename T>
	concept ConvertibleCoroutineHandle = std::convertible_to<T, ALTALL_COROUTINE_NS::coroutine_handle<>>;

	template <typename T>
	concept HandleWithContinuation = ConvertibleCoroutineHandle<T> && requires(T x) {
		{ x.promise().continuation } -> std::convertible_to<ALTALL_COROUTINE_NS::coroutine_handle<>>;
	};

	class Compiler {
	private:
		// adapted from and based on https://itnext.io/c-20-practical-coroutines-79202872ebba
		template<typename Result>
		struct Coroutine {
			template<typename Dummy>
			struct Promise;

			using CoroutineHandle = ALTALL_COROUTINE_NS::coroutine_handle<Promise<Result>>;

			template<typename PromiseResult>
			struct Promise {
				std::conditional_t<std::is_same_v<PromiseResult, void>, bool, PromiseResult> value = {};
				ALTALL_COROUTINE_NS::coroutine_handle<> continuation {};
				bool ready = false;

				struct FinalContinuation {
					constexpr bool await_ready() const noexcept {
						return false;
					};
					// see https://stackoverflow.com/a/67557722/6620880
					void await_suspend(HandleWithContinuation auto caller) const noexcept {
						if (caller.promise().continuation && std::exchange(caller.promise().ready, true)) {
							caller.promise().continuation.resume();
						}
					};
					constexpr void await_resume() const noexcept {
						// do nothing
					};
				};

				Coroutine get_return_object() {
					return { CoroutineHandle::from_promise(*this) };
				};
				ALTALL_COROUTINE_NS::suspend_always initial_suspend() noexcept {
					return {};
				};
				FinalContinuation final_suspend() noexcept {
					return {};
				};
				void unhandled_exception() {
					try {
						throw;
					} catch (AltaCore::Errors::ValidationError& e) {
						validationErrorHandler(e);
						std::exit(11);
					}
				};
				void return_value(PromiseResult returnValue) {
					value = returnValue;
				};
			};

			// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=85282
			template<std::same_as<void> T>
			struct Promise<T> {
				ALTALL_COROUTINE_NS::coroutine_handle<> continuation {};
				bool ready = false;

				struct FinalContinuation {
					constexpr bool await_ready() const noexcept {
						return false;
					};
					// see https://stackoverflow.com/a/67557722/6620880
					void await_suspend(HandleWithContinuation auto caller) const noexcept {
						if (caller.promise().continuation && std::exchange(caller.promise().ready, true)) {
							caller.promise().continuation.resume();
						}
					};
					constexpr void await_resume() const noexcept {
						// do nothing
					};
				};

				Coroutine get_return_object() {
					return { CoroutineHandle::from_promise(*this) };
				};
				ALTALL_COROUTINE_NS::suspend_always initial_suspend() noexcept {
					return {};
				};
				FinalContinuation final_suspend() noexcept {
					return {};
				};
				void unhandled_exception() {
					std::terminate();
				};
				void return_void() {
					// do nothing
				};
			};

			using promise_type = Promise<Result>;

			CoroutineHandle coroutine;

			~Coroutine() {
				if (coroutine) {
					coroutine.destroy();
				}
			}

			Coroutine(CoroutineHandle handle):
				coroutine(handle)
				{};

			Coroutine(Coroutine&& other):
				coroutine(std::move(other.coroutine))
			{
				other.coroutine = {};
			}

			Coroutine(const Coroutine&) = delete;

			Coroutine& operator=(Coroutine&& other) {
				if (coroutine) {
					coroutine.destroy();
				}
				coroutine = std::move(other.coroutine);
				other.coroutine = {};
				return *this;
			};

			Coroutine& operator=(const Coroutine&) = delete;

			constexpr bool await_ready() const noexcept {
				return false;
			};

			// see https://stackoverflow.com/a/67557722/6620880
			bool await_suspend(ALTALL_COROUTINE_NS::coroutine_handle<> caller) noexcept {
				coroutine.promise().continuation = caller;
				coroutine.resume();
				return !std::exchange(coroutine.promise().ready, true);
			};

			Result await_resume() {
				if constexpr (!std::is_same_v<Result, void>) {
					return coroutine.promise().value;
				}
			};
		};

		using LLCoroutine = Coroutine<LLVMValueRef>;

		struct ScopeStack {
			enum class Type {
				/**
				 * Scope stacks created to represent the root scope of a function.
				 * Typically have backing AltaCore scopes.
				 */
				Function,

				/**
				 * Scope stacks created to keep track of temporary variables created as a side-effect of compilation subnodes (like expressions, for example).
				 * Typically don't have backing AltaCore scopes.
				 */
				Temporary,

				/**
				 * Any other type of scope stack.
				 * Typically have backing AltaCore scopes.
				 */
				Other,
			};

			struct ScopeItem {
				LLVMBasicBlockRef sourceBlock = nullptr;
				LLVMValueRef source = nullptr;
				std::shared_ptr<AltaCore::DET::Type> type = nullptr;
				LLVMValueRef count = nullptr;
				LLVMTypeRef countType = nullptr;
				bool branched = false;
			};

			std::vector<ScopeItem> items;
			std::stack<size_t> sizesDuringBranching;
			std::stack<ALTACORE_OPTIONAL<size_t>> suspendableStateDuringBranching;
			Compiler& compiler;
			Type type;

			LLVMValueRef suspendableStackPush = nullptr;
			LLVMValueRef suspendableContext = nullptr;
			std::vector<LLVMTypeRef> suspendableAllocationTypes;
			std::vector<LLVMValueRef> suspendableAllocations;
			LLVMBasicBlockRef suspendableReloadBlock;
			LLBuilder suspendableReloadBuilder;
			std::vector<std::pair<size_t, LLVMBasicBlockRef>> suspendableContinuations;
			std::shared_ptr<AltaCore::DET::Class> suspendableClass;
			ALTACORE_MAP<size_t, std::vector<LLVMValueRef>> suspendableAllocationUpdateRequests;

			ScopeStack(ScopeStack&& other):
				items(std::move(other.items)),
				sizesDuringBranching(std::move(other.sizesDuringBranching)),
				compiler(other.compiler),
				type(std::move(other.type))
				{};

			ScopeStack(Compiler& compiler, Type type);

			ScopeStack& operator=(ScopeStack&& other) {
				items = std::move(other.items);
				sizesDuringBranching = std::move(other.sizesDuringBranching);
				if (&compiler != &other.compiler) {
					throw std::runtime_error("this should be impossible");
				}
				type = std::move(other.type);
				return *this;
			};

			void pushItem(LLVMValueRef memory, std::shared_ptr<AltaCore::DET::Type> type);
			void pushItem(LLVMValueRef memory, std::shared_ptr<AltaCore::DET::Type> type, LLVMBasicBlockRef sourceBlock);
			void pushRuntimeArray(LLVMValueRef array, LLVMValueRef count, std::shared_ptr<AltaCore::DET::Type> elementType, LLVMTypeRef countType, LLVMBasicBlockRef sourceBlock);
			void pushRuntimeArray(LLVMValueRef array, LLVMValueRef count, std::shared_ptr<AltaCore::DET::Type> elementType, LLVMTypeRef countType);

			LLVMValueRef buildAlloca(LLVMTypeRef type, const std::string& name = "");
			LLVMValueRef buildArrayAlloca(LLVMTypeRef type, size_t elementCount, const std::string& name = "");

			void beginBranch() {
				sizesDuringBranching.push(items.size());
				suspendableStateDuringBranching.push(compiler._suspendableStateIndex.empty() ? ALTACORE_NULLOPT : ALTACORE_MAKE_OPTIONAL(compiler._suspendableStateIndex.top()));
			};

			//void forkValue(ScopeItem& item, );

			Coroutine<void> endBranch(LLVMBasicBlockRef mergeBlock, std::vector<LLVMBasicBlockRef> mergingBlocks, bool allowStateChange = false);
			Coroutine<void> cleanup(bool stateChange = false);
			Coroutine<void> popping();
		};

		struct LoopEntry {
			ScopeStack* rootScope = nullptr;
			LLVMBasicBlockRef breakBlock = NULL;
			LLVMBasicBlockRef continueBlock = NULL;

			LoopEntry(ScopeStack* _rootScope, LLVMBasicBlockRef _breakBlock, LLVMBasicBlockRef _continueBlock):
				rootScope(_rootScope),
				breakBlock(_breakBlock),
				continueBlock(_continueBlock)
				{};
		};

		struct SuspendableDestructor {
			LLVMValueRef function;
			LLBuilder builder;
			LLVMBasicBlockRef entry;
			LLVMBasicBlockRef exit;
			LLVMValueRef stateValue;
			ALTACORE_MAP<size_t, LLVMBasicBlockRef> stateUnwindBlocks;
		};

		LLContext _llcontext;
		LLModule _llmod;
		LLTargetMachine _targetMachine;
		LLTargetData _targetData;
		std::stack<LLBuilder> _builders;
		ALTACORE_MAP<std::string, LLVMTypeRef> _definedTypes;
		ALTACORE_MAP<std::string, LLVMMetadataRef> _definedDebugTypes;
		ALTACORE_MAP<std::string, LLVMValueRef> _definedFunctions;
		std::deque<ScopeStack> _stacks;
		ALTACORE_MAP<std::string, LLVMValueRef> _definedVariables;
		ALTACORE_MAP<std::string, LLVMMetadataRef> _definedDebugVariables;
		LLVMValueRef _initFunction = nullptr;
		ALTACORE_OPTIONAL<ScopeStack> _initFunctionScopeStack = ALTACORE_NULLOPT;
		LLBuilder _initFunctionBuilder;
		std::stack<size_t> temporaryIndices;
		LLDIBuilder _debugBuilder = nullptr;
		ALTACORE_MAP<AltaCore::Filesystem::Path, LLVMMetadataRef> _debugFiles;
		ALTACORE_MAP<AltaCore::Filesystem::Path, LLVMMetadataRef> _compileUnits;
		ALTACORE_MAP<AltaCore::Filesystem::Path, LLVMMetadataRef> _rootDebugScopes;
		AltaCore::Filesystem::Path _currentFile;
		ALTACORE_MAP<std::string, LLVMMetadataRef> _definedScopes;
		uint64_t _pointerBits;
		LLVMMetadataRef _unknownDebugFile;
		LLVMMetadataRef _unknownDebugUnit;
		LLVMMetadataRef _unknownRootDebugScope;
		unsigned int _compositeCounter = 0;
		std::stack<LLVMMetadataRef> _debugLocations;
		std::stack<LoopEntry> _loops;
		ALTACORE_MAP<std::string, LLVMValueRef> _bitfieldAccessTargets;
		std::stack<LLVMValueRef> _suspendableContext;
		std::stack<size_t> _suspendableStateIndex;
		std::stack<ALTACORE_MAP<size_t, LLVMBasicBlockRef>> _suspendableStateBlocks;
		std::stack<LLVMValueRef> _thisContextValue;
		std::stack<std::pair<std::string, LLVMMetadataRef>> _overrideFuncScopes;
		std::stack<SuspendableDestructor> _suspendableDestructor;
		std::stack<std::shared_ptr<AltaCore::DET::Class>> _suspendableClass;

		inline LLVMMetadataRef currentDebugFile() {
			return _debugFiles[_currentFile];
		};

		inline LLVMMetadataRef currentDebugUnit() {
			return _compileUnits[_currentFile];
		};

		inline LLVMMetadataRef currentRootDebugScope() {
			return _rootDebugScopes[_currentFile];
		};

		inline bool inSuspendableFunction() {
			if (_suspendableContext.empty()) {
				return false;
			}
			return !!_suspendableContext.top();
		}

		inline LLVMValueRef currentSuspendableContext() {
			return _suspendableContext.empty() ? NULL : _suspendableContext.top();
		};

		LLVMMetadataRef translateTypeDebug(std::shared_ptr<AltaCore::DET::Type> type, bool usePointersToFunctions = true);
		LLVMMetadataRef translateClassDebug(std::shared_ptr<AltaCore::DET::Class> klass);

		inline LLVMMetadataRef debugFileForModule(std::shared_ptr<AltaCore::DET::Module> module) {
			if (!_debugFiles[module->path]) {
				auto filenameStr = module->path.basename();
				auto dirnameStr = module->path.dirname().toString();
				_debugFiles[module->path] = LLVMDIBuilderCreateFile(_debugBuilder.get(), filenameStr.c_str(), filenameStr.size(), dirnameStr.c_str(), dirnameStr.size());
				_compileUnits[module->path] = LLVMDIBuilderCreateCompileUnit(_debugBuilder.get(), LLVMDWARFSourceLanguageC, _debugFiles[module->path], ALTA_COMPILER_NAME, sizeof(ALTA_COMPILER_NAME), /* TODO */ false, "", 0, 0x01000000, "", 0, LLVMDWARFEmissionFull, 0, true, false, "", 0, "", 0);
				_rootDebugScopes[module->path] = _debugFiles[module->path];
			}
			return _debugFiles[module->path];
		};

		inline LLVMMetadataRef debugUnitForModule(std::shared_ptr<AltaCore::DET::Module> module) {
			debugFileForModule(module);
			return _compileUnits[module->path];
		};

		inline LLVMMetadataRef rootDebugScopeForModule(std::shared_ptr<AltaCore::DET::Module> module) {
			debugFileForModule(module);
			return _rootDebugScopes[module->path];
		};

		inline LLVMMetadataRef translateFunction(std::shared_ptr<AltaCore::DET::Function> func, bool asDefinition = false) {
			if (!_overrideFuncScopes.empty() && _overrideFuncScopes.top().first == func->id) {
				return _overrideFuncScopes.top().second;
			}

			// if we already had a scope defined for this function, it was just a declaration, so we overwrite it unconditionally.
			// nothing outside the function needs access to the function definition's scope, so this should be perfectly fine.
			bool needsToBeSet = !_definedScopes[func->id] || (asDefinition && !reinterpret_cast<llvm::MDNode*>(_definedScopes[func->id])->isTemporary() && !reinterpret_cast<llvm::DISubprogram*>(_definedScopes[func->id])->isDefinition());
			if (needsToBeSet) {
				auto mangled = mangleName(func);
				auto parentScope = func->parentScope.lock();
				auto parentModule = AltaCore::Util::getModule(parentScope.get()).lock();
				auto debugFile = debugFileForModule(parentModule);

				// create a temporary node to resolve cycles in the scope resolution
				auto tmpNode = _definedScopes[func->id] = LLVMTemporaryMDNode(_llcontext.get(), NULL, 0);

				auto llparentScope = translateScope(parentScope);

				if (func->isLambda) {
					// a subprogram can't be the direct child of another subprogram.
					// *however*, a subprogram is allowed to have a nested class/structure definition,
					// and *this* can have a subprogram attached to it.
					auto dummy = LLVMDIBuilderCreateStructType(_debugBuilder.get(), translateParentScope(AltaCore::Util::getFunction(parentScope).lock()), "<lambda>", sizeof("<lambda>") - 1, debugFile, 0, 0, 0, LLVMDIFlagArtificial, NULL, NULL, 0, 0, NULL, "", 0);
					llparentScope = dummy;
				}

				auto altaFuncType = AltaCore::DET::Type::getUnderlyingType(func);

				if (func->isLambda) {
					altaFuncType->isRawFunction = true;
					altaFuncType->parameters.insert(altaFuncType->parameters.begin(), { "@lambda_state", std::make_shared<AltaCore::DET::Type>(AltaCore::DET::NativeType::Void)->point(), false, "" });
				}

				LLVMMetadataRef funcType = translateTypeDebug(altaFuncType, false);

				uint64_t flags = LLVMDIFlagZero;

				switch (func->visibility) {
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

				_definedScopes[func->id] = LLVMDIBuilderCreateFunction(_debugBuilder.get(), llparentScope, func->name.c_str(), func->name.size(), mangled.c_str(), mangled.size(), debugFile, func->position.line, funcType, false, asDefinition, func->position.line, static_cast<LLVMDIFlags>(flags), /* TODO */ false);

				// now replace all uses of the temporary node
				LLVMMetadataReplaceAllUsesWith(tmpNode, _definedScopes[func->id]);
			}
			return _definedScopes[func->id];
		};

		inline LLVMMetadataRef debugFileForScope(std::shared_ptr<AltaCore::DET::Scope> scope) {
			if (auto mod = AltaCore::Util::getModule(scope.get()).lock()) {
				return debugFileForModule(mod);
			} else {
				return _unknownDebugFile;
			}
		};

		inline LLVMMetadataRef debugFileForScopeItem(std::shared_ptr<AltaCore::DET::ScopeItem> item) {
			if (auto scope = item->parentScope.lock()) {
				return debugFileForScope(scope);
			} else {
				return _unknownDebugFile;
			}
		};

		inline LLVMMetadataRef debugUnitForScope(std::shared_ptr<AltaCore::DET::Scope> scope) {
			if (auto mod = AltaCore::Util::getModule(scope.get()).lock()) {
				return debugUnitForModule(mod);
			} else {
				return _unknownDebugUnit;
			}
		};

		inline LLVMMetadataRef debugUnitForScopeItem(std::shared_ptr<AltaCore::DET::ScopeItem> item) {
			if (auto scope = item->parentScope.lock()) {
				return debugUnitForScope(scope);
			} else {
				return _unknownDebugUnit;
			}
		};

		inline LLVMMetadataRef rootDebugScopeForScope(std::shared_ptr<AltaCore::DET::Scope> scope) {
			if (auto mod = AltaCore::Util::getModule(scope.get()).lock()) {
				return rootDebugScopeForModule(mod);
			} else {
				return _unknownRootDebugScope;
			}
		};

		inline LLVMMetadataRef rootDebugScopeForScopeItem(std::shared_ptr<AltaCore::DET::ScopeItem> item) {
			if (auto scope = item->parentScope.lock()) {
				return rootDebugScopeForScope(scope);
			} else {
				return _unknownRootDebugScope;
			}
		};

		inline LLVMMetadataRef translateScope(std::shared_ptr<AltaCore::DET::Scope> scope) {
			if (!scope) {
				return _unknownRootDebugScope;
			}

			auto scopeID = scope->id;

			if (!_overrideFuncScopes.empty()) {
				if (auto func = AltaCore::Util::getFunction(scope).lock()) {
					if (func->id == _overrideFuncScopes.top().first) {
						scopeID += "@override";
					}
				}
			}

			auto it = _definedScopes.find(scopeID);
			if (it != _definedScopes.end()) {
				return it->second;
			}

			LLVMMetadataRef llscope = nullptr;

			if (auto parentClass = scope->parentClass.lock()) {
				llscope = translateClassDebug(parentClass);
			} else if (auto parentFunc = scope->parentFunction.lock()) {
				llscope = translateFunction(parentFunc);
			} else if (auto parentNS = scope->parentNamespace.lock()) {
				if (!_definedScopes[parentNS->id]) {
					_definedScopes[parentNS->id] = LLVMDIBuilderCreateNameSpace(_debugBuilder.get(), translateScope(parentNS->parentScope.lock()), parentNS->name.c_str(), parentNS->name.size(), false);
				}
				llscope = _definedScopes[parentNS->id];
			} else if (auto parentModule = scope->parentModule.lock()) {
				llscope = rootDebugScopeForModule(parentModule);
			} else {
				llscope = LLVMDIBuilderCreateLexicalBlock(_debugBuilder.get(), translateScope(scope->parent.lock()), debugFileForModule(AltaCore::Util::getModule(scope.get()).lock()), scope->position.line, scope->position.column);
			}

			_definedScopes[scopeID] = llscope;
			return llscope;
		};

		inline LLVMMetadataRef translateParentScope(std::shared_ptr<AltaCore::DET::ScopeItem> item) {
			if (item->nodeType() == AltaCore::DET::NodeType::Class) {
				if (auto func = AltaCore::Util::getFunction(item->parentScope.lock()).lock()) {
					return translateParentScope(func);
				}
			}
			if (auto scope = item->parentScope.lock()) {
				return translateScope(scope);
			} else {
				return _unknownRootDebugScope;
			}
		};

		inline ScopeStack& currentStack() {
			return _stacks.back();
		};

		inline ScopeStack& currentNonTemporaryStack() {
			for (auto rit = _stacks.rbegin(); rit != _stacks.rend(); ++rit) {
				if (rit->type != ScopeStack::Type::Temporary) {
					return *rit;
				}
			}
			throw std::runtime_error("No non-temporary stack?");
		};

		inline void pushStack(ScopeStack::Type type) {
			if (type == ScopeStack::Type::Function) {
				temporaryIndices.push(0);
			}
			_stacks.emplace_back(*this, type);
		};

		inline Coroutine<void> popStack() {
			if (_stacks.back().type == ScopeStack::Type::Function) {
				temporaryIndices.pop();
			}
			co_await _stacks.back().popping();
			_stacks.pop_back();
		};

		inline LoopEntry& currentLoop() {
			return _loops.top();
		};

		inline void pushLoop(LLVMBasicBlockRef breakBlock = NULL, LLVMBasicBlockRef continueBlock = NULL) {
			_loops.emplace(&currentStack(), breakBlock, continueBlock);
		};

		inline void popLoop() {
			_loops.pop();
		};

		inline size_t nextTemp() {
			return temporaryIndices.top()++;
		};

		inline void pushEmptyDebugLocation() {
			_debugLocations.push(nullptr);
			LLVMSetCurrentDebugLocation2(_builders.top().get(), NULL);
		};

		inline void pushCustomDebugLocation(size_t line, size_t column, std::shared_ptr<AltaCore::DET::Scope> scope) {
			auto debugScope = translateScope(scope);
			if (!scope && _debugLocations.size() > 0) {
				auto& loc = _debugLocations.top();
				if (loc) {
					auto maybeScope = LLVMDILocationGetScope(loc);
					if (maybeScope) {
						debugScope = maybeScope;
					}
				}
			}
			if (llvm::isa<llvm::DILocalScope>(reinterpret_cast<llvm::DIScope*>(debugScope))) {
				auto loc = LLVMDIBuilderCreateDebugLocation(_llcontext.get(), line, column, debugScope, NULL);
				_debugLocations.push(loc);
				LLVMSetCurrentDebugLocation2(_builders.top().get(), loc);
			} else {
				return pushEmptyDebugLocation();
			}
		};

		inline void pushUnknownDebugLocation(std::shared_ptr<AltaCore::DET::Scope> scope = nullptr) {
			return pushCustomDebugLocation(0, 0, scope);
		};

		inline void pushDebugLocation(const AltaCore::Errors::Position& position, std::shared_ptr<AltaCore::DET::Scope> scope = nullptr) {
			return pushCustomDebugLocation(position.line, position.column, scope);
		};

		inline void setBuilderDebugLocation(LLBuilder builder, size_t line, size_t column) {
			LLVMMetadataRef debugScope = _unknownDebugFile;
			if (_debugLocations.size() > 0) {
				auto& loc = _debugLocations.top();
				if (loc) {
					auto maybeScope = LLVMDILocationGetScope(loc);
					if (maybeScope) {
						debugScope = maybeScope;
					}
				}
			}
			if (llvm::isa<llvm::DILocalScope>(reinterpret_cast<llvm::DIScope*>(debugScope))) {
				auto loc = LLVMDIBuilderCreateDebugLocation(_llcontext.get(), line, column, debugScope, NULL);
				LLVMSetCurrentDebugLocation2(builder.get(), loc);
			} else {
				LLVMSetCurrentDebugLocation2(builder.get(), NULL);
			}
		};

		inline void popDebugLocation() {
			_debugLocations.pop();
			if (!_debugLocations.empty()) {
				LLVMSetCurrentDebugLocation2(_builders.top().get(), _debugLocations.top());
			}
		};

		Coroutine<LLVMTypeRef> translateType(std::shared_ptr<AltaCore::DET::Type> type, bool usePointersToFunctions = true, bool useStructForVoid = true);

		/**
		 * whether it requires copying
		 */
		using CopyInfo = std::pair<bool, bool>;

		static const CopyInfo defaultCopyInfo;
		static ALTACORE_MAP<std::string, std::shared_ptr<AltaCore::DET::Type>> invalidValueExpressionTable;

		friend void ::AltaLL::registerAttributes(AltaCore::Filesystem::Path);

		LLCoroutine cast(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType, std::shared_ptr<AltaCore::DET::Type> dest, bool copy, CopyInfo additionalCopyInfo, bool manual, AltaCore::Errors::Position* position);

		inline CopyInfo additionalCopyInfo(std::shared_ptr<AltaCore::AST::Node> node, std::shared_ptr<AltaCore::DH::Node> info) const {
			using ANT = AltaCore::AST::NodeType;
			namespace AAST = AltaCore::AST;
			namespace DH = AltaCore::DH;

			ANT type = node->nodeType();
			bool canCopy = true;
			bool canTempify = /*false*/ true;
			if (type == ANT::CastExpression) {
				auto cast = std::dynamic_pointer_cast<AAST::CastExpression>(node);
				auto det = std::dynamic_pointer_cast<DH::CastExpression>(info);

				//canTempify = det->usesFromOrTo;
				//canCopy = !canTempify;
				canCopy = !det->usesFromOrTo;
			}
			if (type == ANT::BinaryOperation) {
				auto op = std::dynamic_pointer_cast<AAST::BinaryOperation>(node);
				auto det = std::dynamic_pointer_cast<DH::BinaryOperation>(info);

				canCopy = !det->operatorMethod;
				//canTempify = !canCopy;
			}
			if (type == ANT::UnaryOperation) {
				auto op = std::dynamic_pointer_cast<AAST::UnaryOperation>(node);
				auto det = std::dynamic_pointer_cast<DH::UnaryOperation>(info);

				canCopy = !det->operatorMethod;
				//canTempify = !canCopy;
			}
			if (type == ANT::SubscriptExpression) {
				auto subs = std::dynamic_pointer_cast<AAST::SubscriptExpression>(node);
				auto det = std::dynamic_pointer_cast<DH::SubscriptExpression>(info);

				canCopy = !det->enumeration && !det->operatorMethod;
				//canTempify = !canCopy;
			}
			if (type == ANT::YieldExpression) {
				auto yield = std::dynamic_pointer_cast<AAST::YieldExpression>(node);
				auto det = std::dynamic_pointer_cast<DH::YieldExpression>(info);

				canCopy = false;
				//canTempify = false;
			}
			if (type == ANT::SpecialFetchExpression) {
				// NOTE: this doesn't technically cover all cases where invalid values could be provided, but
				// given that invalid values are actually an ugly hack that are only meant to be used internally,
				// this should be fine for all practical use cases
				auto special = std::dynamic_pointer_cast<AAST::SpecialFetchExpression>(node);
				auto det = std::dynamic_pointer_cast<DH::SpecialFetchExpression>(info);

				if (invalidValueExpressionTable.find(det->id) != invalidValueExpressionTable.end()) {
					canCopy = false;
					//canTempify = true;
				}
			}
			return std::make_pair(
				(
					type != ANT::ClassInstantiationExpression &&
					type != ANT::FunctionCallExpression &&
					type != ANT::LambdaExpression &&
					canCopy
				),
				(
					type == ANT::FunctionCallExpression ||
					type == ANT::ClassInstantiationExpression ||
					type == ANT::ConditionalExpression ||
					type == ANT::LambdaExpression ||
					canTempify
				)
			);
		};

		LLCoroutine doCopyCtorInternal(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType, CopyInfo additionalCopyInfo, bool* didCopy);
		LLCoroutine doCopyCtor(LLVMValueRef compiled, std::shared_ptr<AltaCore::AST::ExpressionNode> expr, std::shared_ptr<AltaCore::DH::ExpressionNode> info, bool* didCopy = nullptr);
		LLCoroutine doCopyCtor(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType, CopyInfo additionalCopyInfo, bool* didCopy = nullptr);
		LLCoroutine doDtor(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType, bool* didDtor = nullptr, bool force = false);

		LLCoroutine loadRef(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType, size_t finalRefLevel = 0);
		LLCoroutine loadIndirection(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType, size_t finalIndirLevel = 0);

		LLCoroutine getRealInstance(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType);
		LLCoroutine getRootInstance(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType);

		LLCoroutine doParentRetrieval(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType, std::shared_ptr<AltaCore::DET::Type> targetType, bool* didRetrieval = nullptr);
		LLCoroutine doChildRetrieval(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType, std::shared_ptr<AltaCore::DET::Type> targetType, bool* didRetrieval = nullptr);

		LLCoroutine tmpify(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> type, bool withStack = true, bool tmpifyRef = false);
		LLCoroutine tmpify(std::shared_ptr<AltaCore::AST::ExpressionNode> expr, std::shared_ptr<AltaCore::DH::ExpressionNode> info);

		Coroutine<void> processArgs(std::vector<ALTACORE_VARIANT<std::pair<std::shared_ptr<AltaCore::AST::ExpressionNode>, std::shared_ptr<AltaCore::DH::ExpressionNode>>, std::vector<std::pair<std::shared_ptr<AltaCore::AST::ExpressionNode>, std::shared_ptr<AltaCore::DH::ExpressionNode>>>>> adjustedArguments, std::vector<std::tuple<std::string, std::shared_ptr<AltaCore::DET::Type>, bool, std::string>> parameters, AltaCore::Errors::Position* position, std::vector<LLVMValueRef>& outputArgs);

		Coroutine<LLVMTypeRef> defineClassType(std::shared_ptr<AltaCore::DET::Class> klass);
		Coroutine<std::pair<LLVMTypeRef, LLVMValueRef>> declareFunction(std::shared_ptr<AltaCore::DET::Function> function, LLVMTypeRef llfunctionType = nullptr);

		LLVMValueRef enterInitFunction();
		void exitInitFunction();

		LLCoroutine forEachUnionMember(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> type, LLVMTypeRef returnValueType, std::function<LLCoroutine(LLVMValueRef memberRef, std::shared_ptr<AltaCore::DET::Type> memberType, size_t memberIndex)> callback);

		inline bool canDestroy(std::shared_ptr<AltaCore::DET::Type> exprType, bool force = false) const {
			return (
				((force && exprType->pointerLevel() < 1) || exprType->indirectionLevel() < 1) &&
				(
					(
						!exprType->isNative &&
						(
							exprType->isUnion() ||
							(exprType->isOptional && *exprType->optionalTarget != AltaCore::DET::Type(AltaCore::DET::NativeType::Void)) ||
							(exprType->klass && exprType->klass->destructor)
						)
					) ||
					(
						exprType->isFunction &&
						!exprType->isRawFunction
					)
				)
			);
		};

		inline bool canPush(std::shared_ptr<AltaCore::DET::Type> exprType) const {
			return (
				exprType->indirectionLevel() == 0 &&
				(
					(
						!exprType->isNative &&
						(
							!exprType->klass ||
							(exprType->klass && !exprType->klass->isStructure)
						)
					) ||
					(
						exprType->isFunction &&
						!exprType->isRawFunction
					)
				)
			);
		};

		inline Coroutine<void> updateSuspendableStateIndex(size_t newIndex) {
			auto gepIndexType = LLVMInt64TypeInContext(_llcontext.get());
			auto gepStructIndexType = LLVMInt32TypeInContext(_llcontext.get());

			std::array<LLVMValueRef, 4> stateGEPIndices {
				LLVMConstInt(gepIndexType, 0, false), // the first element in the "array"
				LLVMConstInt(gepStructIndexType, 0, false), // @Suspendable@::basic_context
				LLVMConstInt(gepStructIndexType, 0, false), // _Alta_basic_{coroutine,generator}::basic_suspendable
				LLVMConstInt(gepStructIndexType, 1, false), // _Alta_basic_suspendable::state
			};
			auto stateGEP = LLVMBuildGEP2(_builders.top().get(), co_await defineClassType(_suspendableClass.top()), currentSuspendableContext(), stateGEPIndices.data(), stateGEPIndices.size(), "@state_update_ref");
			LLVMBuildStore(_builders.top().get(), LLVMConstInt(LLVMInt64TypeInContext(_llcontext.get()), newIndex, false), stateGEP);
		}

		std::pair<LLVMValueRef, LLVMTypeRef> defineRuntimeFunction(const std::string& name);

		// <runtime-builders>
		LLVMValueRef buildSuspendablePushStack(LLBuilder builder, LLVMValueRef suspendableContext, LLVMValueRef stackSize);
		LLVMValueRef buildSuspendablePushStack(LLBuilder builder, LLVMValueRef suspendableContext, size_t stackSize);
		void buildSuspendablePopStack(LLBuilder builder, LLVMValueRef suspendableContext);
		LLVMValueRef buildSuspendableAlloca(LLBuilder builder, LLVMValueRef suspendableContext);
		void buildSuspendableReload(LLBuilder builder, LLVMValueRef suspendableContext);
		void buildSuspendableReloadContinue(LLBuilder builder, LLVMValueRef suspendableContext);
		LLVMValueRef buildGetChild(LLBuilder builder, LLVMValueRef instance, const std::vector<std::string>& parentContainerPath);
		void buildBadCast(LLBuilder builder, const std::string& from, const std::string& to);
		void buildBadEnum(LLBuilder builder, const std::string& enumType, LLVMValueRef badEnumValue);
		LLVMValueRef buildVirtualLookup(LLBuilder builder, LLVMValueRef instance, const std::string& methodName);

		void updateSuspendableAlloca(LLVMValueRef alloca, size_t stackOffset);
		void updateSuspendablePushStack(LLVMValueRef push, size_t stackSize);
		// </runtime-builders>

		LLCoroutine returnNull();

		// not actually a coroutine, just picks the appropriate compiler method and invokes it
		LLCoroutine compileNode(std::shared_ptr<AltaCore::AST::Node> node, std::shared_ptr<AltaCore::DH::Node> info);

		LLCoroutine compileExpressionStatement(std::shared_ptr<AltaCore::AST::ExpressionStatement> node, std::shared_ptr<AltaCore::DH::ExpressionStatement> info);
		LLCoroutine compileBlockNode(std::shared_ptr<AltaCore::AST::BlockNode> node, std::shared_ptr<AltaCore::DH::BlockNode> info);
		LLCoroutine compileFunctionDefinitionNode(std::shared_ptr<AltaCore::AST::FunctionDefinitionNode> node, std::shared_ptr<AltaCore::DH::FunctionDefinitionNode> info);
		LLCoroutine compileReturnDirectiveNode(std::shared_ptr<AltaCore::AST::ReturnDirectiveNode> node, std::shared_ptr<AltaCore::DH::ReturnDirectiveNode> info);
		LLCoroutine compileIntegerLiteralNode(std::shared_ptr<AltaCore::AST::IntegerLiteralNode> node, std::shared_ptr<AltaCore::DH::IntegerLiteralNode> info);
		LLCoroutine compileVariableDefinitionExpression(std::shared_ptr<AltaCore::AST::VariableDefinitionExpression> node, std::shared_ptr<AltaCore::DH::VariableDefinitionExpression> info);
		LLCoroutine compileAccessor(std::shared_ptr<AltaCore::AST::Accessor> node, std::shared_ptr<AltaCore::DH::Accessor> info);
		LLCoroutine compileFetch(std::shared_ptr<AltaCore::AST::Fetch> node, std::shared_ptr<AltaCore::DH::Fetch> info);
		LLCoroutine compileAssignmentExpression(std::shared_ptr<AltaCore::AST::AssignmentExpression> node, std::shared_ptr<AltaCore::DH::AssignmentExpression> info);
		LLCoroutine compileBooleanLiteralNode(std::shared_ptr<AltaCore::AST::BooleanLiteralNode> node, std::shared_ptr<AltaCore::DH::BooleanLiteralNode> info);
		LLCoroutine compileBinaryOperation(std::shared_ptr<AltaCore::AST::BinaryOperation> node, std::shared_ptr<AltaCore::DH::BinaryOperation> info);
		LLCoroutine compileFunctionCallExpression(std::shared_ptr<AltaCore::AST::FunctionCallExpression> node, std::shared_ptr<AltaCore::DH::FunctionCallExpression> info);
		LLCoroutine compileStringLiteralNode(std::shared_ptr<AltaCore::AST::StringLiteralNode> node, std::shared_ptr<AltaCore::DH::StringLiteralNode> info);
		LLCoroutine compileFunctionDeclarationNode(std::shared_ptr<AltaCore::AST::FunctionDeclarationNode> node, std::shared_ptr<AltaCore::DH::FunctionDeclarationNode> info);
		LLCoroutine compileAttributeStatement(std::shared_ptr<AltaCore::AST::AttributeStatement> node, std::shared_ptr<AltaCore::DH::AttributeStatement> info);
		LLCoroutine compileConditionalStatement(std::shared_ptr<AltaCore::AST::ConditionalStatement> node, std::shared_ptr<AltaCore::DH::ConditionalStatement> info);
		LLCoroutine compileConditionalExpression(std::shared_ptr<AltaCore::AST::ConditionalExpression> node, std::shared_ptr<AltaCore::DH::ConditionalExpression> info);
		LLCoroutine compileClassDefinitionNode(std::shared_ptr<AltaCore::AST::ClassDefinitionNode> node, std::shared_ptr<AltaCore::DH::ClassDefinitionNode> info);
		LLCoroutine compileClassMethodDefinitionStatement(std::shared_ptr<AltaCore::AST::ClassMethodDefinitionStatement> node, std::shared_ptr<AltaCore::DH::ClassMethodDefinitionStatement> info);
		LLCoroutine compileClassSpecialMethodDefinitionStatement(std::shared_ptr<AltaCore::AST::ClassSpecialMethodDefinitionStatement> node, std::shared_ptr<AltaCore::DH::ClassSpecialMethodDefinitionStatement> info);
		LLCoroutine compileClassInstantiationExpression(std::shared_ptr<AltaCore::AST::ClassInstantiationExpression> node, std::shared_ptr<AltaCore::DH::ClassInstantiationExpression> info);
		LLCoroutine compilePointerExpression(std::shared_ptr<AltaCore::AST::PointerExpression> node, std::shared_ptr<AltaCore::DH::PointerExpression> info);
		LLCoroutine compileDereferenceExpression(std::shared_ptr<AltaCore::AST::DereferenceExpression> node, std::shared_ptr<AltaCore::DH::DereferenceExpression> info);
		LLCoroutine compileWhileLoopStatement(std::shared_ptr<AltaCore::AST::WhileLoopStatement> node, std::shared_ptr<AltaCore::DH::WhileLoopStatement> info);
		LLCoroutine compileCastExpression(std::shared_ptr<AltaCore::AST::CastExpression> node, std::shared_ptr<AltaCore::DH::CastExpression> info);
		LLCoroutine compileClassReadAccessorDefinitionStatement(std::shared_ptr<AltaCore::AST::ClassReadAccessorDefinitionStatement> node, std::shared_ptr<AltaCore::DH::ClassReadAccessorDefinitionStatement> info);
		LLCoroutine compileCharacterLiteralNode(std::shared_ptr<AltaCore::AST::CharacterLiteralNode> node, std::shared_ptr<AltaCore::DH::CharacterLiteralNode> info);
		LLCoroutine compileSubscriptExpression(std::shared_ptr<AltaCore::AST::SubscriptExpression> node, std::shared_ptr<AltaCore::DH::SubscriptExpression> info);
		LLCoroutine compileSuperClassFetch(std::shared_ptr<AltaCore::AST::SuperClassFetch> node, std::shared_ptr<AltaCore::DH::SuperClassFetch> info);
		LLCoroutine compileInstanceofExpression(std::shared_ptr<AltaCore::AST::InstanceofExpression> node, std::shared_ptr<AltaCore::DH::InstanceofExpression> info);
		LLCoroutine compileForLoopStatement(std::shared_ptr<AltaCore::AST::ForLoopStatement> node, std::shared_ptr<AltaCore::DH::ForLoopStatement> info);
		LLCoroutine compileRangedForLoopStatement(std::shared_ptr<AltaCore::AST::RangedForLoopStatement> node, std::shared_ptr<AltaCore::DH::RangedForLoopStatement> info);
		LLCoroutine compileUnaryOperation(std::shared_ptr<AltaCore::AST::UnaryOperation> node, std::shared_ptr<AltaCore::DH::UnaryOperation> info);
		LLCoroutine compileSizeofOperation(std::shared_ptr<AltaCore::AST::SizeofOperation> node, std::shared_ptr<AltaCore::DH::SizeofOperation> info);
		LLCoroutine compileFloatingPointLiteralNode(std::shared_ptr<AltaCore::AST::FloatingPointLiteralNode> node, std::shared_ptr<AltaCore::DH::FloatingPointLiteralNode> info);
		LLCoroutine compileStructureDefinitionStatement(std::shared_ptr<AltaCore::AST::StructureDefinitionStatement> node, std::shared_ptr<AltaCore::DH::StructureDefinitionStatement> info);
		LLCoroutine compileVariableDeclarationStatement(std::shared_ptr<AltaCore::AST::VariableDeclarationStatement> node, std::shared_ptr<AltaCore::DH::VariableDeclarationStatement> info);
		LLCoroutine compileDeleteStatement(std::shared_ptr<AltaCore::AST::DeleteStatement> node, std::shared_ptr<AltaCore::DH::DeleteStatement> info);
		LLCoroutine compileControlDirective(std::shared_ptr<AltaCore::AST::ControlDirective> node, std::shared_ptr<AltaCore::DH::Node> info);
		LLCoroutine compileTryCatchBlock(std::shared_ptr<AltaCore::AST::TryCatchBlock> node, std::shared_ptr<AltaCore::DH::TryCatchBlock> info);
		LLCoroutine compileThrowStatement(std::shared_ptr<AltaCore::AST::ThrowStatement> node, std::shared_ptr<AltaCore::DH::ThrowStatement> info);
		LLCoroutine compileNullptrExpression(std::shared_ptr<AltaCore::AST::NullptrExpression> node, std::shared_ptr<AltaCore::DH::NullptrExpression> info);
		LLCoroutine compileCodeLiteralNode(std::shared_ptr<AltaCore::AST::CodeLiteralNode> node, std::shared_ptr<AltaCore::DH::CodeLiteralNode> info);
		LLCoroutine compileBitfieldDefinitionNode(std::shared_ptr<AltaCore::AST::BitfieldDefinitionNode> node, std::shared_ptr<AltaCore::DH::BitfieldDefinitionNode> info);
		LLCoroutine compileLambdaExpression(std::shared_ptr<AltaCore::AST::LambdaExpression> node, std::shared_ptr<AltaCore::DH::LambdaExpression> info);
		LLCoroutine compileSpecialFetchExpression(std::shared_ptr<AltaCore::AST::SpecialFetchExpression> node, std::shared_ptr<AltaCore::DH::SpecialFetchExpression> info);
		LLCoroutine compileClassOperatorDefinitionStatement(std::shared_ptr<AltaCore::AST::ClassOperatorDefinitionStatement> node, std::shared_ptr<AltaCore::DH::ClassOperatorDefinitionStatement> info);
		LLCoroutine compileEnumerationDefinitionNode(std::shared_ptr<AltaCore::AST::EnumerationDefinitionNode> node, std::shared_ptr<AltaCore::DH::EnumerationDefinitionNode> info);
		LLCoroutine compileYieldExpression(std::shared_ptr<AltaCore::AST::YieldExpression> node, std::shared_ptr<AltaCore::DH::YieldExpression> info);
		LLCoroutine compileAssertionStatement(std::shared_ptr<AltaCore::AST::AssertionStatement> node, std::shared_ptr<AltaCore::DH::AssertionStatement> info);
		LLCoroutine compileAwaitExpression(std::shared_ptr<AltaCore::AST::AwaitExpression> node, std::shared_ptr<AltaCore::DH::AwaitExpression> info);
		LLCoroutine compileVoidExpression(std::shared_ptr<AltaCore::AST::VoidExpression> node, std::shared_ptr<AltaCore::DH::VoidExpression> info);

	public:
		Compiler(LLContext llcontext, LLModule llmod, LLTargetMachine targetMachine, LLTargetData targetData):
			_llcontext(llcontext),
			_llmod(llmod),
			_targetMachine(targetMachine),
			_targetData(targetData)
		{
			_debugBuilder = llwrap(LLVMCreateDIBuilder(_llmod.get()));
			_pointerBits = LLVMPointerSize(_targetData.get()) * 8;

			_unknownDebugFile = LLVMDIBuilderCreateFile(_debugBuilder.get(), "<unknown>", 9, "", 0);
			_unknownDebugUnit = LLVMDIBuilderCreateCompileUnit(_debugBuilder.get(), LLVMDWARFSourceLanguageC, _unknownDebugFile, ALTA_COMPILER_NAME, sizeof(ALTA_COMPILER_NAME), /* TODO */ false, "", 0, 0x01000000, "", 0, LLVMDWARFEmissionFull, 0, true, false, "", 0, "", 0);
			_unknownRootDebugScope = _unknownDebugFile;
		};

		void compile(std::shared_ptr<AltaCore::AST::RootNode> root);
		void finalize();
	};
};

#endif // ALTALL_COMPILER_HPP
