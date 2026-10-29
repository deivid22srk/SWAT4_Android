#!/bin/sh
# Uploads gen/ (recompiled C derived from the game) to the PRIVATE codegen
# repository as a release asset. The public repository must never receive it.
#
#   GEN_REPO=deivid22srk/SWAT4_codegen GEN_TAG=gen-v1 tools/push_gen.sh
#
# Requires: gh, zstd, tar. The authenticated account needs write access to
# GEN_REPO. The script refuses to push unless GEN_REPO is private.
set -eu

GEN_REPO=${GEN_REPO:-deivid22srk/SWAT4_codegen}
GEN_TAG=${GEN_TAG:-gen-v1}

[ -d gen ] || { echo "no gen/ here; run tools/regen_gen.sh first" >&2; exit 1; }
[ -n "$(find -L gen -name '*.c' -print -quit)" ] || { echo "gen/ has no .c files" >&2; exit 1; }

# Refuse anything that is not a private repository. If the lookup fails, stop:
# we cannot prove the destination is private, and a wrong push is irreversible.
priv=$(gh repo view "$GEN_REPO" --json isPrivate -q .isPrivate 2>/dev/null || true)
if [ "$priv" != "true" ]; then
    echo "refusing to push: $GEN_REPO is not a private repository (isPrivate='${priv}')" >&2
    echo "create it with: gh repo create $GEN_REPO --private" >&2
    exit 1
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
tar --zstd -cf "$tmp/gen.tar.zst" -C gen .
echo "archive: $(du -h "$tmp/gen.tar.zst" | cut -f1), $(find -L gen -name '*.c' | wc -l) C files"
sha256sum "$tmp/gen.tar.zst"

gh release view "$GEN_TAG" --repo "$GEN_REPO" >/dev/null 2>&1 || \
    gh release create "$GEN_TAG" --repo "$GEN_REPO" \
        --title "gen $GEN_TAG" \
        --notes "Recompiled C derived from the game. Private on purpose: never publish." \
        --prerelease
gh release upload "$GEN_TAG" "$tmp/gen.tar.zst" --repo "$GEN_REPO" --clobber
echo "uploaded gen.tar.zst to $GEN_REPO@$GEN_TAG (private)"