#include <altall/compiler.hpp>

// DEBUGGING
#include <iostream>

AltaLL::Compiler::Coroutine AltaLL::Compiler::returnNull() {
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileNode(std::shared_ptr<AltaCore::AST::Node> node) {
	switch (node->nodeType()) {
		case AltaCore::AST::NodeType::ExpressionStatement: return compileExpressionStatement(std::dynamic_pointer_cast<AltaCore::AST::ExpressionStatement>(node));
		case AltaCore::AST::NodeType::Type: return compileType(std::dynamic_pointer_cast<AltaCore::AST::Type>(node));
		case AltaCore::AST::NodeType::Parameter: return compileParameter(std::dynamic_pointer_cast<AltaCore::AST::Parameter>(node));
		case AltaCore::AST::NodeType::BlockNode: return compileBlockNode(std::dynamic_pointer_cast<AltaCore::AST::BlockNode>(node));
		case AltaCore::AST::NodeType::FunctionDefinitionNode: return compileFunctionDefinitionNode(std::dynamic_pointer_cast<AltaCore::AST::FunctionDefinitionNode>(node));
		case AltaCore::AST::NodeType::ReturnDirectiveNode: return compileReturnDirectiveNode(std::dynamic_pointer_cast<AltaCore::AST::ReturnDirectiveNode>(node));
		case AltaCore::AST::NodeType::IntegerLiteralNode: return compileIntegerLiteralNode(std::dynamic_pointer_cast<AltaCore::AST::IntegerLiteralNode>(node));
		case AltaCore::AST::NodeType::VariableDefinitionExpression: return compileVariableDefinitionExpression(std::dynamic_pointer_cast<AltaCore::AST::VariableDefinitionExpression>(node));
		case AltaCore::AST::NodeType::Accessor: return compileAccessor(std::dynamic_pointer_cast<AltaCore::AST::Accessor>(node));
		case AltaCore::AST::NodeType::Fetch: return compileFetch(std::dynamic_pointer_cast<AltaCore::AST::Fetch>(node));
		case AltaCore::AST::NodeType::AssignmentExpression: return compileAssignmentExpression(std::dynamic_pointer_cast<AltaCore::AST::AssignmentExpression>(node));
		case AltaCore::AST::NodeType::BooleanLiteralNode: return compileBooleanLiteralNode(std::dynamic_pointer_cast<AltaCore::AST::BooleanLiteralNode>(node));
		case AltaCore::AST::NodeType::BinaryOperation: return compileBinaryOperation(std::dynamic_pointer_cast<AltaCore::AST::BinaryOperation>(node));
		case AltaCore::AST::NodeType::ImportStatement: return compileImportStatement(std::dynamic_pointer_cast<AltaCore::AST::ImportStatement>(node));
		case AltaCore::AST::NodeType::FunctionCallExpression: return compileFunctionCallExpression(std::dynamic_pointer_cast<AltaCore::AST::FunctionCallExpression>(node));
		case AltaCore::AST::NodeType::StringLiteralNode: return compileStringLiteralNode(std::dynamic_pointer_cast<AltaCore::AST::StringLiteralNode>(node));
		case AltaCore::AST::NodeType::FunctionDeclarationNode: return compileFunctionDeclarationNode(std::dynamic_pointer_cast<AltaCore::AST::FunctionDeclarationNode>(node));
		case AltaCore::AST::NodeType::AttributeNode: return compileAttributeNode(std::dynamic_pointer_cast<AltaCore::AST::AttributeNode>(node));
		case AltaCore::AST::NodeType::LiteralNode: return compileLiteralNode(std::dynamic_pointer_cast<AltaCore::AST::LiteralNode>(node));
		case AltaCore::AST::NodeType::AttributeStatement: return compileAttributeStatement(std::dynamic_pointer_cast<AltaCore::AST::AttributeStatement>(node));
		case AltaCore::AST::NodeType::ConditionalStatement: return compileConditionalStatement(std::dynamic_pointer_cast<AltaCore::AST::ConditionalStatement>(node));
		case AltaCore::AST::NodeType::ConditionalExpression: return compileConditionalExpression(std::dynamic_pointer_cast<AltaCore::AST::ConditionalExpression>(node));
		case AltaCore::AST::NodeType::ClassDefinitionNode: return compileClassDefinitionNode(std::dynamic_pointer_cast<AltaCore::AST::ClassDefinitionNode>(node));
		case AltaCore::AST::NodeType::ClassStatementNode: return compileClassStatementNode(std::dynamic_pointer_cast<AltaCore::AST::ClassStatementNode>(node));
		case AltaCore::AST::NodeType::ClassMemberDefinitionStatement: return compileClassMemberDefinitionStatement(std::dynamic_pointer_cast<AltaCore::AST::ClassMemberDefinitionStatement>(node));
		case AltaCore::AST::NodeType::ClassMethodDefinitionStatement: return compileClassMethodDefinitionStatement(std::dynamic_pointer_cast<AltaCore::AST::ClassMethodDefinitionStatement>(node));
		case AltaCore::AST::NodeType::ClassSpecialMethodDefinitionStatement: return compileClassSpecialMethodDefinitionStatement(std::dynamic_pointer_cast<AltaCore::AST::ClassSpecialMethodDefinitionStatement>(node));
		case AltaCore::AST::NodeType::ClassInstantiationExpression: return compileClassInstantiationExpression(std::dynamic_pointer_cast<AltaCore::AST::ClassInstantiationExpression>(node));
		case AltaCore::AST::NodeType::PointerExpression: return compilePointerExpression(std::dynamic_pointer_cast<AltaCore::AST::PointerExpression>(node));
		case AltaCore::AST::NodeType::DereferenceExpression: return compileDereferenceExpression(std::dynamic_pointer_cast<AltaCore::AST::DereferenceExpression>(node));
		case AltaCore::AST::NodeType::WhileLoopStatement: return compileWhileLoopStatement(std::dynamic_pointer_cast<AltaCore::AST::WhileLoopStatement>(node));
		case AltaCore::AST::NodeType::CastExpression: return compileCastExpression(std::dynamic_pointer_cast<AltaCore::AST::CastExpression>(node));
		case AltaCore::AST::NodeType::ClassReadAccessorDefinitionStatement: return compileClassReadAccessorDefinitionStatement(std::dynamic_pointer_cast<AltaCore::AST::ClassReadAccessorDefinitionStatement>(node));
		case AltaCore::AST::NodeType::CharacterLiteralNode: return compileCharacterLiteralNode(std::dynamic_pointer_cast<AltaCore::AST::CharacterLiteralNode>(node));
		case AltaCore::AST::NodeType::TypeAliasStatement: return compileTypeAliasStatement(std::dynamic_pointer_cast<AltaCore::AST::TypeAliasStatement>(node));
		case AltaCore::AST::NodeType::SubscriptExpression: return compileSubscriptExpression(std::dynamic_pointer_cast<AltaCore::AST::SubscriptExpression>(node));
		case AltaCore::AST::NodeType::RetrievalNode: return compileRetrievalNode(std::dynamic_pointer_cast<AltaCore::AST::RetrievalNode>(node));
		case AltaCore::AST::NodeType::SuperClassFetch: return compileSuperClassFetch(std::dynamic_pointer_cast<AltaCore::AST::SuperClassFetch>(node));
		case AltaCore::AST::NodeType::InstanceofExpression: return compileInstanceofExpression(std::dynamic_pointer_cast<AltaCore::AST::InstanceofExpression>(node));
		case AltaCore::AST::NodeType::Generic: return compileGeneric(std::dynamic_pointer_cast<AltaCore::AST::Generic>(node));
		case AltaCore::AST::NodeType::ForLoopStatement: return compileForLoopStatement(std::dynamic_pointer_cast<AltaCore::AST::ForLoopStatement>(node));
		case AltaCore::AST::NodeType::RangedForLoopStatement: return compileRangedForLoopStatement(std::dynamic_pointer_cast<AltaCore::AST::RangedForLoopStatement>(node));
		case AltaCore::AST::NodeType::UnaryOperation: return compileUnaryOperation(std::dynamic_pointer_cast<AltaCore::AST::UnaryOperation>(node));
		case AltaCore::AST::NodeType::SizeofOperation: return compileSizeofOperation(std::dynamic_pointer_cast<AltaCore::AST::SizeofOperation>(node));
		case AltaCore::AST::NodeType::FloatingPointLiteralNode: return compileFloatingPointLiteralNode(std::dynamic_pointer_cast<AltaCore::AST::FloatingPointLiteralNode>(node));
		case AltaCore::AST::NodeType::StructureDefinitionStatement: return compileStructureDefinitionStatement(std::dynamic_pointer_cast<AltaCore::AST::StructureDefinitionStatement>(node));
		case AltaCore::AST::NodeType::ExportStatement: return compileExportStatement(std::dynamic_pointer_cast<AltaCore::AST::ExportStatement>(node));
		case AltaCore::AST::NodeType::VariableDeclarationStatement: return compileVariableDeclarationStatement(std::dynamic_pointer_cast<AltaCore::AST::VariableDeclarationStatement>(node));
		case AltaCore::AST::NodeType::AliasStatement: return compileAliasStatement(std::dynamic_pointer_cast<AltaCore::AST::AliasStatement>(node));
		case AltaCore::AST::NodeType::DeleteStatement: return compileDeleteStatement(std::dynamic_pointer_cast<AltaCore::AST::DeleteStatement>(node));
		case AltaCore::AST::NodeType::ControlDirective: return compileControlDirective(std::dynamic_pointer_cast<AltaCore::AST::ControlDirective>(node));
		case AltaCore::AST::NodeType::TryCatchBlock: return compileTryCatchBlock(std::dynamic_pointer_cast<AltaCore::AST::TryCatchBlock>(node));
		case AltaCore::AST::NodeType::ThrowStatement: return compileThrowStatement(std::dynamic_pointer_cast<AltaCore::AST::ThrowStatement>(node));
		case AltaCore::AST::NodeType::NullptrExpression: return compileNullptrExpression(std::dynamic_pointer_cast<AltaCore::AST::NullptrExpression>(node));
		case AltaCore::AST::NodeType::CodeLiteralNode: return compileCodeLiteralNode(std::dynamic_pointer_cast<AltaCore::AST::CodeLiteralNode>(node));
		case AltaCore::AST::NodeType::BitfieldDefinitionNode: return compileBitfieldDefinitionNode(std::dynamic_pointer_cast<AltaCore::AST::BitfieldDefinitionNode>(node));
		case AltaCore::AST::NodeType::LambdaExpression: return compileLambdaExpression(std::dynamic_pointer_cast<AltaCore::AST::LambdaExpression>(node));
		case AltaCore::AST::NodeType::SpecialFetchExpression: return compileSpecialFetchExpression(std::dynamic_pointer_cast<AltaCore::AST::SpecialFetchExpression>(node));
		case AltaCore::AST::NodeType::ClassOperatorDefinitionStatement: return compileClassOperatorDefinitionStatement(std::dynamic_pointer_cast<AltaCore::AST::ClassOperatorDefinitionStatement>(node));
		case AltaCore::AST::NodeType::EnumerationDefinitionNode: return compileEnumerationDefinitionNode(std::dynamic_pointer_cast<AltaCore::AST::EnumerationDefinitionNode>(node));
		case AltaCore::AST::NodeType::YieldExpression: return compileYieldExpression(std::dynamic_pointer_cast<AltaCore::AST::YieldExpression>(node));
		case AltaCore::AST::NodeType::AssertionStatement: return compileAssertionStatement(std::dynamic_pointer_cast<AltaCore::AST::AssertionStatement>(node));
		case AltaCore::AST::NodeType::AwaitExpression: return compileAwaitExpression(std::dynamic_pointer_cast<AltaCore::AST::AwaitExpression>(node));
		default: return returnNull();
	}
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileExpressionStatement(std::shared_ptr<AltaCore::AST::ExpressionStatement> node) {
	// TODO
	std::cout << "TODO: ExpressionStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileType(std::shared_ptr<AltaCore::AST::Type> node) {
	// TODO
	std::cout << "TODO: Type" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileParameter(std::shared_ptr<AltaCore::AST::Parameter> node) {
	// TODO
	std::cout << "TODO: Parameter" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileBlockNode(std::shared_ptr<AltaCore::AST::BlockNode> node) {
	// TODO
	std::cout << "TODO: BlockNode" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileFunctionDefinitionNode(std::shared_ptr<AltaCore::AST::FunctionDefinitionNode> node) {
	// TODO
	std::cout << "TODO: FunctionDefinitionNode" << std::endl;

	// just for testing:
	auto ret = co_await compileNode(node->body);

	std::cout << "compiling the body returned " << ret << std::endl;

	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileReturnDirectiveNode(std::shared_ptr<AltaCore::AST::ReturnDirectiveNode> node) {
	// TODO
	std::cout << "TODO: ReturnDirectiveNode" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileIntegerLiteralNode(std::shared_ptr<AltaCore::AST::IntegerLiteralNode> node) {
	// TODO
	std::cout << "TODO: IntegerLiteralNode" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileVariableDefinitionExpression(std::shared_ptr<AltaCore::AST::VariableDefinitionExpression> node) {
	// TODO
	std::cout << "TODO: VariableDefinitionExpression" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileAccessor(std::shared_ptr<AltaCore::AST::Accessor> node) {
	// TODO
	std::cout << "TODO: Accessor" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileFetch(std::shared_ptr<AltaCore::AST::Fetch> node) {
	// TODO
	std::cout << "TODO: Fetch" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileAssignmentExpression(std::shared_ptr<AltaCore::AST::AssignmentExpression> node) {
	// TODO
	std::cout << "TODO: AssignmentExpression" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileBooleanLiteralNode(std::shared_ptr<AltaCore::AST::BooleanLiteralNode> node) {
	// TODO
	std::cout << "TODO: BooleanLiteralNode" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileBinaryOperation(std::shared_ptr<AltaCore::AST::BinaryOperation> node) {
	// TODO
	std::cout << "TODO: BinaryOperation" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileImportStatement(std::shared_ptr<AltaCore::AST::ImportStatement> node) {
	// TODO
	std::cout << "TODO: ImportStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileFunctionCallExpression(std::shared_ptr<AltaCore::AST::FunctionCallExpression> node) {
	// TODO
	std::cout << "TODO: FunctionCallExpression" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileStringLiteralNode(std::shared_ptr<AltaCore::AST::StringLiteralNode> node) {
	// TODO
	std::cout << "TODO: StringLiteralNode" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileFunctionDeclarationNode(std::shared_ptr<AltaCore::AST::FunctionDeclarationNode> node) {
	// TODO
	std::cout << "TODO: FunctionDeclarationNode" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileAttributeNode(std::shared_ptr<AltaCore::AST::AttributeNode> node) {
	// TODO
	std::cout << "TODO: AttributeNode" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileLiteralNode(std::shared_ptr<AltaCore::AST::LiteralNode> node) {
	// TODO
	std::cout << "TODO: LiteralNode" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileAttributeStatement(std::shared_ptr<AltaCore::AST::AttributeStatement> node) {
	// TODO
	std::cout << "TODO: AttributeStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileConditionalStatement(std::shared_ptr<AltaCore::AST::ConditionalStatement> node) {
	// TODO
	std::cout << "TODO: ConditionalStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileConditionalExpression(std::shared_ptr<AltaCore::AST::ConditionalExpression> node) {
	// TODO
	std::cout << "TODO: ConditionalExpression" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileClassDefinitionNode(std::shared_ptr<AltaCore::AST::ClassDefinitionNode> node) {
	// TODO
	std::cout << "TODO: ClassDefinitionNode" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileClassStatementNode(std::shared_ptr<AltaCore::AST::ClassStatementNode> node) {
	// TODO
	std::cout << "TODO: ClassStatementNode" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileClassMemberDefinitionStatement(std::shared_ptr<AltaCore::AST::ClassMemberDefinitionStatement> node) {
	// TODO
	std::cout << "TODO: ClassMemberDefinitionStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileClassMethodDefinitionStatement(std::shared_ptr<AltaCore::AST::ClassMethodDefinitionStatement> node) {
	// TODO
	std::cout << "TODO: ClassMethodDefinitionStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileClassSpecialMethodDefinitionStatement(std::shared_ptr<AltaCore::AST::ClassSpecialMethodDefinitionStatement> node) {
	// TODO
	std::cout << "TODO: ClassSpecialMethodDefinitionStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileClassInstantiationExpression(std::shared_ptr<AltaCore::AST::ClassInstantiationExpression> node) {
	// TODO
	std::cout << "TODO: ClassInstantiationExpression" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compilePointerExpression(std::shared_ptr<AltaCore::AST::PointerExpression> node) {
	// TODO
	std::cout << "TODO: PointerExpression" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileDereferenceExpression(std::shared_ptr<AltaCore::AST::DereferenceExpression> node) {
	// TODO
	std::cout << "TODO: DereferenceExpression" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileWhileLoopStatement(std::shared_ptr<AltaCore::AST::WhileLoopStatement> node) {
	// TODO
	std::cout << "TODO: WhileLoopStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileCastExpression(std::shared_ptr<AltaCore::AST::CastExpression> node) {
	// TODO
	std::cout << "TODO: CastExpression" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileClassReadAccessorDefinitionStatement(std::shared_ptr<AltaCore::AST::ClassReadAccessorDefinitionStatement> node) {
	// TODO
	std::cout << "TODO: ClassReadAccessorDefinitionStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileCharacterLiteralNode(std::shared_ptr<AltaCore::AST::CharacterLiteralNode> node) {
	// TODO
	std::cout << "TODO: CharacterLiteralNode" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileTypeAliasStatement(std::shared_ptr<AltaCore::AST::TypeAliasStatement> node) {
	// TODO
	std::cout << "TODO: TypeAliasStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileSubscriptExpression(std::shared_ptr<AltaCore::AST::SubscriptExpression> node) {
	// TODO
	std::cout << "TODO: SubscriptExpression" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileRetrievalNode(std::shared_ptr<AltaCore::AST::RetrievalNode> node) {
	// TODO
	std::cout << "TODO: RetrievalNode" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileSuperClassFetch(std::shared_ptr<AltaCore::AST::SuperClassFetch> node) {
	// TODO
	std::cout << "TODO: SuperClassFetch" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileInstanceofExpression(std::shared_ptr<AltaCore::AST::InstanceofExpression> node) {
	// TODO
	std::cout << "TODO: InstanceofExpression" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileGeneric(std::shared_ptr<AltaCore::AST::Generic> node) {
	// TODO
	std::cout << "TODO: Generic" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileForLoopStatement(std::shared_ptr<AltaCore::AST::ForLoopStatement> node) {
	// TODO
	std::cout << "TODO: ForLoopStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileRangedForLoopStatement(std::shared_ptr<AltaCore::AST::RangedForLoopStatement> node) {
	// TODO
	std::cout << "TODO: RangedForLoopStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileUnaryOperation(std::shared_ptr<AltaCore::AST::UnaryOperation> node) {
	// TODO
	std::cout << "TODO: UnaryOperation" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileSizeofOperation(std::shared_ptr<AltaCore::AST::SizeofOperation> node) {
	// TODO
	std::cout << "TODO: SizeofOperation" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileFloatingPointLiteralNode(std::shared_ptr<AltaCore::AST::FloatingPointLiteralNode> node) {
	// TODO
	std::cout << "TODO: FloatingPointLiteralNode" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileStructureDefinitionStatement(std::shared_ptr<AltaCore::AST::StructureDefinitionStatement> node) {
	// TODO
	std::cout << "TODO: StructureDefinitionStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileExportStatement(std::shared_ptr<AltaCore::AST::ExportStatement> node) {
	// TODO
	std::cout << "TODO: ExportStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileVariableDeclarationStatement(std::shared_ptr<AltaCore::AST::VariableDeclarationStatement> node) {
	// TODO
	std::cout << "TODO: VariableDeclarationStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileAliasStatement(std::shared_ptr<AltaCore::AST::AliasStatement> node) {
	// TODO
	std::cout << "TODO: AliasStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileDeleteStatement(std::shared_ptr<AltaCore::AST::DeleteStatement> node) {
	// TODO
	std::cout << "TODO: DeleteStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileControlDirective(std::shared_ptr<AltaCore::AST::ControlDirective> node) {
	// TODO
	std::cout << "TODO: ControlDirective" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileTryCatchBlock(std::shared_ptr<AltaCore::AST::TryCatchBlock> node) {
	// TODO
	std::cout << "TODO: TryCatchBlock" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileThrowStatement(std::shared_ptr<AltaCore::AST::ThrowStatement> node) {
	// TODO
	std::cout << "TODO: ThrowStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileNullptrExpression(std::shared_ptr<AltaCore::AST::NullptrExpression> node) {
	// TODO
	std::cout << "TODO: NullptrExpression" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileCodeLiteralNode(std::shared_ptr<AltaCore::AST::CodeLiteralNode> node) {
	// TODO
	std::cout << "TODO: CodeLiteralNode" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileBitfieldDefinitionNode(std::shared_ptr<AltaCore::AST::BitfieldDefinitionNode> node) {
	// TODO
	std::cout << "TODO: BitfieldDefinitionNode" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileLambdaExpression(std::shared_ptr<AltaCore::AST::LambdaExpression> node) {
	// TODO
	std::cout << "TODO: LambdaExpression" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileSpecialFetchExpression(std::shared_ptr<AltaCore::AST::SpecialFetchExpression> node) {
	// TODO
	std::cout << "TODO: SpecialFetchExpression" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileClassOperatorDefinitionStatement(std::shared_ptr<AltaCore::AST::ClassOperatorDefinitionStatement> node) {
	// TODO
	std::cout << "TODO: ClassOperatorDefinitionStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileEnumerationDefinitionNode(std::shared_ptr<AltaCore::AST::EnumerationDefinitionNode> node) {
	// TODO
	std::cout << "TODO: EnumerationDefinitionNode" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileYieldExpression(std::shared_ptr<AltaCore::AST::YieldExpression> node) {
	// TODO
	std::cout << "TODO: YieldExpression" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileAssertionStatement(std::shared_ptr<AltaCore::AST::AssertionStatement> node) {
	// TODO
	std::cout << "TODO: AssertionStatement" << std::endl;
	co_return NULL;
};

AltaLL::Compiler::Coroutine AltaLL::Compiler::compileAwaitExpression(std::shared_ptr<AltaCore::AST::AwaitExpression> node) {
	// TODO
	std::cout << "TODO: AwaitExpression" << std::endl;
	co_return NULL;
};

void AltaLL::Compiler::compile(std::shared_ptr<AltaCore::AST::RootNode> root) {
	for (auto& node: root->statements) {
		auto co = compileNode(node);
		co.coroutine.resume();
	}
};
