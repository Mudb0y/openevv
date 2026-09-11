#!/usr/bin/env bash

set -eu

repoRoot=$(cd "$(dirname "$0")/.." && pwd)
helperPath=${repoRoot}/speechd/openevv-speechd-enable
testRoot=$(mktemp -d)
trap 'rm -rf "${testRoot}"' EXIT

configRoot=${testRoot}/config
configPath=${configRoot}/speechd.conf
marker='# OpenEVV module installed by openevv-git'
moduleLine='AddModule "openevv" "sd_openevv" "openevv.conf"'

mkdir -p "${configRoot}"

printf '%s\n' \
    '# Existing Speech Dispatcher configuration' \
    'DefaultRate 25' \
    '' \
    "${marker}" \
    "${moduleLine}" > "${configPath}"

migrationOutput=$(bash "${helperPath}" --config-dir "${configRoot}")
grep -Fq 'Warning: restarting Speech Dispatcher temporarily stops speech.' \
    <<< "${migrationOutput}"
grep -Fq 'Continue only from a session you can recover without hearing.' \
    <<< "${migrationOutput}"

if grep -Fqx "${marker}" "${configPath}" || \
        grep -Fqx "${moduleLine}" "${configPath}"; then
    printf '%s\n' 'Obsolete OpenEVV registration was not removed.' >&2
    exit 1
fi

grep -Fqx 'DefaultRate 25' "${configPath}"
[[ -f ${configPath}.openevv-git.explicit.bak ]]

configChecksum=$(sha256sum "${configPath}")
bash "${helperPath}" --config-dir "${configRoot}"
[[ $(sha256sum "${configPath}") == "${configChecksum}" ]]

manualRoot=${testRoot}/manual
manualPath=${manualRoot}/speechd.conf
manualLine='AddModule "openevv" "/home/Username/openevv/build/sd_openevv" "/home/Username/openevv/speechd/openevv.conf"'
mkdir -p "${manualRoot}"
printf '%s\n' '# Old documentation registration' "${manualLine}" > "${manualPath}"
manualOutput=$(bash "${helperPath}" --config-dir "${manualRoot}")
grep -Fq "Removing OpenEVV registration: ${manualLine}" <<< "${manualOutput}"
if grep -Fqx "${manualLine}" "${manualPath}"; then
    printf '%s\n' 'The old documentation registration was not removed.' >&2
    exit 1
fi

customRoot=${testRoot}/custom
customPath=${customRoot}/speechd.conf
customLine='AddModule "openevv" "/opt/speech/sd_custom_openevv" "custom.conf"'
mkdir -p "${customRoot}"
printf '%s\n' "${customLine}" > "${customPath}"
customChecksum=$(sha256sum "${customPath}")
bash "${helperPath}" --config-dir "${customRoot}"
grep -Fqx "${customLine}" "${customPath}"
[[ $(sha256sum "${customPath}") == "${customChecksum}" ]]

printf '%s\n' "${marker}" "${moduleLine}" > "${configPath}"
printf '%s\n' 'AddModule "custom" "sd_custom" "custom.conf"' >> "${configPath}"
mixedChecksum=$(sha256sum "${configPath}")
bash "${helperPath}" --config-dir "${configRoot}"
grep -Fqx "${marker}" "${configPath}"
grep -Fqx "${moduleLine}" "${configPath}"
[[ $(sha256sum "${configPath}") == "${mixedChecksum}" ]]

printf '%s\n' \
    'addmodule "custom" "sd_custom" "custom.conf"' \
    'addmodule "openevv" "sd_openevv" "openevv.conf"' > "${configPath}"
lowercaseChecksum=$(sha256sum "${configPath}")
bash "${helperPath}" --config-dir "${configRoot}"
[[ $(sha256sum "${configPath}") == "${lowercaseChecksum}" ]]

printf '%s\n' \
    '# Existing explicit module configuration' \
    'AddModule "custom" "sd_custom" "custom.conf"' > "${configPath}"
enableOutput=$(bash "${helperPath}" --config-dir "${configRoot}")
grep -Fq 'Warning: restarting Speech Dispatcher temporarily stops speech.' \
    <<< "${enableOutput}"
grep -Fq 'Continue only from a session you can recover without hearing.' \
    <<< "${enableOutput}"
grep -Fqx 'AddModule "custom" "sd_custom" "custom.conf"' "${configPath}"
grep -Fqx "${marker}" "${configPath}"
grep -Fqx "${moduleLine}" "${configPath}"
[[ -f ${configPath}.openevv.bak ]]

enabledChecksum=$(sha256sum "${configPath}")
bash "${helperPath}" --config-dir "${configRoot}"
[[ $(sha256sum "${configPath}") == "${enabledChecksum}" ]]

missingRoot=${testRoot}/missing
bash "${helperPath}" --config-dir "${missingRoot}"

if [[ -e ${missingRoot}/speechd.conf ]]; then
    printf '%s\n' 'The helper created a configuration that disables autodetection.' >&2
    exit 1
fi

bash "${helperPath}" --help >/dev/null

if bash "${helperPath}" --config-dir >/dev/null 2>&1; then
    printf '%s\n' 'A missing --config-dir argument was accepted.' >&2
    exit 1
fi

if bash "${helperPath}" --not-an-option >/dev/null 2>&1; then
    printf '%s\n' 'An unknown option was accepted.' >&2
    exit 1
fi

if (( EUID != 0 )) && bash "${helperPath}" --system >/dev/null 2>&1; then
    printf '%s\n' 'A non-root system migration was accepted.' >&2
    exit 1
fi

printf '%s\n' 'Speech Dispatcher autodetection migration passed.'
