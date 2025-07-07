#!/bin/bash

set -e

# Build configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "Building SlickNat packages..."
echo "Project root: $PROJECT_ROOT"

cd "$PROJECT_ROOT"

# Create output directory
mkdir -p pkg/output

# Build both packages using the project root makefiles
echo "Building client package..."
make -f Makefile.client package

echo "Building daemon package..."
make -f Makefile.clientd package

echo ""
echo "Build complete!"
echo "Packages created in pkg/output/:"
ls -la pkg/output/

echo ""
echo "To install:"
echo "  sudo dpkg -i pkg/output/slick-nat-client.deb"
echo "  sudo dpkg -i pkg/output/slick-nat-daemon.deb"
echo ""
echo "Or install both:"
echo "  sudo dpkg -i pkg/output/slick-nat-*.deb"
