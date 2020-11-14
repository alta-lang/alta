use std::string::String;
use std::vec::Vec;
use std::boxed::Box;

/// Defines common methods and operations on Alta AST nodes
pub trait Common {
	/// Default-constructs a new Box for this type
	fn new_box() -> Box<Self>;
}

pub trait Printable {
	/// Creates a fancy string representation of this type
	fn to_fancy_string(&self, indent: &str) -> String;
}

bitflags! {
	#[derive(Default)]
	pub struct TypeModifier: u8 {
		const MUTABLE   = 0b00000001;
		const REFERENCE = 0b00000010;
	}
}

pub struct Type {
	pub name: String,
	pub modifiers: TypeModifier,
}

pub struct FunctionParameter {
	pub name: String,
	pub parameter_type: Box<Type>,
}

pub struct Attribute {
	pub name: String,
}

pub struct FunctionDefinition {
	pub attributes: Vec<Box<Attribute>>,
	pub name: String,
	pub parameters: Vec<Box<FunctionParameter>>,
	pub return_type: Box<Type>,
	pub body: Statement,
}

pub struct ReturnStatement {
	pub expression: Expression,
}

pub struct ExpressionStatement {
	pub expression: Expression,
}

pub struct IntegerLiteral {
	pub integer: u64,
}

pub struct StringLiteral {
	pub string: String,
}

pub struct FunctionCall {
	pub target: Expression,
	pub arguments: Vec<(String, Expression)>,
}

pub struct StructureDefinition {}

pub struct Block {
	pub statements: Vec<Statement>,
}

pub struct Root {
	pub statements: Vec<RootStatement>,
}

pub struct Fetch {
	pub target: String,
}

sum_type! {
	pub enum RootStatement {
		FunctionDefinition(Box<FunctionDefinition>),
		StructureDefinition(Box<StructureDefinition>),
	}
}

sum_type! {
	pub enum Statement {
		ReturnStatement(Box<ReturnStatement>),
		ExpressionStatement(Box<ExpressionStatement>),
		Block(Box<Block>),
	}
}

sum_type! {
	pub enum Expression {
		IntegerLiteral(Box<IntegerLiteral>),
		StringLiteral(Box<StringLiteral>),
		FunctionCall(Box<FunctionCall>),
		Fetch(Box<Fetch>),
	}
}

sum_type! {
	pub enum Node {
		RootStatement(RootStatement),
		Statement(Statement),
		Expression(Expression),
		Root(Box<Root>),
		Attribute(Box<Attribute>),
		Type(Box<Type>),
		FunctionParameter(Box<FunctionParameter>),
	}
}

impl Printable for Statement {
	fn to_fancy_string(&self, indent: &str) -> String {
		match self {
			Statement::ReturnStatement(node)     => node.as_ref().to_fancy_string(indent),
			Statement::ExpressionStatement(node) => node.as_ref().to_fancy_string(indent),
			Statement::Block(node)               => node.as_ref().to_fancy_string(indent),
		}
	}
}

impl Printable for Expression {
	fn to_fancy_string(&self, indent: &str) -> String {
		match self {
			Expression::IntegerLiteral(node) => node.as_ref().to_fancy_string(indent),
			Expression::StringLiteral(node)  => node.as_ref().to_fancy_string(indent),
			Expression::FunctionCall(node)   => node.as_ref().to_fancy_string(indent),
			Expression::Fetch(node)          => node.as_ref().to_fancy_string(indent),
		}
	}
}

impl Printable for RootStatement {
	fn to_fancy_string(&self, indent: &str) -> String {
		match self {
			RootStatement::FunctionDefinition(node)  => node.as_ref().to_fancy_string(indent),
			RootStatement::StructureDefinition(node) => node.as_ref().to_fancy_string(indent),
		}
	}
}

impl Printable for Node {
	fn to_fancy_string(&self, indent: &str) -> String {
		match self {
			Node::RootStatement(node)     => node.to_fancy_string(indent),
			Node::Statement(node)         => node.to_fancy_string(indent),
			Node::Expression(node)        => node.to_fancy_string(indent),

			Node::Root(node)              => node.as_ref().to_fancy_string(indent),
			Node::Attribute(node)         => node.as_ref().to_fancy_string(indent),
			Node::Type(node)              => node.as_ref().to_fancy_string(indent),
			Node::FunctionParameter(node) => node.as_ref().to_fancy_string(indent),
		}
	}
}

