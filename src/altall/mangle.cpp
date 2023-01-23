#include <altall/mangle.hpp>
#include <picosha2.h>

ALTACORE_MAP<std::string, std::string> AltaLL::mangledToFullMapping;

std::string AltaLL::cTypeNameify(std::shared_ptr<AltaCore::DET::Type> type, bool mangled) {
	using NT = AltaCore::DET::NativeType;
	if (type->isOptional) {
		return "_Alta_optional_" + mangleType(type->optionalTarget);
	} if (type->isFunction) {
		if (!type->isRawFunction) {
			return "_Alta_basic_function";
		}
		std::string result = "_Alta_func_ptr_" + mangleType(type->returnType);
		if (type->isMethod) {
			result += "_" + mangleType(std::make_shared<AltaCore::DET::Type>(type->methodParent)->reference());
		}
		for (auto& [name, param, isVariable, id]: type->parameters) {
			result += '_';
			auto target = isVariable ? param->point() : param;
			result += mangleType(target);
		}
		return result;
	} else if (type->isNative) {
		switch (type->nativeTypeName) {
			case NT::Integer:
				return "int";
			case NT::Byte:
				return "char";
			case NT::Bool:
				return "_Alta_bool";
			case NT::Void:
				return "void";
			case NT::Double:
				return "double";
			case NT::Float:
				return "float";
			case NT::UserDefined:
				return type->userDefinedName;
			default:
				throw std::runtime_error("ok, wtaf.");
		}
	} else if (type->isUnion()) {
		std::string result = "_Alta_union";
		for (auto& item: type->unionOf) {
			result += '_';
			result += mangleType(item);
		}
		return result;
	} else {
		return mangleName(type->klass);
	}
};

