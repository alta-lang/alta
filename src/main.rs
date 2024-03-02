mod parser;
mod package;
mod ast;
mod workqueue;
mod util;

use std::{collections::HashSet, env::current_dir, path::{Path, PathBuf}, process::{ExitCode, Termination}, sync::{atomic::{AtomicBool, Ordering}, Mutex}};

use clap::Parser as ArgParser;
use path_clean::PathClean;

use crate::{ast::Node, util::resolve_chrooted};

#[derive(ArgParser)]
#[command(version, about, long_about = None)]
struct Args {
	/// The path to the package or module to build.
	///
	/// If omitted, defaults to using the current directory as the package to build.
	source: Option<PathBuf>,

	/// Build the specified package targets (only valid when SOURCE points to a package).
	///
	/// If omitted, defaults to building all the targets of the package.
	#[arg(short, long, value_name = "PACKAGE_TARGET")]
	target: Vec<String>,

	/// An override for the package name.
	///
	/// If omitted, the compiler will search for `package.alta.toml` in the parent directories of the source
	/// and use the package name found there. If none is found, an error will be produced.
	#[arg(long, value_name = "NAME")]
	package_name: Option<String>,

	/// An override for the package version.
	///
	/// If omitted, the compiler will search for `package.alta.toml` in the parent directories of the source
	/// and use the package version found there. If none is found, an error will be produced.
	#[arg(long, value_name = "VERSION")]
	package_version: Option<String>,

	/// An override for the target kind being produced (only valid when SOURCE points to a single module).
	///
	/// If omitted, defaults to producing a binary.
	#[arg(long, value_name = "KIND")]
	target_kind: Option<String>,

	/// Produce native code for the specified LLVM target.
	///
	/// If omitted, defaults to the LLVM target for the host.
	#[arg(long, value_name = "LLVM_TARGET")]
	llvm_target: Option<String>,
}