impl Common for Root {
	fn new_box() -> Box<Self> {
		Box::new(Self {
			statements: Vec::new(),
		})
	}
}

impl Printable for Root {
	fn to_fancy_string(&self, indent: &str) -> String {
		let mut result = "Root {".to_string();
		let new_indent = indent.to_string() + "\t";

		if self.statements.len() == 0 {
			result += "}";
		} else {
			result += "\n";
			for stmt in &self.statements {
				result += &new_indent;
				result += &stmt.to_fancy_string(&new_indent);
				result += "\n";
			}
			result += indent;
			result += "}";
		}

		result
	}
}

impl Common for Type {
	fn new_box() -> Box<Self> {
		Box::new(Self {
			name: String::new(),
			modifiers: Default::default(),
		})
	}
}

impl Printable for Type {
	fn to_fancy_string(&self, indent: &str) -> String {
		let mut result = "Type {\n".to_string();
		let new_indent = indent.to_string() + "\t";

		result += &new_indent;
		result += "name = \"";
		result += &self.name;
		result += "\"\n";

		result += &new_indent;
		result += "modifiers = {";
		if self.modifiers.is_empty() {
			result += "}\n";
		} else {
			result += "\n";
			let extra_indent = new_indent.clone() + "\t";
			if self.modifiers.contains(TypeModifier::MUTABLE) {
				result += &extra_indent;
				result += "Mutable\n";
			}
			if self.modifiers.contains(TypeModifier::REFERENCE) {
				result += &extra_indent;
				result += "Reference\n";
			}
			result += &new_indent;
			result += "}\n";
		}

		result += indent;
		result += "}";

		result
	}
}

impl Common for Block {
	fn new_box() -> Box<Self> {
		Box::new(Self {
			statements: Vec::new(),
		})
	}
}

impl Printable for Block {
	fn to_fancy_string(&self, indent: &str) -> String {
		let mut result = "Block {".to_string();
		let new_indent = indent.to_string() + "\t";

		if self.statements.len() == 0 {
			result += "}";
		} else {
			result += "\n";
			for stmt in &self.statements {
				result += &new_indent;
				result += &stmt.to_fancy_string(&new_indent);
				result += "\n";
			}
			result += indent;
			result += "}";
		}

		result
	}
}

impl Common for FunctionParameter {
	fn new_box() -> Box<Self> {
		Box::new(Self {
			name: String::new(),
			parameter_type: Type::new_box(),
		})
	}
}

impl Printable for FunctionParameter {
	fn to_fancy_string(&self, indent: &str) -> String {
		let mut result = "FunctionParameter {\n".to_string();
		let new_indent = indent.to_string() + "\t";

		result += &new_indent;
		result += "name = \"";
		result += &self.name;
		result += "\"\n";

		result += &new_indent;
		result += "type = ";
		result += &self.parameter_type.as_ref().to_fancy_string(&new_indent);
		result += "\n";

		result += indent;
		result += "}";

		result
	}
}

impl Common for Attribute {
	fn new_box() -> Box<Self> {
		Box::new(Self {
			name: String::new(),
		})
	}
}

impl Printable for Attribute {
	fn to_fancy_string(&self, _indent: &str) -> String {
		"Attribute { ".to_string() + &self.name + " }"
	}
}

impl Common for FunctionDefinition {
	fn new_box() -> Box<Self> {
		Box::new(Self {
			attributes: Vec::new(),
			name: String::new(),
			parameters: Vec::new(),
			return_type: Type::new_box(),
			body: Statement::Block(Block::new_box()),
		})
	}
}