std::string AltaLL::mangleType(std::shared_ptr<AltaCore::DET::Type> type) {
	using TypeModifier = AltaCore::Shared::TypeModifierFlag;
	std::string mangled;

	for (auto& mod: type->modifiers) {
		if (mod & (uint8_t)TypeModifier::Constant) {
			mangled += "const_3_";
		}
		if (mod & (uint8_t)TypeModifier::Pointer) {
			mangled += "ptr_3_";
		}
		if (mod & (uint8_t)TypeModifier::Reference) {
			mangled += "ref_3_";
		}
		if (mod & (uint8_t)TypeModifier::Long) {
			mangled += "long_3_";
		}
		if (mod & (uint8_t)TypeModifier::Short) {
			mangled += "short_3_";
		}
		if (mod & (uint8_t)TypeModifier::Unsigned) {
			mangled += "unsigned_3_";
		}
		if (mod & (uint8_t)TypeModifier::Signed) {
			mangled += "signed_3_";
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

	/**
		* Escapes 0-30 are reserved for internal use (since they're control characters in ASCII anyways).
		* `_0_` is reserved for scope item name separation
		* `_1_` is reserved for function parameter type separation
		* `_2_` is reserved for generic instantiation argument separation
		* `_3_` is reserved for type modifier separation
		* `_4_` is reserved for scope identifier delineation
		* `_5_` is reserved for version delimitation
		* `_6_` is reserved for version prerelease delineation
		* `_7_` is reserved for version build information delineation
		* `_8_` is reserved for variable function parameter type separation
		* `_9_` is reserved for parameter name separation
		* `_10_` is reserved for lambda ID delineation
		* `_11_` is reserved for function return type delineation
		*/

	for (size_t i = 0; i < name.size(); i++) {
		auto character = name[i];
		if (character == '_') {
			escaped += "__";
		} else if (
			(character >= '0' && character <= '9') || // 0-9
			(character >= 'A' && character <= 'Z') || // A-Z
			(character >= 'a' && character <= 'z')    // a-z
		) {
			escaped += character;
		} else {
			escaped += "_" + std::to_string((unsigned short int)character) + "_";
		}
	}

	return escaped;
};

std::string AltaLL::mangleName(std::shared_ptr<AltaCore::DET::Module> mod, bool fullName) {
	auto version = mod->packageInfo.version;
	// since we use underscores as special escape characters,
	// and dashes aren't allowed in identifiers,
	// we have to use another character to separate version parts
	// 'a' works just fine
	auto versionString = std::to_string(version.major) + 'a' + std::to_string(version.minor) + 'a' + std::to_string(version.patch);
	auto normalVersionString = std::to_string(version.major) + '.' + std::to_string(version.minor) + '.' + std::to_string(version.patch);
	if (version.prerelease != NULL) {
		versionString += std::string("_6_") + version.prerelease;
		normalVersionString += '-' + std::string(version.prerelease);
	}
	if (version.metadata != NULL) {
		versionString += std::string("_7_") + version.metadata;
		normalVersionString += '+' + std::string(version.metadata);
	}
	auto result = escapeName(mod->name) + "_5_" + versionString;

	mangledToFullMapping[result] = mod->toString();

	return result;
};

std::string AltaLL::mangleName(std::shared_ptr<AltaCore::DET::Scope> scope, bool fullName) {
	std::string mangled = "_4_" + std::to_string(scope->relativeID);
	if (!fullName) return mangled;
	if (auto mod = scope->parentModule.lock()) {
		mangled = mangleName(mod, true) + "_0_" + mangled;
	} else if (auto func = scope->parentFunction.lock()) {
		mangled = mangleName(func, true) + "_0_" + mangled;
	} else if (auto parent = scope->parent.lock()) {
		mangled = mangleName(parent, true) + "_0_" + mangled;
	} else if (auto ns = scope->parentNamespace.lock()) {
		mangled = mangleName(ns, true) + "_0_" + mangled;
	}

	mangledToFullMapping[mangled] = scope->toString();

	return mangled;
};

std::string AltaLL::mangleName(std::shared_ptr<AltaCore::DET::ScopeItem> item, bool fullName) {
	item = followAlias(item);

	using NodeType = AltaCore::DET::NodeType;
	namespace DET = AltaCore::DET;
	auto nodeType = item->nodeType();
	std::string itemName;
	auto isLiteral = false;
	std::string mangled;

	if (nodeType == NodeType::Function) {
		auto func = std::dynamic_pointer_cast<DET::Function>(item);
		isLiteral = func->isLiteral;
		itemName = isLiteral ? func->name : escapeName(func->name);
		for (auto arg: func->genericArguments) {
			itemName += "_2_" + mangleType(arg);
		}

		if (!isLiteral) {
			itemName = escapeName(itemName);
			for (auto& [name, type, isVariable, id]: func->parameters) {
				itemName += "_9_" + escapeName(name) + ((isVariable) ? "_8_" : "_1_") + mangleType(type);
			}
			if (func->returnType) {
				if (func->returnType->klass && func->returnType->klass->scope->hasParent(func->scope)) {
					itemName += "_11__64_CaptureClass_64_";
				} else {
					itemName += "_11_" + mangleType(func->returnType);
				}
			}
		}
	} else if (nodeType == NodeType::Variable) {
		auto var = std::dynamic_pointer_cast<DET::Variable>(item);
		isLiteral = isLiteral || var->isLiteral;
		if (!isLiteral) {
			if (auto ps = var->parentScope.lock()) {
				if (auto pc = ps->parentClass.lock()) {
					isLiteral = pc->isLiteral;
				}
			}
		}
		itemName = isLiteral ? var->name : escapeName(var->name);
		if (var->isVariable) {
			var->isVariable = false;
			auto tmp = "_Alta_array_" + mangleName(var);
			var->isVariable = true;
			return tmp;
		}
	} else if (nodeType == NodeType::Namespace) {
		auto ns = std::dynamic_pointer_cast<DET::Namespace>(item);
		isLiteral = false;
		itemName = isLiteral ? ns->name : escapeName(ns->name);
	} else if (nodeType == NodeType::Class) {
		auto klass = std::dynamic_pointer_cast<DET::Class>(item);
		isLiteral = klass->isLiteral;
		itemName = isLiteral ? klass->name : escapeName(klass->name);
		for (auto arg: klass->genericArguments) {
			itemName += "_2_" + mangleType(arg);
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
				mangled += mangleName(scope->parentModule.lock()) + "_0_" + mangled;
				maybeScope = std::weak_ptr<DET::Scope>(); // modules are root nodes, stop because we found one
			} else if (!scope->parentFunction.expired()) {
				mangled += mangleName(scope->parentFunction.lock()) + "_0_" + mangled;
				maybeScope = scope->parentFunction.lock()->parentScope;
			} else if (!scope->parent.expired()) {
				mangled += "_4_" + std::to_string(scope->relativeID) + "_0_" + mangled;
				maybeScope = scope->parent;
			} else if (!scope->parentNamespace.expired()) {
				mangled += mangleName(scope->parentNamespace.lock()) + "_0_" + mangled;
				maybeScope = scope->parentNamespace.lock()->parentScope;
			} else if (!scope->parentClass.expired()) {
				mangled += mangleName(scope->parentClass.lock()) + "_0_" + mangled;
				maybeScope = scope->parentClass.lock()->parentScope;
			}
		}
	}

	mangled += itemName;

	// new final step for non-literal names: hashing
	// this is necessary to establish a maximum length for identifiers that
	// is nearly impossible to have the same output for different inputs
	if (!isLiteral) {
		mangled = "Alta_" + picosha2::hash256_hex_string(mangled);
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
