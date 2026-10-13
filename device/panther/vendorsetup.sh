#!/usr/bin/env bash
#
# Copyright (C) 2023-2024 The Halium Project
#
# Lunch combos and build helper scripts for Google Pixel 7 (panther / pantah)
#

add_lunch_combo halium_panther-userdebug
add_lunch_combo halium_panther-user
add_lunch_combo halium_panther-eng

# Helper function: Quick build Halium boot & system for panther
function mkh_panther() {
    echo "==> Setting up build environment for Google Pixel 7 (panther)..."
    export USE_CCACHE=1
    export CCACHE_EXEC=$(which ccache)
    export WITHOUT_CHECK_API=true
    
    lunch halium_panther-userdebug
    m -j$(nproc) halium-boot systemimage
}

# Helper function: Package flashable zip from current build tree
function pkg_panther() {
    local OUT_DIR="out/target/product/panther"
    if [ -f "$OUT_DIR/boot.img" ] && [ -f "$OUT_DIR/system.img" ]; then
        echo "==> Creating flashable image package for panther..."
        mkdir -p out/panther-release
        cp -v $OUT_DIR/boot.img out/panther-release/halium-boot.img
        cp -v $OUT_DIR/system.img out/panther-release/system.img
        cp -v $OUT_DIR/vendor.img out/panther-release/vendor.img || true
        echo "Release files ready in out/panther-release/"
    else
        echo "Error: Output images not found in $OUT_DIR. Run mkh_panther first."
    fi
}
