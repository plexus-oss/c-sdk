#!/usr/bin/env bash
set -euo pipefail

if [ $# -ne 1 ]; then
  echo "Usage: $0 <version>"
  echo "Example: $0 0.6.0"
  exit 1
fi

VERSION="$1"

# Validate format
if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Error: Version must be in X.Y.Z format"
  exit 1
fi

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

echo "Bumping version to $VERSION in all files..."

# 1. src/plexus.h
sed -i.bak "s/#define PLEXUS_SDK_VERSION \".*\"/#define PLEXUS_SDK_VERSION \"$VERSION\"/" "$REPO_ROOT/src/plexus.h"
rm -f "$REPO_ROOT/src/plexus.h.bak"
echo "  Updated src/plexus.h"

# 2. CMakeLists.txt
sed -i.bak "s/project(plexus-sdk VERSION .* LANGUAGES/project(plexus-sdk VERSION $VERSION LANGUAGES/" "$REPO_ROOT/CMakeLists.txt"
rm -f "$REPO_ROOT/CMakeLists.txt.bak"
echo "  Updated CMakeLists.txt"

# 3. library.json
sed -i.bak "s/\"version\": \".*\"/\"version\": \"$VERSION\"/" "$REPO_ROOT/library.json"
rm -f "$REPO_ROOT/library.json.bak"
echo "  Updated library.json"

# 4. library.properties
sed -i.bak "s/^version=.*/version=$VERSION/" "$REPO_ROOT/library.properties"
rm -f "$REPO_ROOT/library.properties.bak"
echo "  Updated library.properties"

echo ""
echo "Done! Version bumped to $VERSION in all 4 files."
echo "Next steps:"
echo "  1. git add -A && git commit -m \"bump version to $VERSION\""
echo "  2. Open a PR and merge"
echo "  3. Create a GitHub Release tagged v$VERSION"
