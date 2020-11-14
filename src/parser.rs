use pest::Parser;
use std::fs;
use std::boxed::Box;
use crate::ast;
use pest::iterators::Pair;
use std::convert::TryFrom;
use ast::Common;

#[derive(Parser)]
#[grammar = "alta.pest"]
pub struct AltaParser;

fn process(pair: Pair<Rule>) -> ast::Node {
	match pair.as_rule() {
		Rule::root => {
			let pairs = pair.into_inner();
			let mut root = ast::Root::new_box();

			for stmt in pairs {
				if stmt.as_rule() == Rule::EOI {
					break;
				}
				root.as_mut().statements.push(ast::RootStatement::try_from(process(stmt)).unwrap());
			}

			ast::Node::Root(root)
		}
		Rule::root_statement => {
			let mut pairs = pair.into_inner();
			process(pairs.next().unwrap())
		}
		Rule::function_definition => {
			let mut pairs = pair.into_inner();
			let mut func = ast::FunctionDefinition::new_box();

			while let Some(attr) = pairs.next() {
				if attr.as_rule() != Rule::attribute {
					break;
				}
				func.as_mut().attributes.push(Box::<ast::Attribute>::try_from(process(attr)).unwrap());
			}

			// we consumed "function" in that loop above

			func.name = pairs.next().unwrap().as_str().to_string();

			pairs.next(); // consume "("

			let maybe_param = pairs.next().unwrap();
			if maybe_param.as_rule() == Rule::function_parameter {
				func.as_mut().parameters.push(Box::<ast::FunctionParameter>::try_from(process(maybe_param)).unwrap());
				while let Some(comma) = pairs.peek() {
					if comma.as_str() != "," {
						break;
					}
					pairs.next(); // consume ","
					if let Some(param) = pairs.next() {
						func.as_mut().parameters.push(Box::<ast::FunctionParameter>::try_from(process(param)).unwrap());
					} else {
						break;
					}
				}
				pairs.next(); // consume ")"
			} // otherwise, we've just consumed ")"

			pairs.next(); // consume ":"

			func.as_mut().return_type = Box::<ast::Type>::try_from(process(pairs.next().unwrap())).unwrap();

			func.as_mut().body = ast::Statement::try_from(process(pairs.next().unwrap())).unwrap();

			ast::Node::RootStatement(ast::RootStatement::FunctionDefinition(func))
		}
		Rule::function_parameter => {
			let mut pairs = pair.into_inner();
			let mut param = ast::FunctionParameter::new_box();

			param.as_mut().name = pairs.next().unwrap().as_str().to_string();

			pairs.next(); // consume ":"

			param.as_mut().parameter_type = Box::<ast::Type>::try_from(process(pairs.next().unwrap())).unwrap();

			ast::Node::FunctionParameter(param)
		}
		Rule::alta_type => {
			let mut pairs = pair.into_inner();
			let mut alta_type = ast::Type::new_box();

			while let Some(maybe_mod) = pairs.peek() {
				if maybe_mod.as_rule() != Rule::type_modifier {
					break;
				}
				pairs.next(); // consume it
				alta_type.as_mut().modifiers |= match maybe_mod.as_str().trim() {
					"mut" => ast::TypeModifier::MUTABLE,
					"ref" => ast::TypeModifier::REFERENCE,
					_ => unreachable!(),
				}
			}

			alta_type.as_mut().name = pairs.next().unwrap().as_str().to_string();

			ast::Node::Type(alta_type)
		}
		Rule::attribute => {
			let mut pairs = pair.into_inner();
			let mut attr = ast::Attribute::new_box();

			//pairs.next(); // consume "@"
			// actually, we can't consume "@" because Pest doesn't give it to us
			// as a separate token since "attribute" is an atomic rule
			// (weird design choice, but ok)

			attr.as_mut().name = pairs.next().unwrap().as_str().to_string();

			ast::Node::Attribute(attr)
		}
		Rule::statement => {
			let mut pairs = pair.into_inner();
			process(pairs.next().unwrap())
		}
		Rule::expression => {
			let mut pairs = pair.into_inner();
			let mut expr = ast::Expression::try_from(process(pairs.next().unwrap())).unwrap();

			while let Some(suffix) = pairs.next() {
				match suffix.as_rule() {
					Rule::function_call_suffix => {
						let mut spairs = suffix.into_inner();
						let mut call = ast::FunctionCall::new_box();

						call.as_mut().target = expr;

						spairs.next(); // consume "("

						loop {
							let mut maybe_expr = spairs.next().unwrap();
							if maybe_expr.as_str() == ")" {
								// we consumed ")" there
								break;
							}
							let mut name = String::new();
							if maybe_expr.as_rule() != Rule::expression {
								name = maybe_expr.as_str().to_string();
								spairs.next(); // consume ":"
								maybe_expr = spairs.next().unwrap();
							}
							call.as_mut().arguments.push((name, ast::Expression::try_from(process(maybe_expr)).unwrap()));
							let maybe_comma = spairs.next().unwrap();
							if maybe_comma.as_str() != "," {
								// the above consumes ")" (the only other possibility)
								break;
							}
						}

						// we already consumed ")" in that loop

						expr = ast::Expression::FunctionCall(call);
					}
					_ => unreachable!()
				}
			}

			ast::Node::Expression(expr)
		}
		Rule::integer_literal => {
			let mut integer = ast::IntegerLiteral::new_box();
			integer.as_mut().integer = pair.as_str().trim().parse().unwrap();
			ast::Node::Expression(ast::Expression::IntegerLiteral(integer))
		}
		Rule::string_literal => {
			let mut string = ast::StringLiteral::new_box();
			string.as_mut().string = pair.as_str().trim().to_string(); // for now; later, we'll unescape the string
			ast::Node::Expression(ast::Expression::StringLiteral(string))
		}
		Rule::identifier => {
			let mut fetch = ast::Fetch::new_box();
			fetch.as_mut().target = pair.as_str().trim().to_string();
			ast::Node::Expression(ast::Expression::Fetch(fetch))
		}
		Rule::return_statement => {
			let mut pairs = pair.into_inner();
			let mut ret = ast::ReturnStatement::new_box();
			pairs.next(); // consume "return"
			ret.as_mut().expression = ast::Expression::try_from(process(pairs.next().unwrap())).unwrap();
			ast::Node::Statement(ast::Statement::ReturnStatement(ret))
		}
		Rule::block => {
			let mut pairs = pair.into_inner();
			let mut block = ast::Block::new_box();
			pairs.next(); // consume "{"
			loop {
				let next = pairs.next().unwrap();
				if next.as_str() == "}" {
					break;
				}
				block.as_mut().statements.push(ast::Statement::try_from(process(next)).unwrap());
			}
			ast::Node::Statement(ast::Statement::Block(block))
		}
		Rule::expr_statement => {
			let mut pairs = pair.into_inner();
			let mut stmt = ast::ExpressionStatement::new_box();

			stmt.as_mut().expression = ast::Expression::try_from(process(pairs.next().unwrap())).unwrap();

			ast::Node::Statement(ast::Statement::ExpressionStatement(stmt))
		}
		_ => unreachable!()
	}
}

pub fn parse_file(path: &str) -> Box<ast::Root> {
	let contents = fs::read_to_string(path)
		.expect("couldn't read basic hello world example");
	let result = AltaParser::parse(Rule::root, &contents)
		.expect("couldn't parse input")
		.next().unwrap();
	//println!("{:#?}", result);
	match process(result) {
		ast::Node::Root(root) => root,
		_ => unreachable!()
	}
}
