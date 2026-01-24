#!/bin/bash
# Verify the Kurios2 toolchain is properly set up

echo "=== Kurios2 Toolchain Verification ==="
echo ""

ERRORS=0

check_tool() {
    local name=$1
    local cmd=$2
    printf "%-20s" "$name:"
    if command -v $cmd &> /dev/null; then
        version=$($cmd --version 2>&1 | head -1)
        echo "OK - $version"
        return 0
    else
        echo "MISSING"
        return 1
    fi
}

# Required tools
echo "Required tools:"
check_tool "GCC" "gcc" || ((ERRORS++))
check_tool "G++" "g++" || ((ERRORS++))
check_tool "NASM" "nasm" || ((ERRORS++))
check_tool "LD" "ld" || ((ERRORS++))
check_tool "Make" "make" || ((ERRORS++))
check_tool "Objcopy" "objcopy" || ((ERRORS++))

echo ""
echo "Optional tools (for ISO/testing):"
check_tool "QEMU" "qemu-system-x86_64" || echo "  (Install for testing: sudo apt install qemu-system-x86)"
check_tool "xorriso" "xorriso" || echo "  (Install for ISO: sudo apt install xorriso)"
check_tool "grub-mkrescue" "grub-mkrescue" || echo "  (Install for ISO: sudo apt install grub-pc-bin grub-common)"

echo ""
echo "Cross-compiler (optional):"
if command -v x86_64-elf-gcc &> /dev/null; then
    check_tool "x86_64-elf-gcc" "x86_64-elf-gcc"
else
    echo "x86_64-elf-gcc:     Not installed (using system GCC with freestanding flags)"
fi

echo ""
echo "=== Verification Summary ==="
if [ $ERRORS -eq 0 ]; then
    echo "All required tools are available!"
    echo "Run 'nasm --version' to verify NASM is installed."
    exit 0
else
    echo "Missing $ERRORS required tool(s)."
    echo ""
    echo "Install missing tools:"
    echo "  sudo apt-get install build-essential nasm"
    exit 1
fi
