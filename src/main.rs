extern crate pest;
#[macro_use]
extern crate pest_derive;
#[macro_use]
extern crate sum_type;
#[macro_use]
extern crate bitflags;

mod parser;
mod ast;

use parser::parse_file;
use ast::Printable;

fn main() {
	let root = parse_file("examples/hello-world.alta");
	println!("{}", root.as_ref().to_fancy_string(""));
}
