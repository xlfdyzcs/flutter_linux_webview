#!/bin/bash

set -e

# flutter_linux_webview/
PROJECT_ROOT_DIR="$(realpath "$(dirname "$0")")"

cd "$(dirname "$0")" || exit

# $1: src_root_dir (example: "third_party_base/cef")
# $2: files_to_link (one line per file)
# $3: dst_root_dir (example: "linux/third_party/cef")
#
# For each $files_to_link under $src_root_dir, creates symlinks to them under
# $dst_root_dir.
#
# Example:
#   $1: "third_party_base/some_oss"
#   $2: "file1
#        dir1/file2
#        dir2/file5"
#   $3: "linux/third_party/some_oss"
#
#  flutter_linux_webview/
#  └── third_party_base/
#      ├── some_oss/  # $src_root_dir
#      │   ├── file1
#      │   ├── dir1/
#      │   │   ├── file2  # specified in $files_to_link
#      │   │   └── file3
#      │   └── dir2/
#      │       ├── file4
#      │       └── file5  # specified in $files_to_link
#      ├── linux/
#      │   └── third_party/  # must exist
#      │ ================ (begin) symlinks and directories are created as follows ===============
#      │       └── some_oss/  # $dst_root_dir
#      │           ├── file1 (-> ../../../third_party_base/some_oss/file1)
#      │           ├── dir1/
#      │           │   └── file2 (-> ../../../../third_party_base/some_oss/dir1/file2)
#      │           └── dir2/
#      │               └── file5 (-> ../../../../third_party_base/some_oss/dir2/file5)
#      │ ================ (end) ================
#      └── ...
function copy_directory_structure_and_create_symlinks_for_files() {
    local src_root_dir;  src_root_dir="$PROJECT_ROOT_DIR"/"$1"
    local files_to_link;  files_to_link="$2"
    local dst_root_dir;  dst_root_dir="$PROJECT_ROOT_DIR"/"$3"

    if [ ! -d "$src_root_dir" ]; then
        echo "Error: Source directory '$src_root_dir' does not exist."
        exit 1
    fi

    if [ -e "$dst_root_dir" ]; then
        echo "Warning: '$dst_root_dir' already exists."
        echo "Do you want to delete this directory and its contents? (y/n)"
        read -r answer
        if [ "$answer" != "y" ]; then
            echo "Abort."
            exit 1
        fi
        rm -rf "$dst_root_dir"
    fi

    mkdir -p "$dst_root_dir" || {
        echo "Error: Failed to create directory '$dst_root_dir'."
        exit 1
    }

    for file_rel in $files_to_link; do
      _create_symlink_with_mkdir "$src_root_dir" "$file_rel" "$dst_root_dir" || {
        exit 1
      }
    done
}

function _create_symlink_with_mkdir() {
    local src_root_dir;  src_root_dir="$1"
    local rel_filepath;  rel_filepath="$2"
    local dst_root_dir;  dst_root_dir="$3"

    local abs_src_file_path;  abs_src_file_path="$(realpath "$src_root_dir/$rel_filepath")"
    local abs_dst_file_path;  abs_dst_file_path="$(realpath --canonicalize-missing "$dst_root_dir/$rel_filepath")"
    local dst_file_dirname;  dst_file_dirname="$(dirname "$abs_dst_file_path")"
    local rel_path_for_symlink;  rel_path_for_symlink=$(realpath "$abs_src_file_path" --canonicalize-missing --relative-to="$dst_file_dirname")

    mkdir -p "$(dirname "$abs_dst_file_path")" || {
        echo "Error: Failed to create directory '$dst_file_dirname'."
        exit 1
    }
    ln -sv "$rel_path_for_symlink" "$abs_dst_file_path" || {
        echo "Error: Failed to create symbolic link."
        echo "File: '$abs_src_file_path' could not be symbolically linked to '$abs_dst_file_path'."
        exit 1
    }
}


# $1: target_dir (example: "lib/src/third_party")
#
# Creates a directory named $target_dir as a third-party top directory.
function create_third_party_dir() {
  local target_dir;  target_dir="$PROJECT_ROOT_DIR"/"$1"

  if [ -e "$target_dir" ]; then
      echo "Warning: '$target_dir' already exists."
      echo "Do you want to delete this directory and its contents? (y/n)"
      read -r answer
      if [ "$answer" != "y" ]; then
          echo "Abort."
          exit 1
      fi
      rm -rf "$target_dir"
  fi

  mkdir -p "$target_dir"
  _create_symlink_with_mkdir "third_party_base" "README.md" "$target_dir" 
}

