#!/usr/bin/env bash
# Quick validation script for the Copilot Agent environment Dockerfile
# This script performs basic syntax checking without building the full image

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOCKERFILE="$SCRIPT_DIR/Dockerfile"

echo "=== Dockerfile Validation ==="
echo ""

# Check if Dockerfile exists
if [ ! -f "$DOCKERFILE" ]; then
    echo "❌ Error: Dockerfile not found at $DOCKERFILE"
    exit 1
fi
echo "✓ Dockerfile exists"

# Check if Dockerfile has FROM instruction
if ! grep -q "^FROM" "$DOCKERFILE"; then
    echo "❌ Error: No FROM instruction found"
    exit 1
fi
echo "✓ FROM instruction found"

# Check if Dockerfile uses Ubuntu
if ! grep -q "FROM ubuntu" "$DOCKERFILE"; then
    echo "⚠️  Warning: Not using Ubuntu as base image"
else
    echo "✓ Using Ubuntu base image"
fi

# Check for key components
echo ""
echo "Checking for required components:"

if grep -q "pjproject" "$DOCKERFILE"; then
    echo "✓ PJSIP installation found"
else
    echo "❌ PJSIP installation not found"
fi

if grep -q "spandsp" "$DOCKERFILE"; then
    echo "✓ SpanDSP build steps found"
else
    echo "❌ SpanDSP build steps not found"
fi

if grep -q "asterisk" "$DOCKERFILE"; then
    echo "✓ Asterisk installation found"
else
    echo "❌ Asterisk installation not found"
fi

if grep -q "iproute2\|ip netns" "$DOCKERFILE"; then
    echo "✓ Network namespace tools found"
else
    echo "⚠️  Warning: Network namespace tools may not be included"
fi

echo ""
echo "=== Basic validation complete ==="
echo ""
echo "To build the image locally for testing:"
echo "  docker build -f .github/agents/Dockerfile -t spandsp-agent-env ."
echo ""
echo "To run with network namespace support:"
echo "  docker run -it --cap-add=NET_ADMIN spandsp-agent-env"
