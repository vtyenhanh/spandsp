# GitHub Copilot Agent Development Environment

This directory contains the custom development environment configuration for GitHub Copilot Agent.

## Overview

The Dockerfile in this directory creates a development environment based on Ubuntu (latest) with the following components installed and configured:

1. **PJSIP** (Project SIP) - An open-source SIP stack
2. **SpanDSP** - A library of DSP functions for telephony (built from this repository)
3. **Asterisk** - An open-source PBX and telephony platform
4. **Network Namespace Support** - Tools and capabilities for creating and testing network namespaces

## Components

### PJSIP
- Version: 2.14.1
- Built with external dependencies (speex, gsm, srtp)
- Shared library installation
- Installed to `/usr` prefix

### SpanDSP
- Built from the current repository source
- All standard dependencies included (libtiff, libfftw3, etc.)
- Installed to `/usr` prefix

### Asterisk
- Version: Latest from Asterisk 21 LTS branch
- Built with PJSIP support (system PJSIP, not bundled)
- Standard modules enabled
- Installed to `/usr` prefix with config in `/etc`

### Network Tools
- `iproute2` - Advanced IP routing and network device configuration tools
- `iputils-ping` - Tools to test network connectivity
- `net-tools` - Traditional networking tools

## Helper Scripts

### `/usr/local/bin/verify-installation.sh`
Verifies that all components are properly installed by checking:
- SpanDSP library files
- PJSIP library files
- Asterisk binary and version
- Network tools availability

Usage:
```bash
verify-installation.sh
```

### `/usr/local/bin/test-netns.sh`
Tests network namespace functionality:
- Creates a test network namespace
- Lists available namespaces
- Tests ping connectivity to the host

Usage:
```bash
test-netns.sh
```

## Usage

This environment is automatically used by GitHub Copilot Agent when configured. The agent will have access to all installed tools and libraries for development and testing.

## Requirements

- Docker with sufficient resources (at least 4GB RAM recommended for building)
- The build process takes approximately 30-60 minutes depending on system resources

## Network Namespace Notes

Network namespaces allow for isolated network stacks within the container. This is useful for:
- Testing network configurations
- Simulating different network topologies
- Isolating network traffic

The environment includes the necessary capabilities and tools to create and manage network namespaces, and test connectivity to the host system.

## Building Locally (Optional)

To test this environment locally:

```bash
cd /path/to/spandsp
docker build -f .github/agents/Dockerfile -t spandsp-agent-env .
docker run -it --cap-add=NET_ADMIN spandsp-agent-env
```

Note: The `--cap-add=NET_ADMIN` flag is required for network namespace operations.
