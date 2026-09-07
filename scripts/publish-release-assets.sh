#!/usr/bin/env bash

set -euo pipefail

if (( $# == 0 )); then
    printf 'Usage: %s ASSET...\n' "$0" >&2
    exit 2
fi

: "${GH_REPO:?GH_REPO must name the owner/repository}"
: "${TAG_NAME:?TAG_NAME must name the release tag}"

for asset in "$@"; do
    if [[ ! -f "$asset" ]]; then
        printf 'Release asset does not exist: %s\n' "$asset" >&2
        exit 2
    fi
done

release_json=$(mktemp)
query_error=$(mktemp)
download_dir=$(mktemp -d)
trap 'rm -f "$release_json" "$query_error"; rm -rf "$download_dir"' EXIT

release_endpoint="repos/${GH_REPO}/releases/tags/${TAG_NAME}"
if gh api "$release_endpoint" >"$release_json" 2>"$query_error"; then
    missing_assets=()

    for asset in "$@"; do
        asset_name=$(basename "$asset")
        asset_id=$(jq -r --arg name "$asset_name" \
            '[.assets[] | select(.name == $name) | .id] | if length == 1 then .[0] else empty end' \
            "$release_json")
        matching_count=$(jq -r --arg name "$asset_name" \
            '[.assets[] | select(.name == $name)] | length' "$release_json")

        if [[ "$matching_count" == "0" ]]; then
            missing_assets+=("$asset")
            continue
        fi
        if [[ "$matching_count" != "1" || -z "$asset_id" ]]; then
            printf "Release '%s' has an ambiguous asset named '%s'; refusing to modify it.\n" \
                "$TAG_NAME" "$asset_name" >&2
            exit 1
        fi

        published_asset="$download_dir/$asset_name"
        gh api \
            -H 'Accept: application/octet-stream' \
            "repos/${GH_REPO}/releases/assets/${asset_id}" >"$published_asset"
        if ! cmp -s "$asset" "$published_asset"; then
            printf "Release '%s' already has different content for '%s'; publish a new version instead.\n" \
                "$TAG_NAME" "$asset_name" >&2
            exit 1
        fi

        printf "Release asset '%s' is already published with identical content; skipping.\n" \
            "$asset_name"
    done

    if (( ${#missing_assets[@]} == 0 )); then
        printf "Release '%s' is already complete and identical.\n" "$TAG_NAME"
        exit 0
    fi

    printf "Release '%s' is missing %d asset(s); uploading only the missing files.\n" \
        "$TAG_NAME" "${#missing_assets[@]}"
    gh release upload "$TAG_NAME" "${missing_assets[@]}"
    exit 0
fi

if ! grep -Eq '\(HTTP 404\)|HTTP[^0-9]*404|status[^0-9]*404' "$query_error"; then
    printf "Could not query release '%s'; refusing to assume it does not exist.\n" "$TAG_NAME" >&2
    sed 's/^/gh: /' "$query_error" >&2
    exit 1
fi

printf "Release '%s' does not exist; creating it with immutable assets.\n" "$TAG_NAME"
release_notes="docs/releases/${TAG_NAME}.md"
if [[ -f "$release_notes" ]]; then
    gh release create "$TAG_NAME" "$@" \
        --verify-tag \
        --title "$TAG_NAME" \
        --notes-file "$release_notes"
else
    gh release create "$TAG_NAME" "$@" \
        --verify-tag \
        --title "$TAG_NAME" \
        --generate-notes
fi
