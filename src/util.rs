use std::path::{Path, PathBuf};

use path_clean::PathClean;

/// Clean/normalize a path, treating it as it were chrooted.
///
/// This performs the same functionality as `path_clean::clean`, except that it will treat
/// '..' at the beginning of the path the same way as if it were rooted/absolute (they will be eliminated).
/// e.g. `../foo/bar` becomes `foo/bar`.
///
/// Note that this will ALWAYS return the path uprooted! e.g. `/foo/bar` becomes `foo/bar`.
pub fn resolve_chrooted(path: impl AsRef<Path>) -> PathBuf {
	PathBuf::from("/").join(path).clean().strip_prefix("/").unwrap().to_owned()
}
