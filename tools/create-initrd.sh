#!/bin/bash
# create-initrd.sh - Create a CPIO newc format initrd
#
# Usage: ./create-initrd.sh <output.cpio> [directory]
# If no directory specified, creates a sample initrd

OUTPUT="${1:-build/initrd.cpio}"
SRCDIR="${2:-}"

mkdir -p "$(dirname "$OUTPUT")"

# Get absolute path
OUTPUT_ABS="$(cd "$(dirname "$OUTPUT")" 2>/dev/null && pwd)/$(basename "$OUTPUT")"
if [ -z "$OUTPUT_ABS" ] || [ ! -d "$(dirname "$OUTPUT_ABS")" ]; then
    mkdir -p "$(dirname "$OUTPUT")"
    OUTPUT_ABS="$(cd "$(dirname "$OUTPUT")" && pwd)/$(basename "$OUTPUT")"
fi

if [ -n "$SRCDIR" ] && [ -d "$SRCDIR" ]; then
    # Create from specified directory
    (cd "$SRCDIR" && find . -print0 | cpio -o -H newc --null > "$OUTPUT_ABS" 2>/dev/null)
else
    # Create a sample initrd with test files
    TMPDIR=$(mktemp -d)
    trap "rm -rf '$TMPDIR'" EXIT

    # Create sample directory structure
    mkdir -p "$TMPDIR/bin"
    mkdir -p "$TMPDIR/etc"

    # Create sample files
    echo "#!/bin/sh" > "$TMPDIR/bin/init"
    echo "echo Hello from initrd!" >> "$TMPDIR/bin/init"
    chmod 755 "$TMPDIR/bin/init"

    echo "hostname=kurios" > "$TMPDIR/etc/hostname"
    echo "# Kurios OS configuration" > "$TMPDIR/etc/config"

    # Create CPIO archive
    (cd "$TMPDIR" && find . -print0 | cpio -o -H newc --null > "$OUTPUT_ABS" 2>/dev/null)
fi

OUTPUT="$OUTPUT_ABS"

INITRD_SIZE=$(stat -c%s "$OUTPUT")
echo "Created initrd: $OUTPUT ($INITRD_SIZE bytes)"
echo ""
echo "To build with this initrd:"
echo "  make INITRD_SIZE=$INITRD_SIZE -C boot"
echo "  make INITRD=$OUTPUT _image-bios"
