#ifndef ALTALL_COMPILER_HPP
#define ALTALL_COMPILER_HPP

#include <altall/util.hpp>
#include <altacore.hpp>
#include <altall/altall.hpp>

#if __has_include(<coroutine>)
	#include <coroutine>
	#define ALTALL_COROUTINE_NS std
#else
	#include <experimental/coroutine>
	#define ALTALL_COROUTINE_NS std::experimental
#endif

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
				ALTALL_COROUTINE_NS::coroutine_handle<> continuation = ALTALL_COROUTINE_NS::noop_coroutine();

				struct FinalContinuation {
					constexpr bool await_ready() const noexcept {
						return false;
					};
					ALTALL_COROUTINE_NS::coroutine_handle<> await_suspend(HandleWithContinuation auto caller) const noexcept {
						return caller.promise().continuation;
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
				void return_value(PromiseResult returnValue) {
					value = returnValue;
				};
			};

			// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=85282
			template<std::same_as<void> T>
			struct Promise<T> {
				ALTALL_COROUTINE_NS::coroutine_handle<> continuation = ALTALL_COROUTINE_NS::noop_coroutine();

				struct FinalContinuation {
					constexpr bool await_ready() const noexcept {
						return false;
					};
					ALTALL_COROUTINE_NS::coroutine_handle<> await_suspend(HandleWithContinuation auto caller) const noexcept {
						return caller.promise().continuation;
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

			ALTALL_COROUTINE_NS::coroutine_handle<> await_suspend(ALTALL_COROUTINE_NS::coroutine_handle<> caller) {
				coroutine.promise().continuation = caller;
				return coroutine;
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
			};

			std::vector<ScopeItem> items;
			std::stack<size_t> sizesDuringBranching;
			Compiler& compiler;
			Type type;

			ScopeStack(ScopeStack&& other):
				items(std::move(other.items)),
				sizesDuringBranching(std::move(other.sizesDuringBranching)),
				compiler(other.compiler),
				type(std::move(other.type))
				{};

			ScopeStack(Compiler& _compiler, Type _type):
				compiler(_compiler),
				type(_type)
				{};

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

			void beginBranch() {
				sizesDuringBranching.push(items.size());
			};

			Coroutine<void> endBranch(LLVMBasicBlockRef mergeBlock, std::vector<LLVMBasicBlockRef> mergingBlocks);
			Coroutine<void> cleanup();
		};

		LLContext _llcontext;
		LLModule _llmod;
		LLTargetMachine _targetMachine;
		LLTargetData _targetData;
		std::stack<LLBuilder> _builders;
		ALTACORE_MAP<std::string, LLVMTypeRef> _definedTypes;
		bool _inGenerator = false;
		ALTACORE_MAP<std::string, LLVMValueRef> _definedFunctions;
		std::deque<ScopeStack> _stacks;
		ALTACORE_MAP<std::string, LLVMValueRef> _definedVariables;
		LLVMValueRef _initFunction = nullptr;
		ALTACORE_OPTIONAL<ScopeStack> _initFunctionScopeStack = ALTACORE_NULLOPT;
		LLBuilder _initFunctionBuilder;
		std::stack<size_t> temporaryIndices;

		inline ScopeStack& currentStack() {
			return _stacks.back();
		};

		inline void pushStack(ScopeStack::Type type) {
			if (type == ScopeStack::Type::Function) {
				temporaryIndices.push(0);
			}
			_stacks.emplace_back(*this, type);
		};

		inline void popStack() {
			if (_stacks.back().type == ScopeStack::Type::Function) {
				temporaryIndices.pop();
			}
			_stacks.pop_back();
		};

		inline size_t nextTemp() {
			return temporaryIndices.top()++;
		};

		Coroutine<LLVMTypeRef> translateType(std::shared_ptr<AltaCore::DET::Type> type, bool usePointersToFunctions = true);

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

				canCopy = true;
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

		LLCoroutine loadRef(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType);

		LLCoroutine getRealInstance(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType);
		LLCoroutine getRootInstance(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType);

		LLCoroutine doParentRetrieval(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType, std::shared_ptr<AltaCore::DET::Type> targetType, bool* didRetrieval = nullptr);
		LLCoroutine doChildRetrieval(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> exprType, std::shared_ptr<AltaCore::DET::Type> targetType, bool* didRetrieval = nullptr);

		LLCoroutine tmpify(LLVMValueRef expr, std::shared_ptr<AltaCore::DET::Type> type, bool withStack = true);
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
							exprType->isOptional ||
							exprType->klass->destructor
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

	public:
		Compiler(LLContext llcontext, LLModule llmod, LLTargetMachine targetMachine, LLTargetData targetData):
			_llcontext(llcontext),
			_llmod(llmod),
			_targetMachine(targetMachine),
			_targetData(targetData)
			{};

		void compile(std::shared_ptr<AltaCore::AST::RootNode> root);
		void finalize();
	};
};

#endif // ALTALL_COMPILER_HPP
