#ifndef ALTALL_COMPILER_HPP
#define ALTALL_COMPILER_HPP

#include <altall/util.hpp>
#include <altacore.hpp>

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
		LLModule _llmod;

		// adapted from and based on https://itnext.io/c-20-practical-coroutines-79202872ebba
		struct Coroutine {
			struct Promise;

			using CoroutineHandle = ALTALL_COROUTINE_NS::coroutine_handle<Promise>;

			struct Promise {
				LLVMValueRef value = NULL;
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
				void return_value(LLVMValueRef returnValue) {
					value = returnValue;
				};
			};

			using promise_type = Promise;

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

			LLVMValueRef await_resume() {
				return coroutine.promise().value;
			};
		};

		Coroutine returnNull();

		// not actually a coroutine, just picks the appropriate compiler method and invokes it
		Coroutine compileNode(std::shared_ptr<AltaCore::AST::Node> node);

		Coroutine compileExpressionStatement(std::shared_ptr<AltaCore::AST::ExpressionStatement> node);
		Coroutine compileType(std::shared_ptr<AltaCore::AST::Type> node);
		Coroutine compileParameter(std::shared_ptr<AltaCore::AST::Parameter> node);
		Coroutine compileBlockNode(std::shared_ptr<AltaCore::AST::BlockNode> node);
		Coroutine compileFunctionDefinitionNode(std::shared_ptr<AltaCore::AST::FunctionDefinitionNode> node);
		Coroutine compileReturnDirectiveNode(std::shared_ptr<AltaCore::AST::ReturnDirectiveNode> node);
		Coroutine compileIntegerLiteralNode(std::shared_ptr<AltaCore::AST::IntegerLiteralNode> node);
		Coroutine compileVariableDefinitionExpression(std::shared_ptr<AltaCore::AST::VariableDefinitionExpression> node);
		Coroutine compileAccessor(std::shared_ptr<AltaCore::AST::Accessor> node);
		Coroutine compileFetch(std::shared_ptr<AltaCore::AST::Fetch> node);
		Coroutine compileAssignmentExpression(std::shared_ptr<AltaCore::AST::AssignmentExpression> node);
		Coroutine compileBooleanLiteralNode(std::shared_ptr<AltaCore::AST::BooleanLiteralNode> node);
		Coroutine compileBinaryOperation(std::shared_ptr<AltaCore::AST::BinaryOperation> node);
		Coroutine compileImportStatement(std::shared_ptr<AltaCore::AST::ImportStatement> node);
		Coroutine compileFunctionCallExpression(std::shared_ptr<AltaCore::AST::FunctionCallExpression> node);
		Coroutine compileStringLiteralNode(std::shared_ptr<AltaCore::AST::StringLiteralNode> node);
		Coroutine compileFunctionDeclarationNode(std::shared_ptr<AltaCore::AST::FunctionDeclarationNode> node);
		Coroutine compileAttributeNode(std::shared_ptr<AltaCore::AST::AttributeNode> node);
		Coroutine compileLiteralNode(std::shared_ptr<AltaCore::AST::LiteralNode> node);
		Coroutine compileAttributeStatement(std::shared_ptr<AltaCore::AST::AttributeStatement> node);
		Coroutine compileConditionalStatement(std::shared_ptr<AltaCore::AST::ConditionalStatement> node);
		Coroutine compileConditionalExpression(std::shared_ptr<AltaCore::AST::ConditionalExpression> node);
		Coroutine compileClassDefinitionNode(std::shared_ptr<AltaCore::AST::ClassDefinitionNode> node);
		Coroutine compileClassStatementNode(std::shared_ptr<AltaCore::AST::ClassStatementNode> node);
		Coroutine compileClassMemberDefinitionStatement(std::shared_ptr<AltaCore::AST::ClassMemberDefinitionStatement> node);
		Coroutine compileClassMethodDefinitionStatement(std::shared_ptr<AltaCore::AST::ClassMethodDefinitionStatement> node);
		Coroutine compileClassSpecialMethodDefinitionStatement(std::shared_ptr<AltaCore::AST::ClassSpecialMethodDefinitionStatement> node);
		Coroutine compileClassInstantiationExpression(std::shared_ptr<AltaCore::AST::ClassInstantiationExpression> node);
		Coroutine compilePointerExpression(std::shared_ptr<AltaCore::AST::PointerExpression> node);
		Coroutine compileDereferenceExpression(std::shared_ptr<AltaCore::AST::DereferenceExpression> node);
		Coroutine compileWhileLoopStatement(std::shared_ptr<AltaCore::AST::WhileLoopStatement> node);
		Coroutine compileCastExpression(std::shared_ptr<AltaCore::AST::CastExpression> node);
		Coroutine compileClassReadAccessorDefinitionStatement(std::shared_ptr<AltaCore::AST::ClassReadAccessorDefinitionStatement> node);
		Coroutine compileCharacterLiteralNode(std::shared_ptr<AltaCore::AST::CharacterLiteralNode> node);
		Coroutine compileTypeAliasStatement(std::shared_ptr<AltaCore::AST::TypeAliasStatement> node);
		Coroutine compileSubscriptExpression(std::shared_ptr<AltaCore::AST::SubscriptExpression> node);
		Coroutine compileRetrievalNode(std::shared_ptr<AltaCore::AST::RetrievalNode> node);
		Coroutine compileSuperClassFetch(std::shared_ptr<AltaCore::AST::SuperClassFetch> node);
		Coroutine compileInstanceofExpression(std::shared_ptr<AltaCore::AST::InstanceofExpression> node);
		Coroutine compileGeneric(std::shared_ptr<AltaCore::AST::Generic> node);
		Coroutine compileForLoopStatement(std::shared_ptr<AltaCore::AST::ForLoopStatement> node);
		Coroutine compileRangedForLoopStatement(std::shared_ptr<AltaCore::AST::RangedForLoopStatement> node);
		Coroutine compileUnaryOperation(std::shared_ptr<AltaCore::AST::UnaryOperation> node);
		Coroutine compileSizeofOperation(std::shared_ptr<AltaCore::AST::SizeofOperation> node);
		Coroutine compileFloatingPointLiteralNode(std::shared_ptr<AltaCore::AST::FloatingPointLiteralNode> node);
		Coroutine compileStructureDefinitionStatement(std::shared_ptr<AltaCore::AST::StructureDefinitionStatement> node);
		Coroutine compileExportStatement(std::shared_ptr<AltaCore::AST::ExportStatement> node);
		Coroutine compileVariableDeclarationStatement(std::shared_ptr<AltaCore::AST::VariableDeclarationStatement> node);
		Coroutine compileAliasStatement(std::shared_ptr<AltaCore::AST::AliasStatement> node);
		Coroutine compileDeleteStatement(std::shared_ptr<AltaCore::AST::DeleteStatement> node);
		Coroutine compileControlDirective(std::shared_ptr<AltaCore::AST::ControlDirective> node);
		Coroutine compileTryCatchBlock(std::shared_ptr<AltaCore::AST::TryCatchBlock> node);
		Coroutine compileThrowStatement(std::shared_ptr<AltaCore::AST::ThrowStatement> node);
		Coroutine compileNullptrExpression(std::shared_ptr<AltaCore::AST::NullptrExpression> node);
		Coroutine compileCodeLiteralNode(std::shared_ptr<AltaCore::AST::CodeLiteralNode> node);
		Coroutine compileBitfieldDefinitionNode(std::shared_ptr<AltaCore::AST::BitfieldDefinitionNode> node);
		Coroutine compileLambdaExpression(std::shared_ptr<AltaCore::AST::LambdaExpression> node);
		Coroutine compileSpecialFetchExpression(std::shared_ptr<AltaCore::AST::SpecialFetchExpression> node);
		Coroutine compileClassOperatorDefinitionStatement(std::shared_ptr<AltaCore::AST::ClassOperatorDefinitionStatement> node);
		Coroutine compileEnumerationDefinitionNode(std::shared_ptr<AltaCore::AST::EnumerationDefinitionNode> node);
		Coroutine compileYieldExpression(std::shared_ptr<AltaCore::AST::YieldExpression> node);
		Coroutine compileAssertionStatement(std::shared_ptr<AltaCore::AST::AssertionStatement> node);
		Coroutine compileAwaitExpression(std::shared_ptr<AltaCore::AST::AwaitExpression> node);

	public:
		Compiler(LLModule llmod):
			_llmod(llmod)
			{};

		void compile(std::shared_ptr<AltaCore::AST::RootNode> root);
	};
};

#endif // ALTALL_COMPILER_HPP
