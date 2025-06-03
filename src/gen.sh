#!/bin/bash
set -e

rm -f compile_commands.json
tmp_files=""

for dir in threads userprog vm filesys; do
  echo "Processing $dir..."
  cd "$dir"
  make clean
  bear -- make
  tmp_file="../$dir.compile_commands.json"
  mv compile_commands.json "$tmp_file"
  tmp_files="$tmp_files $tmp_file"
  cd ..
done

# Merge all compile_commands.json files
jq -s 'add' $tmp_files > compile_commands.json

# Clean up
rm $tmp_files

echo "✅ Done: merged compile_commands.json in src/"
