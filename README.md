# SlickNat Client

SlickNat Client is a userspace daemon and client utility for querying IPv6 NAT mappings from the SlickNat kernel module. It provides a network-accessible JSON API for resolving internal IPv6 addresses to their corresponding global unicast addresses.

## Features

- **Network-accessible daemon**: Listens on IPv6 TCP socket for remote access
- **Multi-network support**: Client can connect to different SlickNat networks
- **Automatic address expansion**: `slnatc 7000` connects to `7000::1`
- **Global IP resolution**: `get2kip` command specifically for 2000::/3 mappings
- **Real kernel integration**: Reads actual NAT mappings from kernel module
- **Automatic mapping reload**: Reloads mappings every 5 seconds
- **Multi-address listening**: Daemon can listen on multiple IPv6 addresses
- **Systemd integration**: Proper service management with systemd
- **Debian packaging**: Ready-to-install .deb packages

## Components

### slick-nat-daemon
Background daemon that:
- Reads NAT mappings from `/proc/net/slick_nat_mappings`
- Listens on multiple IPv6 addresses simultaneously
- Serves JSON API over TCP for mapping queries
- Automatically reloads mappings when they change
- Supports multi-threaded client connections

### slnatc
Command-line client that:
- Connects to daemon over IPv6 network
- Queries global unicast IP mappings
- Resolves IP address mappings
- Provides network connectivity testing

## Building

### Prerequisites

```bash
# Install build dependencies
sudo apt install build-essential cmake curl

# Optional: Install system nlohmann/json (recommended)
sudo apt install nlohmann-json3-dev
```

### Quick Build Options

The build system automatically handles the nlohmann/json dependency. Choose one of these options:

```bash
# Option 1: Install system package (recommended)
sudo apt install nlohmann-json3-dev
./build.sh

# Option 2: Let it download automatically
./build.sh

# Option 3: If download fails, install curl/wget
sudo apt install curl wget
./build.sh
```

### Build Commands

```bash
# Build everything
./build.sh

# Build with debug symbols
./build.sh --debug

# Build and create Debian packages
./build.sh --package

# Build only client
./build.sh --client

# Build only daemon
./build.sh --daemon
```

### Manual Build

```bash
# Using Makefiles (default)
make -f Makefile.client
make -f Makefile.clientd

# Using CMake
mkdir build && cd build
cmake ..
make
```

## Installation

### From Packages

```bash
# Build packages
./build.sh --package

# Install both packages
sudo dpkg -i pkg/output/slick-nat-client.deb pkg/output/slick-nat-daemon.deb

# Fix any dependency issues
sudo apt-get install -f
```

### From Source

```bash
# Build and install
./build.sh --install

# Or manually
sudo make -f Makefile.client install
sudo make -f Makefile.clientd install
```

## Usage

### Daemon Configuration

Edit `/etc/slnatcd/config`:

```bash
# Listen on localhost
listen ::1 7001

# Listen on specific network interface
listen 2001:db8::1 7001

# Listen on all interfaces (use with caution)
listen :: 7001

# Kernel proc file path
proc_path /proc/net/slick_nat_mappings
```

### Service Management

```bash
# Start daemon
sudo systemctl start slnatcd

# Enable auto-start
sudo systemctl enable slnatcd

# Check status
sudo systemctl status slnatcd

# View logs
sudo journalctl -u slnatcd -f

# Restart after config changes
sudo systemctl restart slnatcd
```

### Client Usage

```bash
# Test daemon connectivity
slnatc ::1 ping
slnatc 7000::1 ping

# Get global IP for specific address
slnatc 7000::1 get2kip 7000::100

# Get global IP for local address (auto-detect)
slnatc 7000::1 get2kip

# Resolve IP mappings
slnatc ::1 resolve 2001:db8::1
slnatc 7000::1 resolve 7000::50
```

## Network Architecture

### Typical Deployment

```
Network 7000::/64:
├── 7000::1 (daemon host)
├── 7000::2 (client host)  
├── 7000::100 (client host)
└── 7000::200 (client host)

Network 8000::/64:
├── 8000::1 (daemon host)
├── 8000::2 (client host)
└── 8000::150 (client host)
```

### Multi-Network Support

- Each SlickNat network runs its own daemon instance
- Clients connect to their local network's daemon
- Daemon can listen on multiple addresses for redundancy
- Cross-network queries are supported

## Protocol

### JSON API

**Request:**
```json
{
    "command": "get2kip",
    "ip": "7000::100"
}
```

