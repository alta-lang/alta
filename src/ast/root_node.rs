use std::{fmt::{Display, Write}, path::PathBuf, sync::Arc};

use indenter::indented;

use super::{GlobalStatement, Node};

pub struct RootNode {
	pub source_path: PathBuf,
	pub statements: Vec<Arc<dyn GlobalStatement>>,
}

impl Display for RootNode {
	fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
		write!(f, "Root(\"{}\") {{{}", self.source_path.display(), if self.statements.is_empty() { "}" } else { "\n" })?;
		for statement in self.statements.iter() {
			write!(indented(f).with_str("\t"), "{}\n", statement)?;
		}
		if !self.statements.is_empty() {
			write!(f, "}}")
		} else {
			Ok(())
		}
	}
}

impl Node for RootNode {
	fn visit(&self, visitor: &mut dyn super::Visitor) {
		visitor.visit_root_node(self);
		for stmt in self.statements.iter() {
			stmt.visit(visitor);
		}
	}
}

impl GlobalStatement for RootNode {

}
