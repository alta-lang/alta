use std::fmt::Display;

use super::{Constraint, GlobalStatement, Node};

pub struct StructMember {
	pub name: String,
	pub ty: todo!(),
}

pub struct StructDefinitionStatement {
	pub name: String,
	pub generic_parameters: Vec<String>,
	pub constraints: Vec<Constraint>,
	pub members: Vec<StructMember>,
}

impl Display for StructDefinitionStatement {
	fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
		write!(f, "Struct({})", self.name)
	}
}

impl Node for StructDefinitionStatement {
	fn visit(&self, visitor: &mut dyn super::Visitor) {
		visitor.visit_struct_definition_statement(self);
	}
}

impl GlobalStatement for StructDefinitionStatement {

}
