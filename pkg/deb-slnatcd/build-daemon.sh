#!/bin/bash

set -e

# Build configuration
VERSION="1.0.0"
BUILD_DIR="/tmp/slick-nat-daemon-build"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "Building SlickNat Daemon package..."
echo "Project root: $PROJECT_ROOT"
echo "Build directory: $BUILD_DIR"

# Clean and create build directory
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Build daemon binary
echo "Building daemon binary..."
cd "$PROJECT_ROOT"

# Use the daemon makefile
make -f Makefile.clientd clean
make -f Makefile.clientd

# Build daemon package
echo "Building daemon package..."
DAEMON_PKG_DIR="$BUILD_DIR/slick-nat-daemon"
mkdir -p "$DAEMON_PKG_DIR/DEBIAN"
mkdir -p "$DAEMON_PKG_DIR/usr/sbin"
mkdir -p "$DAEMON_PKG_DIR/lib/systemd/system"
mkdir -p "$DAEMON_PKG_DIR/usr/share/man/man8"
mkdir -p "$DAEMON_PKG_DIR/usr/share/doc/slick-nat-daemon"
mkdir -p "$DAEMON_PKG_DIR/etc/slnatcd"

# Copy daemon files
cp build/slick-nat-daemon "$DAEMON_PKG_DIR/usr/sbin/"
cp pkg/deb-slnatcd/control "$DAEMON_PKG_DIR/DEBIAN/"

# Copy systemd service file
cp pkg/deb-slnatcd/res/slnatcd.service "$DAEMON_PKG_DIR/lib/systemd/system/"

# Copy default config file
cp pkg/deb-slnatcd/res/etc-slnatd "$DAEMON_PKG_DIR/etc/slnatcd/config"

# Copy man page for daemon
cp pkg/deb-slnatcd/res/slick-nat-clientd.8 "$DAEMON_PKG_DIR/usr/share/man/man8/slick-nat-daemon.8"

# Create documentation
cat > "$DAEMON_PKG_DIR/usr/share/doc/slick-nat-daemon/README" << 'EOF'
SlickNat Daemon
===============

This package contains the slick-nat-daemon service that provides
IPv6 NAT mapping information by reading kernel SlickNat module
data and serving it over a JSON API.

Configuration:
  Edit /etc/slnatcd/config to configure listening addresses.

Service Management:
  sudo systemctl start slnatcd
  sudo systemctl enable slnatcd
  sudo systemctl status slnatcd

Logs:
  sudo journalctl -u slnatcd -f
EOF

# Create post-install script
cat > "$DAEMON_PKG_DIR/DEBIAN/postinst" << 'EOF'
#!/bin/bash
set -e

# Reload systemd
systemctl daemon-reload

# Enable and start the service
systemctl enable slnatcd || true
systemctl start slnatcd || true

echo "SlickNat daemon installed and started."
echo "Configure listening addresses in /etc/slnatcd/config"
echo "Then restart with: sudo systemctl restart slnatcd"
EOF

# Create pre-remove script
cat > "$DAEMON_PKG_DIR/DEBIAN/prerm" << 'EOF'
#!/bin/bash
set -e

# Stop and disable the service
systemctl stop slnatcd || true
systemctl disable slnatcd || true
EOF

# Copy license
cp LICENSE "$DAEMON_PKG_DIR/usr/share/doc/slick-nat-daemon/copyright"

# Compress files
gzip -9 "$DAEMON_PKG_DIR/usr/share/man/man8/slick-nat-daemon.8"

# Set permissions
chmod 755 "$DAEMON_PKG_DIR/usr/sbin/slick-nat-daemon"
chmod 644 "$DAEMON_PKG_DIR/lib/systemd/system/slnatcd.service"
chmod 644 "$DAEMON_PKG_DIR/etc/slnatd/config"
chmod 755 "$DAEMON_PKG_DIR/DEBIAN/postinst"
chmod 755 "$DAEMON_PKG_DIR/DEBIAN/prerm"

# Build package
echo "Building daemon .deb package..."
cd "$BUILD_DIR"

# Calculate package size and update control file
DAEMON_SIZE=$(du -sk slick-nat-daemon | cut -f1)
echo "Installed-Size: $DAEMON_SIZE" >> slick-nat-daemon/DEBIAN/control

# Build daemon package
dpkg-deb --build slick-nat-daemon
echo "Daemon package built: $BUILD_DIR/slick-nat-daemon.deb"

# Copy package to output directory
OUTPUT_DIR="$PROJECT_ROOT/pkg/output"
mkdir -p "$OUTPUT_DIR"
cp slick-nat-daemon.deb "$OUTPUT_DIR/"

echo "Daemon package built successfully!"
echo "Package: $OUTPUT_DIR/slick-nat-daemon.deb"
echo ""
echo "To install:"
echo "  sudo dpkg -i $OUTPUT_DIR/slick-nat-daemon.deb"
echo ""
echo "To configure:"
echo "  sudo nano /etc/slnatcd/config"
echo "  sudo systemctl restart slnatcd"