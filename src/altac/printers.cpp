#include <altac/printers.hpp>

void CLI::Printers::printAST(AltaCore::AST::Node* node, std::string indent, bool indentName, bool autoPrint) {
  using AltaCore::AST::NodeType;
  using AltaCore::AST::TypeModifierFlag;
  if (autoPrint) {
    printf("%s%s", (indentName) ? indent.c_str() : "", AltaCore::AST::NodeType_names[(size_t)node->nodeType()]);
  }
  switch (node->nodeType()) {
    case NodeType::RootNode: {
      AltaCore::AST::RootNode* root = dynamic_cast<AltaCore::AST::RootNode*>(node);
      printf(" {\n");
      for (auto& statement: root->statements) {
        printAST(statement.get(), indent + "  ");
        printf("\n");
      }
      printf("%s}", indent.c_str());
    } break;
    case NodeType::ExpressionStatement: {
      AltaCore::AST::ExpressionStatement* exprStmt = dynamic_cast<AltaCore::AST::ExpressionStatement*>(node);
      printf("<%s>", AltaCore::AST::NodeType_names[(size_t)exprStmt->expression->nodeType()]);
      printAST(exprStmt->expression.get(), indent, false, false);
    } break;
    case NodeType::FunctionDefinitionNode: {
      AltaCore::AST::FunctionDefinitionNode* funcDef = dynamic_cast<AltaCore::AST::FunctionDefinitionNode*>(node);
      printf(" {\n");
      printf("%s  name = \"%s\"\n", indent.c_str(), funcDef->name.c_str());
      printf("%s  parameters = [", indent.c_str());
      if (funcDef->parameters.size() > 0) {
        printf("\n");
        for (auto& parameter: funcDef->parameters) {
          printAST(parameter.get(), indent + "    ");
          printf(",\n");
        }
        printf("%s  ]\n", indent.c_str());
      } else {
        printf("]\n");
      }
      printf("%s  returnType = ", indent.c_str());
      printAST(funcDef->returnType.get(), indent + "  ", false);
      printf("\n");
      printf("%s  body = ", indent.c_str());
      printAST(funcDef->body.get(), indent + "  ", false);
      printf("\n");
      printf("%s}", indent.c_str());
    } break;
    case NodeType::Parameter: {
      AltaCore::AST::Parameter* param = dynamic_cast<AltaCore::AST::Parameter*>(node);
      printf(" {\n");
      printf("%s  type = ", indent.c_str());
      printAST(param->type.get(), indent + "  ", false);
      printf("\n");
      printf("%s  name = \"%s\"\n", indent.c_str(), param->name.c_str());
      printf("%s}", indent.c_str());
    } break;
    case NodeType::Type: {
      AltaCore::AST::Type* type = dynamic_cast<AltaCore::AST::Type*>(node);
      printf(" {\n");
      printf("%s  name = \"%s\"\n", indent.c_str(), type->name.c_str());
      printf("%s  modifiers = [", indent.c_str());
      if (type->modifiers.size() > 0) {
        printf("\n");
        for (auto& modifier: type->modifiers) {
          printf("%s    ", indent.c_str());
          bool isFirst = true;
          if (modifier & (uint8_t)TypeModifierFlag::Constant) {
            if (isFirst) {
              isFirst = false;
            } else {
              printf(" | ");
            }
            printf("Constant");
          }
          if (modifier & (uint8_t)TypeModifierFlag::Pointer) {
            if (isFirst) {
              isFirst = false;
            } else {
              printf(" | ");
            }
            printf("Pointer");
          }
          printf(",\n");
        }
        printf("%s  ]\n", indent.c_str());
      } else {
        printf("]\n");
      }
      printf("%s}", indent.c_str());
    } break;
    case NodeType::BlockNode: {
      AltaCore::AST::BlockNode* block = dynamic_cast<AltaCore::AST::BlockNode*>(node);
      printf(" {\n");
      for (auto& statement: block->statements) {
        printAST(statement.get(), indent + "  ");
        printf("\n");
      }
      printf("%s}", indent.c_str());
    } break;
    case NodeType::ReturnDirectiveNode: {
      AltaCore::AST::ReturnDirectiveNode* ret = dynamic_cast<AltaCore::AST::ReturnDirectiveNode*>(node);
      printf(" {");
      if (ret->expression != nullptr) {
        printf("\n");
        printAST(ret->expression.get(), indent + "  ");
        printf("\n%s", indent.c_str());
      }
      printf("}");
    } break;
    case NodeType::IntegerLiteralNode: {
      AltaCore::AST::IntegerLiteralNode* intLit = dynamic_cast<AltaCore::AST::IntegerLiteralNode*>(node);
      printf("(\"%s\")", intLit->raw.c_str());
    } break;
    default: {
      printf(" {}");
    } break;
  }
};

