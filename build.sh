#!/bin/bash

# Build script for SlickNat Agent

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored output
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Default values
BUILD_TYPE="Release"
BUILD_TARGET="both"
CLEAN=false
INSTALL=false
PACKAGE=false
PARALLEL_JOBS=$(nproc)

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -d|--debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        -c|--clean)
            CLEAN=true
            shift
            ;;
        -i|--install)
            INSTALL=true
            shift
            ;;
        -p|--package)
            PACKAGE=true
            shift
            ;;
        -j|--jobs)
            PARALLEL_JOBS="$2"
            shift
            shift
            ;;
        --client)
            BUILD_TARGET="client"
            shift
            ;;
        --daemon)
            BUILD_TARGET="daemon"
            shift
            ;;
        --cmake)
            BUILD_TARGET="cmake"
            shift
            ;;
        --test)
            BUILD_TARGET="test"
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  -d, --debug      Build in debug mode"
            echo "  -c, --clean      Clean build directory before building"
            echo "  -i, --install    Install binaries after building"
            echo "  -p, --package    Create Debian packages"
            echo "  -j, --jobs N     Use N parallel jobs (default: $(nproc))"
            echo "  --client         Build only client"
            echo "  --daemon         Build only daemon"
            echo "  --cmake          Use CMake build system"
            echo "  --test           Build and test binaries"
            echo "  -h, --help       Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                    # Build both client and daemon"
            echo "  $0 -d --client        # Build client in debug mode"
            echo "  $0 -c -p              # Clean build and create packages"
            echo "  $0 --cmake            # Use CMake build system"
            echo "  $0 --test             # Build and test"
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            exit 1
            ;;
    esac
done

print_info "Building SlickNat Agent..."
print_info "Build type: $BUILD_TYPE"
print_info "Build target: $BUILD_TARGET"
print_info "Parallel jobs: $PARALLEL_JOBS"

# Check if we have build files
if [ ! -f "Makefile.client" ] && [ ! -f "src/CMakeLists.txt" ]; then
    print_error "No build files found. Please run this script from the project root."
    exit 1
fi

# Clean if requested
if [ "$CLEAN" = true ]; then
    print_info "Cleaning build directory..."
    rm -rf build third_party
fi

# Choose build system
if [ "$BUILD_TARGET" = "cmake" ]; then
    # Use CMake build system
    print_info "Using CMake build system..."
    
    if [ ! -f "CMakeLists.txt" ]; then
        print_error "CMakeLists.txt not found in project root."
        exit 1
    fi
    
    mkdir -p build
    cd build
    
    CMAKE_BUILD_TYPE="Release"
    if [ "$BUILD_TYPE" = "Debug" ]; then
        CMAKE_BUILD_TYPE="Debug"
    fi
    
    cmake -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE" \
          -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
          ..
    
    make -j"$PARALLEL_JOBS"
    cd ..
    
else
    # Use Makefile build system
    print_info "Using Makefile build system..."
    
    if [ ! -f "Makefile.client" ] || [ ! -f "Makefile.clientd" ]; then
        print_error "Required makefiles not found in project root."
        exit 1
    fi
    
    MAKE_TARGET=""
    if [ "$BUILD_TYPE" = "Debug" ]; then
        MAKE_TARGET="debug"
    fi
    
    case $BUILD_TARGET in
        "client")
            print_info "Building client..."
            make -f Makefile.client $MAKE_TARGET -j"$PARALLEL_JOBS"
            ;;
        "daemon")
            print_info "Building daemon..."
            make -f Makefile.clientd $MAKE_TARGET -j"$PARALLEL_JOBS"
            ;;
        "test")
            print_info "Building and testing..."
            make -f Makefile.client $MAKE_TARGET -j"$PARALLEL_JOBS"
            make -f Makefile.client test
            make -f Makefile.clientd $MAKE_TARGET -j"$PARALLEL_JOBS"
            make -f Makefile.clientd test
            ;;
        "both")
            print_info "Building client..."
            make -f Makefile.client $MAKE_TARGET -j"$PARALLEL_JOBS"
            print_info "Building daemon..."
            make -f Makefile.clientd $MAKE_TARGET -j"$PARALLEL_JOBS"
            ;;
    esac
