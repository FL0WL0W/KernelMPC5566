#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 5 ]]; then
    echo "Usage: $0 <bootloader.bin> <application.bin> <output.bin> <application-offset> <flash-size>" >&2
    exit 2
fi

bootloader_path=$1
application_path=$2
output_path=$3
application_offset=$(( $4 ))
flash_size=$(( $5 ))

if [[ ! -f "$bootloader_path" ]]; then
    echo "Bootloader image not found: $bootloader_path" >&2
    exit 1
fi

if [[ ! -f "$application_path" ]]; then
    echo "Application image not found: $application_path" >&2
    exit 1
fi

bootloader_size=$(stat -c %s "$bootloader_path")
application_size=$(stat -c %s "$application_path")
application_end=$((application_offset + application_size))
application_region_size=$((flash_size - application_offset))

if (( bootloader_size != application_offset )); then
    printf 'Bootloader is 0x%X bytes; expected exactly 0x%X bytes\n' \
        "$bootloader_size" "$application_offset" >&2
    exit 1
fi

if (( application_end > flash_size )); then
    printf 'Application ends at 0x%X, beyond flash size 0x%X\n' \
        "$application_end" "$flash_size" >&2
    exit 1
fi

if (( application_size != application_region_size )); then
    printf 'Application is 0x%X bytes; expected the complete 0x%X-byte region with its export footer\n' \
        "$application_size" "$application_region_size" >&2
    exit 1
fi

footer_hex=$(od -An -tx1 -N24 -j $((application_size - 24)) \
    "$application_path" | tr -d ' \n')
for callback_index in 0 1 2 3; do
    callback_word=${footer_hex:$((callback_index * 8)):8}
    if [[ "$callback_word" == "00000000" || "$callback_word" == "ffffffff" ]]; then
        printf 'Invalid bootloader callback %d in application footer: %s\n' \
            "$callback_index" "$callback_word" >&2
        exit 1
    fi
    callback_address=$((16#$callback_word))
    if (( callback_address < application_offset ||
          callback_address >= flash_size - 24 )); then
        printf 'Bootloader callback %d points outside application code: 0x%08X\n' \
            "$callback_index" "$callback_address" >&2
        exit 1
    fi
done
if [[ "${footer_hex:32:4}" != "55aa" ]]; then
    echo "Application footer is missing the 55AA validity marker" >&2
    exit 1
fi

if [[ "$output_path" == "$bootloader_path" || "$output_path" == "$application_path" ]]; then
    echo "Output path must not overwrite an input image" >&2
    exit 1
fi

# Begin with an erased flash image, then place each component at its address.
head -c "$flash_size" /dev/zero | tr '\000' '\377' > "$output_path"
dd if="$bootloader_path" of="$output_path" conv=notrunc status=none
dd if="$application_path" of="$output_path" bs=1 seek="$application_offset" \
    conv=notrunc status=none

output_size=$(stat -c %s "$output_path")
if (( output_size != flash_size )); then
    printf 'Generated image is 0x%X bytes; expected 0x%X bytes\n' \
        "$output_size" "$flash_size" >&2
    exit 1
fi

printf 'Built %s: bootloader 0x%X bytes, application 0x%X bytes, total 0x%X bytes\n' \
    "$output_path" "$bootloader_size" "$application_size" "$output_size"