void CLI::Printers::printDET(AltaCore::DET::Node* node, std::string indent, bool indentName, bool autoPrint) {
  using AltaCore::DET::NodeType;
  using AltaCore::AST::TypeModifierFlag;
  if (autoPrint) {
    printf("%s%s", (indentName) ? indent.c_str() : "", AltaCore::DET::NodeType_names[(size_t)node->nodeType()]);
  }
  switch (node->nodeType()) {
    case NodeType::Module: {
      AltaCore::DET::Module* mod = dynamic_cast<AltaCore::DET::Module*>(node);
      printf(" {\n");
      printf("%s  name = \"%s\"\n", indent.c_str(), mod->name.c_str());
      printf("%s  scope = ", indent.c_str());
      printDET(mod->scope.get(), indent + "  ", false);
      printf("\n%s}", indent.c_str());
    } break;
    case NodeType::Scope: {
      AltaCore::DET::Scope* scope = dynamic_cast<AltaCore::DET::Scope*>(node);
      printf(" [");
      if (scope->items.size() > 0) {
        printf("\n");
        for (auto& item: scope->items) {
          printDET(item.get(), indent + "  ");
          printf(",\n");
        }
        printf("%s", indent.c_str());
      }
      printf("]");
    } break;
    case NodeType::Function: {
      AltaCore::DET::Function* func = dynamic_cast<AltaCore::DET::Function*>(node);
      printf(" {\n");
      printf("%s  name = \"%s\"\n", indent.c_str(), func->name.c_str());
      printf("%s  parameters = [", indent.c_str());
      if (func->parameters.size() > 0) {
        printf("\n");
        for (auto& param: func->parameters) {
          printf("%s    {", indent.c_str());
          printf("%s      name = \"%s\"\n", indent.c_str(), std::get<0>(param).c_str());
          printf("%s      type = ", indent.c_str());
          printDET(std::get<1>(param).get(), indent + "      ", false);
          printf("%s    }", indent.c_str());
          printf(",\n");
        }
        printf("%s  ", indent.c_str());
      }
      printf("]\n");
      printf("%s  returnType = ", indent.c_str());
      printDET(func->returnType.get(), indent + "  ", false);
      printf("\n");
      printf("%s  scope = ", indent.c_str());
      printDET(func->scope.get(), indent + "  ", false);
      printf("\n");
      printf("%s}", indent.c_str());
    } break;
    case NodeType::Type: {
      AltaCore::DET::Type* type = dynamic_cast<AltaCore::DET::Type*>(node);
      printf(" {\n");
      printf("%s  isNative = %s\n", indent.c_str(), type->isNative ? "true" : "false");
      if (type->isNative) {
        printf("%s  nativeTypeName = \"%s\"\n", indent.c_str(), AltaCore::DET::NativeType_names[(uint8_t)type->nativeTypeName]);
      }
      printf("%s  modifiers = [", indent.c_str());
      if (type->modifiers.size() > 0) {
        printf("\n");
        for (auto& mod: type->modifiers) {
          printf("%s    ", indent.c_str());
          bool isFirst = true;
          if (mod & (uint8_t)TypeModifierFlag::Constant) {
            if (isFirst) {
              isFirst = false;
            } else {
              printf(" | ");
            }
            printf("Constant");
          }
          if (mod & (uint8_t)TypeModifierFlag::Pointer) {
            if (isFirst) {
              isFirst = false;
            } else {
              printf(" | ");
            }
            printf("Pointer");
          }
          printf(",\n");
        }
        printf("%s  ", indent.c_str());
      }
      printf("]\n");
      printf("%s}", indent.c_str());
    } break;
    default: {
      printf(" {}");
    } break;
  }
};
