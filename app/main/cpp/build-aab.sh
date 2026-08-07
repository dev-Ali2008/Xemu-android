#!/usr/bin/env bash

set -euo pipefail

project_source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
android_dir="${project_source_dir}/android"
key_props="${android_dir}/key.properties"
release_aab="${android_dir}/app/build/outputs/bundle/release/app-release.aab"
store_file=""

require_cmd() {
  local cmd="$1"

  if ! command -v "${cmd}" >/dev/null 2>&1; then
    printf '\nMissing required tool: %s\n' "${cmd}"
    printf 'Install the Android toolchain prerequisites listed in %s.\n' "${android_dir}/README.md"
    exit 1
  fi
}

if [[ ! -f "${key_props}" ]]; then
  printf 'Creating "%s" with placeholders...\n' "${key_props}"
  cat >"${key_props}" <<'EOF'
storeFile=/path/to/your-release-key.jks
storePassword=REPLACE_ME
keyAlias=REPLACE_ME
keyPassword=REPLACE_ME
EOF
  printf '\nEdit %s with your real values, then rerun this script.\n' "${key_props}"
  exit 1
fi

if grep -Fq 'REPLACE_ME' "${key_props}"; then
  printf '\nPlease replace the placeholders in %s before building.\n' "${key_props}"
  exit 1
fi

if grep -Fq '/path/to/your-release-key.jks' "${key_props}" || \
  grep -Fq 'C:/path/to/your-release-key.jks' "${key_props}"; then
  printf '\nPlease replace the storeFile placeholder in %s before building.\n' "${key_props}"
  exit 1
fi

store_file="$(sed -n 's/^storeFile=//p' "${key_props}" | head -n 1)"
if [[ -z "${store_file}" ]]; then
  printf '\nMissing storeFile in %s.\n' "${key_props}"
  exit 1
fi

if [[ ! -f "${store_file}" ]]; then
  printf '\nKeystore file not found: %s\n' "${store_file}"
  printf 'Update storeFile in %s to point at your release keystore.\n' "${key_props}"
  exit 1
fi

for cmd in java cmake meson ninja cargo; do
  require_cmd "${cmd}"
done

cd "${android_dir}"
bash ./gradlew bundleRelease

printf '\nBuild complete.\n'
printf 'Release AAB:\n'
printf '  %s\n' "${release_aab}"
