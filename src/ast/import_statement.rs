use std::{fmt::{Display, Write}, path::PathBuf};

use indenter::indented;

use super::{GlobalStatement, Node};

pub struct ImportItem {
	pub components: Vec<String>,
	pub alias: Option<String>,
}

impl Display for ImportItem {
	fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
		write!(f, "{}", self.components.join("."))?;
		if let Some(alias) = &self.alias {
			write!(f, " as {}", alias)
		} else {
			Ok(())
		}
	}
}

pub struct ImportStatement {
	pub target: String,
	pub resolved_target: Option<PathBuf>,
	pub items: Vec<ImportItem>,
}

impl Display for ImportStatement {
	fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
		write!(f, "Import(\"{}\" => {}) {{{}", self.target, match &self.resolved_target {
			Some(x) => format!("\"{}\"", x.display()),
			None => "?".into(),
		}, if self.items.is_empty() { "}" } else { "\n" })?;
		for item in self.items.iter() {
			write!(indented(f).with_str("\t"), "{}\n", item)?;
		}
		if self.items.is_empty() {
			Ok(())
		} else {
			write!(f, "}}")
		}
	}
}

impl Node for ImportStatement {
	fn visit(&self, visitor: &mut dyn super::Visitor) {
		visitor.visit_import_statement(self);
	}
}

impl GlobalStatement for ImportStatement {

}
