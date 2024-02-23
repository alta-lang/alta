#include <altall/mangle.hpp>
#include <altall/util.hpp>

ALTACORE_MAP<std::string, std::string> AltaLL::mangledToFullMapping;

std::string AltaLL::cTypeNameify(std::shared_ptr<AltaCore::DET::Type> type, bool mangled) {
	using NT = AltaCore::DET::NativeType;
	bool first;
	if (type->isOptional) {
		return "?" + mangleType(type->optionalTarget);
	} if (type->isFunction) {
		if (!type->isRawFunction) {
			return "=>";
		}
		std::string result = "->" + mangleType(type->returnType);
		if (type->isMethod) {
			result += ":" + mangleType(std::make_shared<AltaCore::DET::Type>(type->methodParent)->reference());
		}
		result += '(';
		first = true;
		for (auto& [name, param, isVariable, id]: type->parameters) {
			if (first) {
				first = false;
			} else {
				result += ',';
			}
			if (isVariable) {
				result += "!";
			}
			auto target = isVariable ? param->point() : param;
			result += mangleType(target);
		}
		result += ')';
		return result;
	} else if (type->isNative) {
		switch (type->nativeTypeName) {
			case NT::Integer:
				return "i";
			case NT::Byte:
				return "c";
			case NT::Bool:
				return "b";
			case NT::Void:
				return "v";
			case NT::Double:
				return "d";
			case NT::Float:
				return "f";
			case NT::UserDefined:
				return type->userDefinedName;
			default:
				throw std::runtime_error("ok, wtaf.");
		}
	} else if (type->isUnion()) {
		std::string result = "|";
		first = true;
		for (auto& item: type->unionOf) {
			if (first) {
				first = false;
			} else {
				result += ',';
			}
			result += mangleType(item);
		}
		result += "|";
		return result;
	} else {
		return mangleName(type->klass, true, false);
	}
};

std::string AltaLL::mangleType(std::shared_ptr<AltaCore::DET::Type> type) {
	using TypeModifier = AltaCore::Shared::TypeModifierFlag;
	std::string mangled;

	for (auto& mod: type->modifiers) {
		if (mod & (uint8_t)TypeModifier::Constant) {
			mangled += "c";
		}
		if (mod & (uint8_t)TypeModifier::Pointer) {
			mangled += "*";
		}
		if (mod & (uint8_t)TypeModifier::Reference) {
			mangled += "&";
		}
		if (mod & (uint8_t)TypeModifier::Long) {
			mangled += "L";
		}
		if (mod & (uint8_t)TypeModifier::Short) {
			mangled += "S";
		}
		if (mod & (uint8_t)TypeModifier::Unsigned) {
			mangled += "u";
		}
		if (mod & (uint8_t)TypeModifier::Signed) {
			mangled += "s";
		}
	}

	mangled += cTypeNameify(type, true);

	if (!type->isRawFunction) {
		auto rawCopy = type->copy();
		rawCopy->isRawFunction = true;
		mangled += cTypeNameify(rawCopy, true);
	}

	return mangled;
};

std::shared_ptr<AltaCore::DET::ScopeItem> AltaLL::followAlias(std::shared_ptr<AltaCore::DET::ScopeItem> maybeAlias) {
	while (auto alias = std::dynamic_pointer_cast<AltaCore::DET::Alias>(maybeAlias)) {
		maybeAlias = alias->target;
	}
	return maybeAlias;
};

std::string AltaLL::escapeName(std::string name) {
	std::string escaped;

	for (size_t i = 0; i < name.size(); i++) {
		auto character = name[i];
		if (character == '#') {
			escaped += "##";
		} else if (character == '.') {
			escaped += "..";
		} else if (character == '$') {
			escaped += "$$";
		} else if (
			(character >= '0' && character <= '9') || // 0-9
			(character >= 'A' && character <= 'Z') || // A-Z
			(character >= 'a' && character <= 'z') || // a-z
			(character == '/') ||
			(character == '_') ||
			(character == '@')
		) {
			escaped += character;
		} else {
			escaped += "#" + AltaLL::byteToHex(character);
		}
	}

	return escaped;
};

std::string AltaLL::mangleName(std::shared_ptr<AltaCore::DET::Module> mod, bool fullName) {
	auto version = mod->packageInfo.version;
	auto normalVersionString = std::to_string(version.major) + '.' + std::to_string(version.minor) + '.' + std::to_string(version.patch);
	auto result = escapeName(mod->name) + "%" + normalVersionString;

	mangledToFullMapping[result] = mod->toString();

	return result;
};

std::string AltaLL::mangleName(std::shared_ptr<AltaCore::DET::Scope> scope, bool fullName) {
	std::string mangled = "." + std::to_string(scope->relativeID);
	if (!fullName) return mangled;
	if (auto mod = scope->parentModule.lock()) {
		mangled = mangleName(mod, true) + "." + mangled;
	} else if (auto func = scope->parentFunction.lock()) {
		mangled = mangleName(func, true, false) + "." + mangled;
	} else if (auto parent = scope->parent.lock()) {
		mangled = mangleName(parent, true) + "." + mangled;
	} else if (auto ns = scope->parentNamespace.lock()) {
		mangled = mangleName(ns, true, false) + "." + mangled;
	}

	mangledToFullMapping[mangled] = scope->toString();

	return mangled;
};