#
# Generates ThirdPartyNotices.txt from a list of README.webview
#
# $1: the list of README.webview file paths
function generate_third_party_notices_txt_from_README_webviews() {
  local list_of_readme_webview;  list_of_readme_webview=$1

  for readme_path in $list_of_readme_webview; do
    grep "^Name: " "$readme_path" || { echo "${readme_path} must have 'Name: '"; exit 1; }
    grep "^URL: " "$readme_path" || { echo "${readme_path} must have 'URL: '"; exit 1; }
    grep "^License File: " "$readme_path" || { echo "${readme_path} must have 'License File: '"; exit 1; }
  done

  local tmp_file
  tmp_file=`mktemp tmp_third_party_notices_XXXXXX`

  cat <<EOS >> "$tmp_file"
Third-Party Software Notices

flutter_linux_webview incorporates materials from third-party software as listed
below.

EOS

  for readme_path in $list_of_readme_webview; do
    local oss_name;  oss_name=$(grep "^Name: " "$readme_path" | sed -e 's/^Name: \(.*\)$/\1/')
    local oss_url;  oss_url=$(grep "^URL: " "$readme_path" | sed -e 's/^URL: \(.*\)$/\1/')
    local oss_license_relpath;  oss_license_relpath=$(grep "^License File: " "$readme_path" | sed -e 's/^License File: \(.*\)$/\1/')
    local oss_license_abspath;  oss_license_abspath="$(dirname "$( realpath "$readme_path" )")/${oss_license_relpath}"

    cat <<EOS >> "$tmp_file"
--------------------------------------------------------------------------------

${oss_name}
${oss_url}

$(cat "$oss_license_abspath")
--------------------------------------------------------------------------------

EOS
  done

  mv "$tmp_file" "./ThirdPartyNotices.txt"
}

###
### Dart side third-party
###

create_third_party_dir "lib/src/third_party"

# cef

param_src_root_dir="third_party_base/cef"
param_dst_root_dir="lib/src/third_party/cef"
param_files_to_link="README.webview
src/LICENSE.txt
src/include/internal/cef_types.dart
src/tests/cefclient/browser/windows_key_code.dart
"
copy_directory_structure_and_create_symlinks_for_files "$param_src_root_dir"  "$param_files_to_link" "$param_dst_root_dir"


# flutter_engine

param_src_root_dir="third_party_base/flutter_engine"
param_dst_root_dir="lib/src/third_party/flutter_engine"
param_files_to_link="README.webview
LICENSE
shell/platform/linux/native_key_code.dart
"
copy_directory_structure_and_create_symlinks_for_files "$param_src_root_dir"  "$param_files_to_link" "$param_dst_root_dir"


# webview_flutter_android

param_src_root_dir="third_party_base/webview_flutter_android"
param_dst_root_dir="lib/src/third_party/webview_flutter_android"
param_files_to_link="README.webview
LICENSE
lib/src/instance_manager.dart
lib/webview_linux_cookie_manager.dart
"
copy_directory_structure_and_create_symlinks_for_files "$param_src_root_dir"  "$param_files_to_link" "$param_dst_root_dir"


###
### Platform (C++) side third-party
###

create_third_party_dir "linux/third_party"

# cef

param_src_root_dir="third_party_base/cef"
param_dst_root_dir="linux/third_party/cef"
param_files_to_link="README.webview
src/LICENSE.txt
src/tests/cefclient/browser/flutter_webview_osr_renderer.cc
src/tests/cefclient/browser/flutter_webview_osr_renderer.h
src/tests/cefsimple/CMakeLists_subprocess_project.txt
"
copy_directory_structure_and_create_symlinks_for_files "$param_src_root_dir"  "$param_files_to_link" "$param_dst_root_dir"


# webview_flutter
# * for the example app and the integration_test

create_third_party_dir "example/lib/third_party"

param_src_root_dir="third_party_base/webview_flutter"
param_dst_root_dir="example/lib/third_party/webview_flutter"
param_files_to_link="README.webview
LICENSE
example/assets/www/index.html
example/assets/www/styles/style.css
example/integration_test/flutter_linux_webview_test.dart
example/lib/main.dart
"
copy_directory_structure_and_create_symlinks_for_files "$param_src_root_dir"  "$param_files_to_link" "$param_dst_root_dir"

generate_third_party_notices_txt_from_README_webviews "third_party_base/cef/README.webview
third_party_base/flutter_engine/README.webview
third_party_base/webview_flutter_android/README.webview
third_party_base/webview_flutter/README.webview
"