fn main() -> impl Termination {
	let mut args = Args::parse();

	if args.source.is_none() {
		args.source = Some(".".into());
	}

	// resolve it to an absolute normalized path
	let unwrapped_source = current_dir().unwrap_or("/".into()).join(args.source.unwrap()).clean();

	if !unwrapped_source.exists() {
		eprintln!("No such file or directory: {}", unwrapped_source.display());
		return ExitCode::FAILURE;
	}

	let mut pkg: PathBuf = unwrapped_source.clone();
	let mut pkg_info = package::Info::new();
	let pkg_info_required = pkg.is_dir() || !(args.package_name.is_some() && args.package_version.is_some());
	let mut pkg_info_found = false;
	let mut targets_to_build;

	if pkg.is_dir() {
		let pkg_toml = pkg.join("package.alta.toml");
		if pkg_toml.exists() && !pkg_toml.is_dir() {
			match package::Info::try_from_file(&pkg_toml) {
				None => {
					eprintln!("Warning: failed to parse package info at {}", pkg_toml.display());
				}
				Some(parsed) => {
					pkg_info = parsed;
					pkg_info_found = true;
				}
			};
		}
	} else {
		pkg = match pkg.parent() {
			None => ".".into(),
			Some(x) => x.to_path_buf(),
		};

		loop {
			let pkg_toml = pkg.join("package.alta.toml");
			if pkg_toml.exists() && !pkg_toml.is_dir() {
				match package::Info::try_from_file(&pkg_toml) {
					None => {
						eprintln!("Warning: failed to parse package info at {}", pkg_toml.display());
					}
					Some(parsed) => {
						pkg_info = parsed;
						pkg_info_found = true;
						break;
					}
				};
			}
	
			if !pkg.pop() {
				// just use the file's parent directory as the package root
				pkg = unwrapped_source.clone();
				if !pkg.pop() {
					pkg = ".".into();
				}
				break;
			}
		}
	}

	if pkg_info_required && !pkg_info_found {
		eprintln!("Package information file (`package.alta.toml`) was required but none was found.");
		if !unwrapped_source.is_dir() {
			eprintln!("Since the source path is a module, you can also specify the package name and version directly on the command line instead using `--package-name` and `--package-version`.");
		}
		return ExitCode::FAILURE;
	}

	if let Some(x) = args.package_name {
		pkg_info.name = x;
	}
	if let Some(x) = args.package_version {
		pkg_info.version = x;
	}

	if unwrapped_source.is_dir() {
		if args.target.len() == 0 {
			targets_to_build = (0..args.target.len()).collect();
		} else {
			targets_to_build = Vec::new();
			for target_name in args.target.iter() {
				let mut found = false;
				for (i, target) in pkg_info.targets.iter().enumerate() {
					if target.name == target_name.as_str() {
						found = true;
						targets_to_build.push(i);
						break;
					}
				}

				if !found {
					eprintln!("No such target: {}", target_name);
					return ExitCode::FAILURE;
				}
			}
		}
	} else {
		pkg_info.targets.push(package::Target {
			name: unwrapped_source.file_stem().unwrap_or(unwrapped_source.as_os_str()).to_string_lossy().to_string(),
			main: unwrapped_source.strip_prefix(&pkg).unwrap().to_string_lossy().to_string(),
			kind: args.target_kind.and_then(|kind| match kind.as_str() {
				"bin" | "binary" => Some(package::TargetKind::Binary),
				"lib" | "library" => Some(package::TargetKind::Library),
				_ => None,
			}).unwrap_or(package::TargetKind::Binary),
		});
		targets_to_build = vec![pkg_info.targets.len() - 1];
	}

	println!("package name = {}; package version = {}", &pkg_info.name, &pkg_info.version);

	for target_idx in targets_to_build.iter() {
		let target = &pkg_info.targets[*target_idx];
		let target_kind_str = match target.kind {
			package::TargetKind::Binary => "binary",
			package::TargetKind::Library => "library",
		};

		println!("building target {} \"{}\"...", target_kind_str, target.name);

		let main = pkg.join(resolve_chrooted(&target.main));
		let processed = Mutex::new(HashSet::new());
		let encountered_error = AtomicBool::new(false);

		let asts: Vec<_> = workqueue::execute([main], |file_path, push_work| -> Option<_> {
			// clone this outside the critical region to minimize time spent there
			let file_path_clone = file_path.clone();

			{
				let mut guard = processed.lock().unwrap();
				if !guard.insert(file_path_clone) {
					return None;
				}
			}

			let contents = match std::fs::read_to_string(&file_path) {
				Ok(x) => x,
				Err(e) => {
					eprintln!("Error while reading \"{}\": {}", file_path.display(), e);
					encountered_error.store(true, Ordering::Relaxed);
					return None;
				},
			};
			let ast = match parser::parse(&contents, &file_path) {
				Ok(x) => x,
				Err(e) => {
					eprintln!("Error while parsing \"{}\": {}", file_path.display(), e);
					encountered_error.store(true, Ordering::Relaxed);
					return None;
				},
			};

			struct ImportVisitor<'a>(&'a dyn Fn(PathBuf), &'a Path, bool);
			impl<'a> ast::Visitor for ImportVisitor<'a> {
				fn visit_import_statement(&mut self, node: &ast::ImportStatement) {
					let target = match &node.resolved_target {
						Some(x) => x.clone(),
						None => {
							eprintln!("Failed to resolve \"{}\" from \"{}\"", &node.target, self.1.display());
							self.2 = true;
							return;
						},
					};
					self.0(target);
				}
			}

			let mut visitor = ImportVisitor(push_work, &file_path, false);
			ast.visit(&mut visitor);

			if visitor.2 {
				encountered_error.store(true, Ordering::Relaxed);
				return None;
			}

			Some(ast)
		}).iter_mut().filter_map(|maybe_ast| maybe_ast.take()).collect();

		if encountered_error.into_inner() {
			eprintln!("Encountered error(s) building target {} \"{}\"", target_kind_str, target.name);
			return ExitCode::FAILURE;
		}

		for ast in asts.iter() {
			println!("{}", ast)
		}
	}

	ExitCode::SUCCESS
}
