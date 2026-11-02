#!/bin/bash
# Cut a release: bump the version, build, commit, tag, push and create the
# GitHub release with the given notes file.
#
# Usage:
#   ./release.sh <notes-file>            # patch release: 1.1.5 -> 1.1.6
#   ./release.sh <notes-file> minor      # minor release: 1.1.5 -> 1.2
#   ./release.sh <notes-file> patch --dry-run

set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NOTES="$1"
BUMP="${2:-patch}"
DRY="$3"

if [ -z "$NOTES" ] || [ ! -f "$NOTES" ]; then
    echo "usage: $0 <notes-file> [patch|minor] [--dry-run]" >&2
    exit 1
fi
if ! command -v gh >/dev/null 2>&1; then
    echo "gh (GitHub CLI) is required" >&2
    exit 1
fi

cd "$DIR"
if [ -n "$(git status --porcelain)" ]; then
    echo "Working tree is dirty, commit or stash first" >&2
    exit 1
fi

current="$(sed -n 's/^project(vlswm VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt)"
[ -n "$current" ] || { echo "Cannot read the version from CMakeLists.txt" >&2; exit 1; }
IFS=. read -r major minor patch <<<"$current"
case "$BUMP" in
minor) next="$major.$((minor + 1))" ;;
patch)
    if [ -n "$patch" ]; then
        next="$major.$minor.$((patch + 1))"
    else
        next="$major.$minor.1"
    fi
    ;;
*) echo "unknown bump '$BUMP', use patch or minor" >&2; exit 1 ;;
esac

echo "==> $current -> $next"
sed -i "s/project(vlswm VERSION $current /project(vlswm VERSION $next /" CMakeLists.txt
sed -i "s/#define VLSWM_VERSION \"$current\"/#define VLSWM_VERSION \"$next\"/" src/main.cpp src/vlswmstat.cpp
sed -i "s/\*\*Version $current\.\*\*/**Version $next.**/" README.md
sed -i "s/\*\*Версия $current\.\*\*/**Версия $next.**/" README.ru.md

echo "==> Building"
cmake -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build -j"$(nproc)" >/dev/null
./build/vlswm --version | grep -q "$next" || { echo "version mismatch" >&2; exit 1; }

if [ "$DRY" = "--dry-run" ]; then
    echo "==> Dry run: $next is built, nothing committed"
    git checkout -- CMakeLists.txt src/main.cpp src/vlswmstat.cpp README.md README.ru.md
    exit 0
fi

echo "==> Committing and tagging"
git add -A
git commit -q -m "Release $next"
git tag -a "v$next" -m "vlswm $next"
git push origin main
git push origin "v$next"

echo "==> Creating the GitHub release"
gh release create "v$next" --title "vlswm $next" --notes-file "$NOTES"
gh release view "v$next" --json url --jq .url
