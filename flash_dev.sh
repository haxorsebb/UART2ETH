#!/bin/bash
set -e

# Configuration
BINARY="build/uart2eth_ota.bin"
PT_SLOTS="partition_table_slots.bin"
OPENOCD_DIR="$HOME/.pico-sdk/openocd/0.12.0+dev"  # Use $HOME instead of ~
OPENOCD_INTERFACE="interface/cmsis-dap.cfg"
OPENOCD_TARGET="target/rp2350.cfg"
OPENOCD_SCRIPT="flash_dev.tcl"
OPENOCD_SCRIPT_DIR="$OPENOCD_DIR/scripts"

# Check OpenOCD directory exists
if [ ! -d "$OPENOCD_DIR" ]; then
    echo "❌ OpenOCD directory not found: $OPENOCD_DIR"
    echo "Checking common locations..."
    
    # Try to find openocd
    if command -v openocd &> /dev/null; then
        OPENOCD_BIN=$(which openocd)
        OPENOCD_DIR=$(dirname "$OPENOCD_BIN")
        OPENOCD_SCRIPT_DIR="$OPENOCD_DIR/../share/openocd/scripts"
        echo "✓ Found OpenOCD at: $OPENOCD_BIN"
    else
        echo "❌ OpenOCD not found in PATH"
        exit 1
    fi
fi

# Check files exist
if [ ! -f "$BINARY" ]; then
    echo "❌ Binary not found: $BINARY"
    echo "Run 'cmake --build build' first"
    exit 1
fi

if [ ! -f "$PT_SLOTS" ]; then
    echo "❌ Partition table slots binary not found: $PT_SLOTS"
    echo "Run setup steps to create it"
    exit 1
fi

if [ ! -f "$OPENOCD_SCRIPT" ]; then
    echo "❌ OpenOCD script not found: $OPENOCD_SCRIPT"
    exit 1
fi

# Flash
echo "🚀 Flashing development build..."
echo "OpenOCD: $OPENOCD_DIR/openocd"
echo "Scripts: $OPENOCD_SCRIPT_DIR"

"$OPENOCD_DIR/openocd" -s "$OPENOCD_SCRIPT_DIR" \
        -f "$OPENOCD_INTERFACE" \
        -f "$OPENOCD_TARGET" \
        -f "$OPENOCD_SCRIPT"

echo "✅ Flash complete!"