impl Printable for FunctionDefinition {
	fn to_fancy_string(&self, indent: &str) -> String {
		let mut result = "FunctionDefinition {\n".to_string();
		let new_indent = indent.to_string() + "\t";

		result += &new_indent;
		result += "name = \"";
		result += &self.name;
		result += "\"\n";

		result += &new_indent;
		result += "attributes = {";
		if self.attributes.len() == 0 {
			result += "}\n";
		} else {
			let extra_indent = new_indent.clone() + "\t";
			result += "\n";
			for attr in &self.attributes {
				result += &extra_indent;
				result += &attr.as_ref().to_fancy_string(&extra_indent);
				result += "\n";
			}
			result += &new_indent;
			result += "}\n";
		}

		result += &new_indent;
		result += "parameters = {";
		if self.parameters.len() == 0 {
			result += "}\n";
		} else {
			let extra_indent = new_indent.clone() + "\t";
			result += "\n";
			for param in &self.parameters {
				result += &extra_indent;
				result += &param.as_ref().to_fancy_string(&extra_indent);
				result += "\n";
			}
			result += &new_indent;
			result += "}\n";
		}

		result += &new_indent;
		result += "return_type = ";
		result += &self.return_type.as_ref().to_fancy_string(&new_indent);
		result += "\n";

		result += &new_indent;
		result += "body = ";
		result += &self.body.to_fancy_string(&new_indent);
		result += "\n";

		result += indent;
		result += "}";

		result
	}
}

impl Common for IntegerLiteral {
	fn new_box() -> Box<Self> {
		Box::new(Self {
			integer: 0,
		})
	}
}

impl Printable for IntegerLiteral {
	fn to_fancy_string(&self, _indent: &str) -> String {
		"IntegerLiteral { ".to_string() + &self.integer.to_string() + " }"
	}
}

impl Common for FunctionCall {
	fn new_box() -> Box<Self> {
		Box::new(Self {
			target: Expression::IntegerLiteral(IntegerLiteral::new_box()),
			arguments: Vec::new(),
		})
	}
}

impl Printable for FunctionCall {
	fn to_fancy_string(&self, indent: &str) -> String {
		let mut result = "FunctionCall {\n".to_string();
		let new_indent = indent.to_string() + "\t";

		result += &new_indent;
		result += "target = ";
		result += &self.target.to_fancy_string(&new_indent);
		result += "\n";

		result += &new_indent;
		result += "parameters = {";
		if self.arguments.len() == 0 {
			result += "}\n";
		} else {
			let first_indent = new_indent.clone() + "\t";
			let extra_indent = new_indent.clone() + "\t\t";
			result += "\n";
			for (name, arg) in &self.arguments {
				result += &first_indent;
				result += "{\n";
				result += &extra_indent;
				result += "name = \"";
				result += &name;
				result += "\"\n";
				result += &extra_indent;
				result += "value = ";
				result += &arg.to_fancy_string(&extra_indent);
				result += "\n";
				result += &first_indent;
				result += "}\n";
			}
			result += &new_indent;
			result += "}\n";
		}

		result += indent;
		result += "}";

		result
	}
}

impl Common for StringLiteral {
	fn new_box() -> Box<Self> {
		Box::new(Self {
			string: String::new(),
		})
	}
}

impl Printable for StringLiteral {
	fn to_fancy_string(&self, _indent: &str) -> String {
		"StringLiteral { ".to_string() + &self.string + " }"
	}
}

impl Common for Fetch {
	fn new_box() -> Box<Self> {
		Box::new(Self {
			target: String::new(),
		})
	}
}

impl Printable for Fetch {
	fn to_fancy_string(&self, _indent: &str) -> String {
		"Fetch { ".to_string() + &self.target + " }"
	}
}

impl Common for ReturnStatement {
	fn new_box() -> Box<Self> {
		Box::new(Self {
			expression: Expression::IntegerLiteral(IntegerLiteral::new_box()),
		})
	}
}

impl Printable for ReturnStatement {
	fn to_fancy_string(&self, indent: &str) -> String {
		let new_indent = indent.to_string() + "\t";
		"ReturnStatement {\n".to_string() + &new_indent + &self.expression.to_fancy_string(&new_indent) + "\n" + indent + "}"
	}
}

impl Common for ExpressionStatement {
	fn new_box() -> Box<Self> {
		Box::new(Self {
			expression: Expression::IntegerLiteral(IntegerLiteral::new_box()),
		})
	}
}

impl Printable for ExpressionStatement {
	fn to_fancy_string(&self, indent: &str) -> String {
		let new_indent = indent.to_string() + "\t";
		"ExpressionStatement {\n".to_string() + &new_indent + &self.expression.to_fancy_string(&new_indent) + "\n" + indent + "}"
	}
}

impl Common for StructureDefinition {
	fn new_box() -> Box<Self> {
		Box::new(Self {})
	}
}

impl Printable for StructureDefinition {
	fn to_fancy_string(&self, _indent: &str) -> String {
		"StructureDefinition {}".to_string()
	}
}