**Success Response:**
```json
{
    "internal_ip": "7000::100",
    "global_ip": "2001:db8::100",
    "interface": "eth0",
    "status": "success"
}
```

**Error Response:**
```json
{
    "error": "No global unicast mapping found",
    "status": "not_found"
}
```

### Commands

- `get2kip [ip]` - Get global unicast IP (2000::/3 range)
- `resolve <ip>` - Resolve any IP mapping
- `ping` - Test daemon connectivity

## Integration

### Kernel Module Integration

The daemon reads mappings from the SlickNat kernel module via:
- `/proc/net/slick_nat_mappings` - NAT mapping entries
- Format: `interface internal_prefix/len -> external_prefix/len`
- Automatic reload every 5 seconds

### Application Integration

Applications can integrate by:
1. Connecting to daemon via IPv6 TCP
2. Sending JSON requests
3. Parsing JSON responses
4. Caching results as needed

Example in shell:
```bash
# Get global IP for local address
GLOBAL_IP=$(slnatc 7000::1 get2kip 7000::100 | grep "Global IP:" | cut -d' ' -f3)
echo "My global IP is: $GLOBAL_IP"
```

## Troubleshooting

### Build Issues

#### nlohmann/json Dependency Issues

The build system tries multiple approaches to get nlohmann/json:

1. **System package (recommended):**
   ```bash
   sudo apt install nlohmann-json3-dev
   ./build.sh
   ```

2. **Automatic download:**
   ```bash
   ./build.sh
   ```

3. **Manual download tools:**
   ```bash
   sudo apt install curl wget
   ./build.sh
   ```

#### Common Build Problems

```bash
# If nlohmann/json download fails
sudo apt install nlohmann-json3-dev

# If curl/wget is missing
sudo apt install curl wget

# If build tools are missing
sudo apt install build-essential cmake

# Clean build and retry
./build.sh --clean

# Check build dependencies
which g++ cmake curl wget
pkg-config --exists nlohmann_json && echo "System nlohmann/json found"
```

#### Build System Selection

```bash
# Use Makefiles (default, faster)
./build.sh

# Use CMake (more portable)
./build.sh --cmake

# Debug build issues
./build.sh --debug

# Verbose build
make -f Makefile.client V=1
make -f Makefile.clientd V=1
```

### Runtime Issues

```bash
# Check daemon status
sudo systemctl status slnatcd

# Check daemon logs
sudo journalctl -u slnatcd -f

# Test daemon connectivity
slnatc ::1 ping

# Check configuration
sudo cat /etc/slnatcd/config

# Check if kernel module is loaded
cat /proc/net/slick_nat_mappings
```

### Network Issues

```bash
# Check if daemon is listening
sudo netstat -tlnp | grep 7001

# Test IPv6 connectivity
ping6 ::1
ping6 7000::1

# Check firewall
sudo ip6tables -L -n
```

### Dependency Verification

```bash
# Check required tools
echo "Checking build dependencies:"
which g++ && echo "✓ g++ found" || echo "✗ g++ missing"
which cmake && echo "✓ cmake found" || echo "✗ cmake missing"
which curl && echo "✓ curl found" || echo "✗ curl missing"
which wget && echo "✓ wget found" || echo "✗ wget missing"
which pkg-config && echo "✓ pkg-config found" || echo "✗ pkg-config missing"

# Check nlohmann/json
pkg-config --exists nlohmann_json && echo "✓ System nlohmann/json found" || echo "✗ System nlohmann/json missing"

# Check if download worked
ls -la third_party/nlohmann/json.hpp 2>/dev/null && echo "✓ Downloaded nlohmann/json found" || echo "✗ No downloaded nlohmann/json"
```

## Development

### Project Structure

```
slick-nat-client/
├── src-client/           # Client source code
│   ├── slnatc.cpp       # Main client implementation
│   └── Makefile.client  # Client build rules
├── src-clientd/         # Daemon source code
│   ├── slnat-daemon.cpp # Main daemon implementation
│   └── Makefile.clientd # Daemon build rules
├── pkg/                 # Packaging files
│   ├── deb-slnatc/     # Client package
│   ├── deb-slnatcd/    # Daemon package
│   └── deb/            # Package build scripts
├── src/                 # CMake build files
│   └── CMakeLists.txt  # CMake configuration
├── build.sh            # Main build script
├── Makefile.client     # Root client makefile
├── Makefile.clientd    # Root daemon makefile
└── CMakeLists.txt      # Root CMake file
```

### Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

### License

GNU Affero General Public License v3.0 (AGPL-3.0)

This ensures that any network service using this code must also provide source code to users.
