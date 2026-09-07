#!/usr/bin/env bash

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
publisher="$repo_root/scripts/publish-release-assets.sh"
test_root=$(mktemp -d)
trap 'rm -rf "$test_root"' EXIT

make_case()
{
    case_dir="$test_root/$1"
    mkdir -p "$case_dir/bin" "$case_dir/dist" "$case_dir/published"
    printf 'zip-content\n' >"$case_dir/dist/plugin.zip"
    printf 'checksum-content\n' >"$case_dir/dist/plugin.zip.sha256"
}

write_mock_gh()
{
    printf '#!/usr/bin/env bash\nset -euo pipefail\n%s\n' "$1" >"$case_dir/bin/gh"
    chmod +x "$case_dir/bin/gh"
}

run_publisher()
{
    (
        cd "$repo_root"
        PATH="$case_dir/bin:$PATH" \
            MOCK_ROOT="$case_dir" \
            GH_REPO="owner/repo" \
            TAG_NAME="${TEST_TAG_NAME:-v1.2.3}" \
            "$publisher" "$case_dir/dist/plugin.zip" "$case_dir/dist/plugin.zip.sha256"
    )
}

assert_log()
{
    grep -Fx "$1" "$case_dir/calls.log" >/dev/null
}

make_case create
write_mock_gh '
printf "%s\n" "$*" >>"$MOCK_ROOT/calls.log"
if [[ "$1 $2" == "api repos/owner/repo/releases/tags/v1.2.3" ]]; then
    printf "gh: release not found (HTTP 404)\n" >&2
    exit 1
fi
[[ "$1 $2" == "release create" ]]'
run_publisher
assert_log "release create v1.2.3 $case_dir/dist/plugin.zip $case_dir/dist/plugin.zip.sha256 --verify-tag --title v1.2.3 --generate-notes"

make_case create_with_notes
write_mock_gh '
printf "%s\n" "$*" >>"$MOCK_ROOT/calls.log"
if [[ "$1 $2" == "api repos/owner/repo/releases/tags/v0.2.8" ]]; then
    printf "gh: release not found (HTTP 404)\n" >&2
    exit 1
fi
[[ "$1 $2" == "release create" ]]'
TEST_TAG_NAME=v0.2.8 run_publisher
assert_log "release create v0.2.8 $case_dir/dist/plugin.zip $case_dir/dist/plugin.zip.sha256 --verify-tag --title v0.2.8 --notes-file docs/releases/v0.2.8.md"

make_case identical
cp "$case_dir/dist/plugin.zip" "$case_dir/published/101"
cp "$case_dir/dist/plugin.zip.sha256" "$case_dir/published/102"
write_mock_gh '
printf "%s\n" "$*" >>"$MOCK_ROOT/calls.log"
if [[ "$1" == "api" && "$2" == "repos/owner/repo/releases/tags/v1.2.3" ]]; then
    printf "{\"assets\":[{\"name\":\"plugin.zip\",\"id\":101},{\"name\":\"plugin.zip.sha256\",\"id\":102}]}"
elif [[ "$1" == "api" ]]; then
    asset_id=${*: -1}; asset_id=${asset_id##*/}; cat "$MOCK_ROOT/published/$asset_id"
else
    exit 9
fi'
run_publisher
if grep -Eq '^release (upload|create)' "$case_dir/calls.log"; then exit 1; fi

make_case missing
cp "$case_dir/dist/plugin.zip" "$case_dir/published/101"
write_mock_gh '
printf "%s\n" "$*" >>"$MOCK_ROOT/calls.log"
if [[ "$1" == "api" && "$2" == "repos/owner/repo/releases/tags/v1.2.3" ]]; then
    printf "{\"assets\":[{\"name\":\"plugin.zip\",\"id\":101}]}"
elif [[ "$1" == "api" ]]; then
    cat "$MOCK_ROOT/published/101"
elif [[ "$1 $2" == "release upload" ]]; then
    exit 0
else
    exit 9
fi'
run_publisher
assert_log "release upload v1.2.3 $case_dir/dist/plugin.zip.sha256"

make_case conflict
printf 'different\n' >"$case_dir/published/101"
cp "$case_dir/dist/plugin.zip.sha256" "$case_dir/published/102"
write_mock_gh '
if [[ "$1" == "api" && "$2" == "repos/owner/repo/releases/tags/v1.2.3" ]]; then
    printf "{\"assets\":[{\"name\":\"plugin.zip\",\"id\":101},{\"name\":\"plugin.zip.sha256\",\"id\":102}]}"
elif [[ "$1" == "api" ]]; then
    asset_id=${*: -1}; asset_id=${asset_id##*/}; cat "$MOCK_ROOT/published/$asset_id"
else
    printf "%s\n" "$*" >>"$MOCK_ROOT/calls.log"
    exit 9
fi'
if run_publisher 2>"$case_dir/error.log"; then exit 1; fi
grep -F "already has different content" "$case_dir/error.log" >/dev/null
if [[ -f "$case_dir/calls.log" ]]; then exit 1; fi

make_case query_error
write_mock_gh '
if [[ "$1" == "api" ]]; then
    printf "gh: API rate limit exceeded (HTTP 403)\n" >&2
    exit 1
fi
printf "%s\n" "$*" >>"$MOCK_ROOT/calls.log"'
if run_publisher 2>"$case_dir/error.log"; then exit 1; fi
grep -F "refusing to assume it does not exist" "$case_dir/error.log" >/dev/null
if [[ -f "$case_dir/calls.log" ]]; then exit 1; fi

printf 'release publishing tests: passed\n'
