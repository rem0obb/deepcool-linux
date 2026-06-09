#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-0.0.1}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="$ROOT/dist"
BUILD="$ROOT/build/package"
APP_ID="unnoficial.deepcool.digital.linux"
APP_NAME="deepcool-digital-linux"
ARCH_DEB="amd64"

LINUXDEPLOY="${LINUXDEPLOY:-/tmp/linuxdeploy-x86_64.AppImage}"
APPIMAGETOOL="${APPIMAGETOOL:-/tmp/appimagetool-x86_64.AppImage}"

mkdir -p "$DIST" "$BUILD"
rm -rf "$BUILD/deb" "$BUILD/AppDir"

make -C "$ROOT" clean
make -C "$ROOT"
strip "$ROOT/$APP_NAME" || true

install_icon() {
    local target="$1"
    install -Dm644 "$ROOT/assets/hicolor/256x256/apps/deepcool-digital-linux.png" \
        "$target/usr/share/icons/hicolor/256x256/apps/deepcool-digital-linux.png"
}

install_desktop() {
    local target="$1"
    install -Dm644 "$ROOT/packaging/deepcool-digital-linux.desktop" \
        "$target/usr/share/applications/$APP_ID.desktop"
}

build_deb() {
    local pkg="$BUILD/deb"
    mkdir -p "$pkg/DEBIAN"
    install -Dm755 "$ROOT/$APP_NAME" "$pkg/usr/bin/$APP_NAME"
    install_icon "$pkg"
    install_desktop "$pkg"

    cat > "$pkg/DEBIAN/control" <<EOF
Package: deepcool-digital-linux
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH_DEB
Maintainer: rem0obb <mobhacking100@gmail.com>
Depends: libc6, libgtk-4-1, libglib2.0-0, libhidapi-hidraw0, libpango-1.0-0, libcairo2, libgdk-pixbuf-2.0-0, libgraphene-1.0-0, policykit-1, systemd
Description: Unofficial DeepCool Digital control panel for Linux
 GTK control panel for DeepCool Digital USB/HID devices.
EOF

    dpkg-deb --build --root-owner-group "$pkg" \
        "$DIST/deepcool-digital-linux_${VERSION}_${ARCH_DEB}.deb"
}

build_appdir() {
    local appdir="$BUILD/AppDir"
    install -Dm755 "$ROOT/$APP_NAME" "$appdir/usr/bin/$APP_NAME"
    install_icon "$appdir"
    install_desktop "$appdir"

    cat > "$appdir/AppRun" <<'EOF'
#!/usr/bin/env sh
HERE="$(dirname "$(readlink -f "$0")")"
export PATH="$HERE/usr/bin:$PATH"
exec "$HERE/usr/bin/deepcool-digital-linux" "$@"
EOF
    chmod +x "$appdir/AppRun"

    cp "$ROOT/packaging/deepcool-digital-linux.desktop" "$appdir/$APP_ID.desktop"
    cp "$ROOT/assets/hicolor/256x256/apps/deepcool-digital-linux.png" "$appdir/deepcool-digital-linux.png"
}

build_appimage() {
    local appdir="$BUILD/AppDir"
    if [[ -x "$LINUXDEPLOY" ]]; then
        APPIMAGE_EXTRACT_AND_RUN=1 "$LINUXDEPLOY" --appdir "$appdir" \
            --executable "$appdir/usr/bin/$APP_NAME" \
            --desktop-file "$appdir/usr/share/applications/$APP_ID.desktop" \
            --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/deepcool-digital-linux.png" \
            || true
    fi

    if [[ ! -x "$APPIMAGETOOL" ]]; then
        echo "appimagetool not found at $APPIMAGETOOL" >&2
        echo "Download it or set APPIMAGETOOL=/path/to/appimagetool-x86_64.AppImage" >&2
        return 1
    fi

    APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 "$APPIMAGETOOL" "$appdir" \
        "$DIST/DeepCool-Digital-Linux-${VERSION}-x86_64.AppImage"
}

build_deb
build_appdir
build_appimage

make -C "$ROOT" clean

echo "Release artifacts:"
ls -lh "$DIST"
