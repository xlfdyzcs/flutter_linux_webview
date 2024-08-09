(This file may be `flutter_linux_webview/third_party_base/README.md` itself,
or a copy or symlink of it.)

# About `third_party/` directories in the repository

`flutter_linux_webview` incorporates third-party software materials.

Third-party source code is organized under several directories named `third_party/` in the repository.

If this package is a Git repository, each third-party file exists as a symlink to a file in the `//third_party_base/` directory.
However, if this package is downloaded from Pub, these symlinks are converted to regular files, and the `//third_party_base/` directory does not exist.


## `//third_party_base/`

To centralize third-party source code and prevent scattering throughout the repository, we store them under `//third_party_base/` for plugin development.

When we need to use these third-party source files, we create symlinks at the respective locations. We do not directly reference `//third_party_base/` in our source code.

This approach is necessary due to the following restrictions:

* Dart source code cannot access directories outside of lib/ due to Dart limitations.
* Pub restricts the upload of packages with symlinks to directories (symlinks to files are allowed, but they are converted to regular files upon upload).

For consistency, we apply the same approach to native source code as well as Dart code. (However, files in `//linux/` can directly access `//third_party_base/` with CMake configuration).

The creation of symlinks is performed by the `//create_symlinks_to_third_party.sh` script.
