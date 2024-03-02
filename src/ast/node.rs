use std::fmt::Display;

pub trait Visitor {
	// we use `let _ = node;` because if we simply rename `node` to `_node`, IDEs use `_node`
	// as the name for autocompletions of implementations for these functions.

	fn visit_root_node(&mut self, node: &super::RootNode) { let _ = node; }
	fn visit_import_statement(&mut self, node: &super::ImportStatement) { let _ = node; }
}

pub trait Node: Send + Sync + Display {
	fn visit(&self, visitor: &mut dyn Visitor);
}
