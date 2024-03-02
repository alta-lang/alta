use std::{fs::{self, DirEntry}, path::{Component, Path, PathBuf}};

use serde::Deserialize;

use crate::util::resolve_chrooted;

#[derive(Deserialize)]
pub struct Info {
	pub name: String,
	pub version: String,

	#[serde(default)]
	pub targets: Vec<Target>,
}

#[derive(Deserialize)]
pub struct Target {
	pub name: String,
	pub main: String,
	pub kind: TargetKind,
}

#[derive(Deserialize)]
pub enum TargetKind {
	#[serde(rename = "bin")]
	Binary,

	#[serde(rename = "lib")]
	Library,
}

impl Info {
	pub fn new() -> Self {
		Self {
			name: "".into(),
			version: "0.0.0".into(),
			targets: Vec::new(),
		}
	}
	pub fn try_from_file(path: &Path) -> Option<Self> {
		let contents = fs::read_to_string(path).ok()?;
		toml::from_str::<Info>(&contents).ok()
	}
}

pub fn find_package_root(path: &Path) -> Option<PathBuf> {
	let mut pkg = match path.parent() {
		None => ".".into(),
		Some(x) => x.to_path_buf(),
	};

	loop {
		let pkg_toml = pkg.join("package.alta.toml");
		if pkg_toml.exists() && !pkg_toml.is_dir() {
			return Some(pkg);
		}

		if !pkg.pop() {
			return None
		}
	}
}

fn try_pkg_info(pkg: &Path) -> Option<Info> {
	let pkg_toml = pkg.join("package.alta.toml");
	if pkg_toml.exists() && !pkg_toml.is_dir() {
		Info::try_from_file(&pkg_toml)
	} else {
		None
	}
}

pub fn find_package(pkg_name: &str, starting_from: &Path) -> Option<PathBuf> {
	// see if the path itself is the target
	if let Some(info) = try_pkg_info(starting_from) {
		if info.name.eq(pkg_name) {
			return Some(starting_from.to_owned());
		}
	}

	let check_dir_entry = |child: &DirEntry| -> Option<PathBuf> {
		if child.file_name() == "alta-vendor" {
			// skip the vendored packages directory
			return None;
		}
		let file_type = match child.file_type() {
			Ok(x) => x,
			Err(_) => return None,
		};
		if !file_type.is_dir() {
			return None;
		}
		if let Some(info) = try_pkg_info(&child.path()) {
			if info.name.eq(pkg_name) {
				return Some(child.path());
			}
		}
		None
	};

	// check children
	if let Ok(iter) = starting_from.read_dir() {
		for child in iter {
			if let Ok(child) = child {
				if let Some(x) = check_dir_entry(&child) {
					return Some(x);
				}
			}
		}
	}

	// check vendored packages
	if let Ok(iter) = starting_from.join("alta-vendor").read_dir() {
		for child in iter {
			if let Ok(child) = child {
				if let Some(x) = check_dir_entry(&child) {
					return Some(x);
				}
			}
		}
	}

	// check siblings
	if let Some(iter) = starting_from.parent().and_then(|p| p.read_dir().ok()) {
		for child in iter {
			if let Ok(child) = child {
				if let Some(x) = starting_from.file_name() {
					if child.file_name() == x {
						// skip the directory we started from
						continue;
					}
				}
				if let Some(x) = check_dir_entry(&child) {
					return Some(x);
				}
			}
		}
	}

	None
}

pub fn resolve_import(source_path: &Path, import: &str) -> Option<PathBuf> {
	let src_pkg = match find_package_root(source_path) {
		Some(x) => x,
		None => return None,
	};
	let import_as_path = PathBuf::from(import);
	let handle_chrooted = |chrooted: PathBuf| -> Option<PathBuf> {
		if chrooted.exists() {
			if chrooted.is_dir() {
				if chrooted.join("main.alta").exists() {
					Some(chrooted.join("main.alta"))
				} else {
					None
				}
			} else {
				Some(chrooted)
			}
		} else if chrooted.with_extension("alta").exists() {
			Some(chrooted.with_extension("alta"))
		} else {
			None
		}
	};
	match import_as_path.components().next()? {
		Component::CurDir | Component::ParentDir => {
			// resolve this import within the current package relative to the source file
			let chrooted = source_path.parent().unwrap_or(&src_pkg).join(resolve_chrooted(import_as_path));
			handle_chrooted(chrooted)
		},
		Component::RootDir => {
			// resolve this import within the current package relative to the package root
			let chrooted = src_pkg.join(resolve_chrooted(import_as_path));
			handle_chrooted(chrooted)
		},
		Component::Normal(pkg_name) => {
			// resolve the rest of this path within the given package
			let pkg_name = pkg_name.to_string_lossy().to_string();
			let pkg_relative_path = import_as_path.strip_prefix(&pkg_name).unwrap();
			let pkg_path = find_package(&pkg_name, &src_pkg)?;
			let chrooted = pkg_path.join(resolve_chrooted(pkg_relative_path));
			handle_chrooted(chrooted)
		},
		_ => {
			// invalid import
			None
		}
	}
}