std::string AltaLL::mangleName(std::shared_ptr<AltaCore::DET::ScopeItem> item, bool fullName, bool root) {
	item = followAlias(item);

	using NodeType = AltaCore::DET::NodeType;
	namespace DET = AltaCore::DET;
	auto nodeType = item->nodeType();
	std::string itemName;
	auto isLiteral = false;
	std::string mangled;
	bool first;

	if (nodeType == NodeType::Function) {
		auto func = std::dynamic_pointer_cast<DET::Function>(item);
		isLiteral = func->isLiteral;
		itemName = isLiteral ? func->name : escapeName(func->name);
		if (func->genericArguments.size() > 0) {
			itemName += "<";
		}
		first = true;
		for (auto arg: func->genericArguments) {
			if (first) {
				first = false;
			} else {
				itemName += ',';
			}
			itemName += mangleType(arg);
		}
		if (func->genericArguments.size() > 0) {
			itemName += ">";
		}

		if (!isLiteral) {
			itemName = itemName;
			itemName += "(";
			first = true;
			for (auto& [name, type, isVariable, id]: func->parameters) {
				if (first) {
					first = false;
				} else {
					itemName += ',';
				}
				itemName += escapeName(name) + ((isVariable) ? "!" : ":") + mangleType(type);
			}
			itemName += ")";
			if (func->returnType) {
				if (func->returnType->klass && func->returnType->klass->scope->hasParent(func->scope)) {
					itemName += ":-";
				} else {
					itemName += ":" + mangleType(func->returnType);
				}
			}
		}
	} else if (nodeType == NodeType::Variable) {
		auto var = std::dynamic_pointer_cast<DET::Variable>(item);
		if (var->isVariable) {
			var->isVariable = false;
			auto tmp = "+" + mangleName(var, fullName, root);
			var->isVariable = true;
			return tmp;
		}
		isLiteral = isLiteral || var->isLiteral;
		if (!isLiteral) {
			if (auto ps = var->parentScope.lock()) {
				if (auto pc = ps->parentClass.lock()) {
					isLiteral = pc->isLiteral;
				}
			}
		}
		itemName = isLiteral ? var->name : escapeName(var->name);
	} else if (nodeType == NodeType::Namespace) {
		auto ns = std::dynamic_pointer_cast<DET::Namespace>(item);
		isLiteral = false;
		itemName = isLiteral ? ns->name : escapeName(ns->name);
	} else if (nodeType == NodeType::Class) {
		auto klass = std::dynamic_pointer_cast<DET::Class>(item);
		isLiteral = klass->isLiteral;
		itemName = isLiteral ? klass->name : escapeName(klass->name);
		if (klass->genericArguments.size() > 0) {
			itemName += "<";
		}
		first = true;
		for (auto arg: klass->genericArguments) {
			if (first) {
				first = false;
			} else {
				itemName += ',';
			}
			itemName += mangleType(arg);
		}
		if (klass->genericArguments.size() > 0) {
			itemName += ">";
		}
	} else {
		throw std::runtime_error("Cannot use mangleName to mangle the name of this scope item: " + item->toString());
	}

#if 0
	if (overridenNames.find(item->id) != overridenNames.end()) {
		isLiteral = true;
		itemName = overridenNames[item->id];
	}
#endif

	if (!isLiteral && fullName) {
		auto maybeScope = item->parentScope;
		while (!maybeScope.expired()) {
			auto scope = maybeScope.lock();
			if (!scope->parentModule.expired()) {
				mangled += mangleName(scope->parentModule.lock(), true) + "." + mangled;
				break;
			} else if (!scope->parentFunction.expired()) {
				mangled += mangleName(scope->parentFunction.lock(), true, false) + "." + mangled;
				break;
			} else if (!scope->parentNamespace.expired()) {
				mangled += mangleName(scope->parentNamespace.lock(), true, false) + "." + mangled;
				break;
			} else if (!scope->parentClass.expired()) {
				mangled += mangleName(scope->parentClass.lock(), true, false) + "." + mangled;
				break;
			}

			if (!scope->parent.expired()) {
				mangled += "." + std::to_string(scope->relativeID) + "." + mangled;
				maybeScope = scope->parent;
			}
		}
	}

	mangled += itemName;

	// new final step for non-literal names: hashing
	// this is necessary to establish a maximum length for identifiers that
	// is nearly impossible to have the same output for different inputs
	if (!isLiteral && root) {
		mangled = "Alta_" + mangled;
	}

	mangledToFullMapping[mangled] = item->toString();

#if 0
	{
		auto tmp = item->toString();
		printf("%s mangled to %s\n", tmp.c_str(), mangled.c_str());
	}
#endif

	return mangled;
};
