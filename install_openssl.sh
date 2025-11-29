#!/bin/bash
# Install OpenSSL development libraries

echo "Installing OpenSSL development libraries..."

# For Ubuntu/Debian
if command -v apt-get &> /dev/null; then
    sudo apt-get update
    sudo apt-get install -y libssl-dev
    echo "✓ OpenSSL installed via apt-get"

# For Fedora/RHEL/CentOS
elif command -v yum &> /dev/null; then
    sudo yum install -y openssl-devel
    echo "✓ OpenSSL installed via yum"

# For Arch Linux
elif command -v pacman &> /dev/null; then
    sudo pacman -S openssl
    echo "✓ OpenSSL installed via pacman"

else
    echo "✗ Cannot detect package manager"
    echo "Please install OpenSSL manually:"
    echo "  Ubuntu/Debian: sudo apt-get install libssl-dev"
    echo "  Fedora/RHEL:   sudo yum install openssl-devel"
    echo "  Arch:          sudo pacman -S openssl"
    exit 1
fi

echo ""
echo "Verifying installation..."
if pkg-config --exists openssl; then
    echo "✓ OpenSSL is properly installed"
    pkg-config --modversion openssl
else
    echo "✗ OpenSSL installation verification failed"
    exit 1
fi
