use std::{mem::MaybeUninit, path::Path, sync::Arc};

use pest::{iterators::Pair, Parser as PestParser};
use pest_derive::Parser as PestParser;

use crate::{ast, package};

#[derive(PestParser)]
#[grammar = "./parser.pest"]
struct Parser;

fn parse_string_literal(literal: Pair<Rule>) -> String {
	assert!(literal.as_rule() == Rule::double_quote_string);
	let src = &literal.as_str()[1..literal.as_str().len() - 1];
	let mut result = String::new();
	let mut escaped = false;
	let mut char_iter = src.chars();

	while let Some(char) = char_iter.next() {
		if escaped {
			escaped = false;
			match char {
				'n' => result.push('\n'),
				'r' => result.push('\r'),
				't' => result.push('\t'),
				'0' => result.push('\0'),
				'"' => result.push('"'),
				'\\' => result.push('\\'),
				'x' => {
					let val = char_iter
						.by_ref()
						.take(2)
						.map(|c| c.to_digit(16).unwrap())
						.reduce(|acc, digit| (acc << 4) | digit)
						.unwrap();
					result.push(char::from_u32(val).unwrap());
				},
				'u' => {
					// consume the opening '{'
					assert!(char_iter.next().unwrap() == '{');
					// the following consumes the '}' as part of the `map_while`
					let val = char_iter
						.by_ref()
						.map_while(|c| if c == '}' { None } else { Some(c.to_digit(16).unwrap()) })
						.reduce(|acc, digit| (acc << 4) | digit)
						.unwrap();
					result.push(char::from_u32(val).unwrap());
				},
				_ => unreachable!(),
			}
		} else if char == '\\' {
			escaped = true;
		} else {
			result.push(char);
		}
	}

	result
}

pub fn parse(contents: &str, path: &Path) -> Result<Arc<ast::RootNode>, pest::error::Error<Rule>> {
	let parsed = Parser::parse(Rule::root, contents)?.next().unwrap();
	let mut result = ast::RootNode {
		source_path: path.to_owned(),
		statements: Vec::new(),
	};

	for global_stmt in parsed.into_inner() {
		match global_stmt.as_rule() {
			Rule::import_statement => {
				let mut import_path = MaybeUninit::uninit();
				let mut items = Vec::new();

				for item in global_stmt.into_inner() {
					match item.as_rule() {
						Rule::keyword_import | Rule::keyword_from => {},
						Rule::double_quote_string => {
							import_path.write(parse_string_literal(item));
						},
						Rule::import_item => {
							let mut pairs = item.into_inner();
							let accessor_pair = pairs.next().unwrap();
							let components = accessor_pair.into_inner().map(|pair| pair.as_str().to_owned()).collect();
							let alias = pairs.next().map(|alias| alias.as_str().to_owned());
							items.push(ast::ImportItem {
								components,
								alias,
							});
						},
						_ => unreachable!(),
					};
				}

				// SAFETY: this rule always includes an import path, so this will always be written
				let target = unsafe { import_path.assume_init() };
				let resolved_target = package::resolve_import(&path, &target);

				result.statements.push(Arc::new(ast::ImportStatement {
					target,
					items,
					resolved_target,
				}))
			},
			Rule::struct_definition_statement => {
				println!("TODO: struct definition");
			},
			Rule::free_function_definition_statement => {
				println!("TODO: free function definition");
			},
			Rule::empty_statement => {},
			Rule::EOI => {},
			_ => unreachable!(),
		};
	}

	Ok(Arc::new(result))
}
