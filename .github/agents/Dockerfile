# Development environment for GitHub Copilot Agent
# This Dockerfile sets up an environment with Asterisk, PJSIP, and SpanDSP

FROM ubuntu:latest

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Set shell to bash
SHELL ["/bin/bash", "-c"]

# Install build dependencies and tools
RUN apt-get update && apt-get install -y \
    # Build essentials
    build-essential \
    autoconf \
    automake \
    libtool \
    pkg-config \
    git \
    wget \
    curl \
    # SpanDSP dependencies
    libtiff-dev \
    libtiff5-dev \
    libxml2-dev \
    libfftw3-dev \
    # PJSIP dependencies
    libssl-dev \
    libasound2-dev \
    libopus-dev \
    libspeex-dev \
    libspeexdsp-dev \
    uuid-dev \
    # Asterisk dependencies
    libncurses5-dev \
    libjansson-dev \
    libsqlite3-dev \
    libsrtp2-dev \
    libedit-dev \
    libgsm1-dev \
    libpopt-dev \
    libresample1-dev \
    libcurl4-openssl-dev \
    libical-dev \
    libneon27-dev \
    libgmime-3.0-dev \
    liblua5.2-dev \
    libunbound-dev \
    libxslt1-dev \
    libldap2-dev \
    libogg-dev \
    libvorbis-dev \
    # Network tools for testing
    iproute2 \
    iputils-ping \
    net-tools \
    # Utilities
    ca-certificates \
    vim \
    && rm -rf /var/lib/apt/lists/*

# Create working directory
WORKDIR /opt

# Install PJSIP (required by Asterisk)
RUN wget https://github.com/pjsip/pjproject/archive/refs/tags/2.14.1.tar.gz -O pjproject-2.14.1.tar.gz && \
    tar xzf pjproject-2.14.1.tar.gz && \
    cd pjproject-2.14.1 && \
    ./configure \
        --prefix=/usr \
        --libdir=/usr/lib \
        --enable-shared \
        --disable-sound \
        --disable-video \
        --disable-opencore-amr \
        --with-external-speex \
        --with-external-gsm \
        --with-external-srtp && \
    make dep && \
    make && \
    make install && \
    ldconfig && \
    cd /opt && \
    rm -rf pjproject-2.14.1 pjproject-2.14.1.tar.gz

# Build and install SpanDSP from the repository
COPY . /opt/spandsp
WORKDIR /opt/spandsp

# Bootstrap and build SpanDSP
RUN ./bootstrap.sh && \
    ./configure \
        --prefix=/usr \
        --libdir=/usr/lib && \
    make && \
    make install && \
    ldconfig

# Install Asterisk
WORKDIR /opt
RUN wget https://downloads.asterisk.org/pub/telephony/asterisk/asterisk-21-current.tar.gz && \
    tar xzf asterisk-21-current.tar.gz && \
    cd asterisk-21.* && \
    ./contrib/scripts/install_prereq install && \
    ./configure \
        --prefix=/usr \
        --sysconfdir=/etc \
        --with-pjproject-bundled=no \
        --with-jansson-bundled=no && \
    make menuselect.makeopts && \
    menuselect/menuselect \
        --enable app_macro \
        --enable CORE-SOUNDS-EN-GSM \
        --enable MOH-OPSOUND-GSM \
        menuselect.makeopts && \
    make && \
    make install && \
    make samples && \
    make config && \
    ldconfig && \
    cd /opt && \
    rm -rf asterisk-21* asterisk-21-current.tar.gz

# Configure network namespace capability
# Capabilities are managed by the container runtime (--cap-add=NET_ADMIN)

# Create a helper script for network namespace testing
RUN echo '#!/bin/bash' > /usr/local/bin/test-netns.sh && \
    echo 'set -e' >> /usr/local/bin/test-netns.sh && \
    echo 'echo "Creating network namespace..."' >> /usr/local/bin/test-netns.sh && \
    echo 'ip netns add testns || echo "Namespace already exists or cannot be created"' >> /usr/local/bin/test-netns.sh && \
    echo 'echo "Listing network namespaces:"' >> /usr/local/bin/test-netns.sh && \
    echo 'ip netns list || echo "Cannot list namespaces"' >> /usr/local/bin/test-netns.sh && \
    echo 'echo "Testing ping to host (172.17.0.1 - typical Docker bridge IP):"' >> /usr/local/bin/test-netns.sh && \
    echo 'ping -c 2 172.17.0.1 || ping -c 2 host.docker.internal || ping -c 2 8.8.8.8 || echo "Ping test completed"' >> /usr/local/bin/test-netns.sh && \
    chmod +x /usr/local/bin/test-netns.sh

# Set up environment variables
ENV PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"

# Create a verification script to show installed versions
RUN echo '#!/bin/bash' > /usr/local/bin/verify-installation.sh && \
    echo 'echo "=== Installation Verification ==="' >> /usr/local/bin/verify-installation.sh && \
    echo 'echo ""' >> /usr/local/bin/verify-installation.sh && \
    echo 'echo "SpanDSP library:"' >> /usr/local/bin/verify-installation.sh && \
    echo 'ls -la /usr/lib/libspandsp* 2>/dev/null || echo "SpanDSP library not found in expected location"' >> /usr/local/bin/verify-installation.sh && \
    echo 'echo ""' >> /usr/local/bin/verify-installation.sh && \
    echo 'echo "PJSIP library:"' >> /usr/local/bin/verify-installation.sh && \
    echo 'ls -la /usr/lib/libpj* 2>/dev/null | head -5 || echo "PJSIP library not found in expected location"' >> /usr/local/bin/verify-installation.sh && \
    echo 'echo ""' >> /usr/local/bin/verify-installation.sh && \
    echo 'echo "Asterisk binary:"' >> /usr/local/bin/verify-installation.sh && \
    echo 'which asterisk && asterisk -V 2>/dev/null || echo "Asterisk not found"' >> /usr/local/bin/verify-installation.sh && \
    echo 'echo ""' >> /usr/local/bin/verify-installation.sh && \
    echo 'echo "Network tools:"' >> /usr/local/bin/verify-installation.sh && \
    echo 'which ip ping || echo "Network tools not found"' >> /usr/local/bin/verify-installation.sh && \
    echo 'echo ""' >> /usr/local/bin/verify-installation.sh && \
    chmod +x /usr/local/bin/verify-installation.sh

# Set working directory to the spandsp repo
WORKDIR /opt/spandsp

# Default command shows verification
CMD ["/bin/bash", "-c", "verify-installation.sh && echo 'Development environment ready!' && /bin/bash"]