fi

print_info "Build completed successfully!"

# Show built binaries
if [ -d "build" ]; then
    print_info "Built binaries:"
    ls -la build/ 2>/dev/null || true
    
    # Test binaries exist
    if [ -f "build/slnatc" ]; then
        print_info "✓ Client binary: build/slnatc"
    else
        print_warning "✗ Client binary not found: build/slnatc"
    fi
    
    if [ -f "build/slick-nat-daemon" ]; then
        print_info "✓ Daemon binary: build/slick-nat-daemon"
    else
        print_warning "✗ Daemon binary not found: build/slick-nat-daemon"
    fi
else
    print_warning "Build directory not found"
fi

# Install if requested
if [ "$INSTALL" = true ]; then
    print_info "Installing binaries..."
    
    case $BUILD_TARGET in
        "client")
            sudo make -f Makefile.client install
            ;;
        "daemon")
            sudo make -f Makefile.clientd install
            ;;
        "both"|"cmake"|"test")
            if [ "$BUILD_TARGET" = "cmake" ]; then
                cd build && sudo make install && cd ..
            else
                sudo make -f Makefile.client install
                sudo make -f Makefile.clientd install
            fi
            ;;
    esac
    
    print_info "Installation completed successfully!"
fi

# Create packages if requested
if [ "$PACKAGE" = true ]; then
    print_info "Creating Debian packages..."
    if [ ! -f "pkg/deb/build-packages.sh" ]; then
        print_error "Package build script not found"
        exit 1
    fi
    chmod +x pkg/deb/build-packages.sh
    ./pkg/deb/build-packages.sh
    print_info "Packages created in pkg/output/"
    
    # Re-check binaries after package building (they might be built as part of packaging)
    print_info ""
    print_info "Final binary status:"
    if [ -f "build/slnatc" ]; then
        print_info "✓ Client binary: build/slnatc"
    else
        print_warning "✗ Client binary not found: build/slnatc"
    fi
    
    if [ -f "build/slick-nat-daemon" ]; then
        print_info "✓ Daemon binary: build/slick-nat-daemon"
    else
        print_warning "✗ Daemon binary not found: build/slick-nat-daemon"
    fi
fi

print_info "Build script completed!"
print_info ""
print_info "Usage:"
if [ -f "build/slnatc" ]; then
    print_info "  Client: ./build/slnatc ::1 ping"
else
    print_warning "  Client not built"
fi
if [ -f "build/slick-nat-daemon" ]; then
    print_info "  Daemon: ./build/slick-nat-daemon --config /etc/slnatcd/config"
else
    print_warning "  Daemon not built"
fi
print_info ""
print_info "For help:"
if [ -f "build/slnatc" ]; then
    print_info "  ./build/slnatc ::1 ping (test connectivity)"
    print_info "  ./build/slnatc --help (show usage)"
else
    print_info "  ./build/slnatc --help (if available)"
fi
if [ -f "build/slick-nat-daemon" ]; then
    print_info "  ./build/slick-nat-daemon --help (show usage)"
else
    print_info "  ./build/slick-nat-daemon --help (if available)"
fi

# Show package information if packages were built
if [ "$PACKAGE" = true ] && [ -d "pkg/output" ]; then
    print_info ""
    print_info "Packages built:"
    ls -la pkg/output/ 2>/dev/null || true
    print_info ""
    print_info "To install packages:"
    print_info "  sudo dpkg -i pkg/output/slick-nat-client.deb"
    print_info "  sudo dpkg -i pkg/output/slick-nat-daemon.deb"
    print_info "  Or: sudo dpkg -i pkg/output/slick-nat-*.deb"
fi
