#!/bin/bash
# install.sh - Fully automated install/deploy for Coup Web + Saturn Server
#
# Idempotent: safe to run multiple times.
# Target: Debian 11/12 or Ubuntu 20.04+
# Serves: https://saturncoup.duckdns.org (port 443)
# Saturn TCP: port 4821 (unchanged)
# WebSocket: port 4823 (proxied via Nginx /ws)

set -e

REPO_URL="https://github.com/feldmanweb/coup-server.git"
REPO_BRANCH="main"
INSTALL_DIR="/opt/coup-server"
DOMAIN="saturncoup.duckdns.org"
DUCKDNS_TOKEN="${DUCKDNS_TOKEN:-}"  # Pass via env: DUCKDNS_TOKEN=xxx ./install.sh
CERT_EMAIL="gary@feldmanweb.com"
SERVICE_USER="coup"
WS_PORT=4823
TCP_PORT=4821

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC} $1"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

# ==========================================================================
# Phase 0: Prerequisite Check & Auto-Install
# ==========================================================================

info "=== Phase 0: Prerequisites ==="

# Check OS
if [ ! -f /etc/debian_version ]; then
    error "This script requires Debian or Ubuntu. /etc/debian_version not found."
fi

OS_VERSION=$(cat /etc/debian_version)
info "Detected Debian/Ubuntu (version: $OS_VERSION)"

# Must be root
if [ "$(id -u)" -ne 0 ]; then
    error "Must be run as root (use sudo)"
fi

# Track what we install
ALREADY_INSTALLED=()
NEWLY_INSTALLED=()

check_pkg() {
    if dpkg -l "$1" 2>/dev/null | grep -q "^ii"; then
        ALREADY_INSTALLED+=("$1")
        return 0
    fi
    return 1
}

install_pkg() {
    if ! check_pkg "$1"; then
        info "Installing $1..."
        apt-get install -y "$1" > /dev/null 2>&1
        NEWLY_INSTALLED+=("$1")
    fi
}

# Update package list
info "Updating package list..."
apt-get update -qq

# System packages
SYSTEM_PKGS=(python3 python3-pip python3-venv nginx gcc make git curl openssl ca-certificates)
for pkg in "${SYSTEM_PKGS[@]}"; do
    install_pkg "$pkg"
done

# Python packages (system-wide)
info "Checking Python packages..."
PIP_PKGS=(websockets)
for pkg in "${PIP_PKGS[@]}"; do
    if python3 -c "import $pkg" 2>/dev/null; then
        ALREADY_INSTALLED+=("pip:$pkg")
    else
        info "Installing pip package: $pkg"
        pip3 install "$pkg" --break-system-packages 2>/dev/null || pip3 install "$pkg"
        NEWLY_INSTALLED+=("pip:$pkg")
    fi
done

# Certbot + DuckDNS plugin
if command -v certbot > /dev/null 2>&1; then
    ALREADY_INSTALLED+=("certbot")
else
    info "Installing certbot..."
    pip3 install certbot --break-system-packages 2>/dev/null || pip3 install certbot
    NEWLY_INSTALLED+=("certbot")
fi

if python3 -c "import certbot_dns_duckdns" 2>/dev/null; then
    ALREADY_INSTALLED+=("certbot-dns-duckdns")
else
    info "Installing certbot-dns-duckdns..."
    pip3 install certbot-dns-duckdns --break-system-packages 2>/dev/null || pip3 install certbot-dns-duckdns
    NEWLY_INSTALLED+=("certbot-dns-duckdns")
fi

# Summary
info "Already installed: ${ALREADY_INSTALLED[*]:-none}"
info "Newly installed: ${NEWLY_INSTALLED[*]:-none}"

# ==========================================================================
# Phase 1: Application Setup
# ==========================================================================

info "=== Phase 1: Application Setup ==="

# Create service user
if id "$SERVICE_USER" &>/dev/null; then
    info "User '$SERVICE_USER' already exists"
else
    info "Creating system user '$SERVICE_USER'..."
    useradd --system --shell /bin/false --home-dir "$INSTALL_DIR" "$SERVICE_USER"
fi

# Clone or update repo
if [ -d "$INSTALL_DIR/.git" ]; then
    info "Updating existing repo..."
    cd "$INSTALL_DIR"
    git fetch origin
    git reset --hard "origin/$REPO_BRANCH"
else
    info "Cloning repo to $INSTALL_DIR..."
    git clone --branch "$REPO_BRANCH" "$REPO_URL" "$INSTALL_DIR" 2>/dev/null || {
        warn "Git clone failed (repo may not exist yet). Creating directory structure..."
        mkdir -p "$INSTALL_DIR"
    }
fi

# Build C rule engine shared library
if [ -f "$INSTALL_DIR/tools/coup_server/build_lib.sh" ]; then
    info "Building libcoup_rules.so..."
    cd "$INSTALL_DIR/tools/coup_server"
    bash build_lib.sh
    if [ -f libcoup_rules.so ]; then
        info "libcoup_rules.so built successfully"
    else
        warn "libcoup_rules.so not found after build (game will use Python fallback)"
    fi
fi

# Copy game assets to web/assets/ (symlink to avoid duplication)
ASSETS_SRC="$INSTALL_DIR/examples/coup/assets"
ASSETS_DST="$INSTALL_DIR/web/assets"
if [ -d "$ASSETS_SRC" ]; then
    mkdir -p "$INSTALL_DIR/web"
    if [ -L "$ASSETS_DST" ]; then
        info "Assets symlink already exists"
    elif [ -d "$ASSETS_DST" ]; then
        info "Assets directory already exists"
    else
        info "Symlinking game assets to web/assets..."
        ln -s "$ASSETS_SRC" "$ASSETS_DST"
    fi
fi

# ==========================================================================
# Phase 2: SSL & Networking
# ==========================================================================

info "=== Phase 2: SSL & Networking ==="

CERT_DIR="/etc/letsencrypt/live/$DOMAIN"
if [ -d "$CERT_DIR" ]; then
    info "SSL certificate already exists for $DOMAIN"
else
    if [ -z "$DUCKDNS_TOKEN" ]; then
        error "DUCKDNS_TOKEN not set. Run: DUCKDNS_TOKEN=your-token-here sudo -E bash install.sh"
    fi
    info "Requesting SSL certificate via certbot DNS-01 (DuckDNS)..."
    certbot certonly \
        --authenticator dns-duckdns \
        --dns-duckdns-token "$DUCKDNS_TOKEN" \
        --dns-duckdns-propagation-seconds 60 \
        -d "$DOMAIN" \
        --non-interactive \
        --agree-tos \
        --email "$CERT_EMAIL" || {
        warn "Certbot failed. You may need to run certbot manually."
    }
fi

# Install certbot post-renewal hook
HOOK_DIR="/etc/letsencrypt/renewal-hooks/deploy"
HOOK_FILE="$HOOK_DIR/reload-nginx.sh"
mkdir -p "$HOOK_DIR"
if [ ! -f "$HOOK_FILE" ]; then
    info "Installing certbot renewal hook..."
    cat > "$HOOK_FILE" << 'HOOKEOF'
#!/bin/bash
systemctl reload nginx
HOOKEOF
    chmod +x "$HOOK_FILE"
fi

# ==========================================================================
# Phase 3: Services
# ==========================================================================

info "=== Phase 3: Services ==="

# Install Nginx config
NGINX_CONF="/etc/nginx/sites-available/saturncoup"
NGINX_ENABLED="/etc/nginx/sites-enabled/saturncoup"

info "Installing Nginx configuration..."
cp "$INSTALL_DIR/deploy/nginx-saturncoup.conf" "$NGINX_CONF"

# Remove default site if it exists
rm -f /etc/nginx/sites-enabled/default

# Enable site
if [ ! -L "$NGINX_ENABLED" ]; then
    ln -s "$NGINX_CONF" "$NGINX_ENABLED"
fi

# Test config
if nginx -t 2>/dev/null; then
    info "Nginx config OK"
    systemctl reload nginx || systemctl start nginx
else
    warn "Nginx config test failed. Check /etc/nginx/sites-available/saturncoup"
fi

systemctl enable nginx

# Install coup-server systemd service
info "Installing coup-server systemd service..."
cp "$INSTALL_DIR/deploy/coup-server.service" /etc/systemd/system/coup-server.service
systemctl daemon-reload
systemctl enable coup-server
systemctl restart coup-server

# Firewall (ufw if available)
if command -v ufw > /dev/null 2>&1; then
    info "Configuring firewall..."
    ufw allow 80/tcp > /dev/null 2>&1 || true
    ufw allow 443/tcp > /dev/null 2>&1 || true
    # Port 4821 should already be open for Saturn TCP
    ufw allow 4821/tcp > /dev/null 2>&1 || true
fi

# Set file ownership
info "Setting file ownership..."
chown -R "$SERVICE_USER:$SERVICE_USER" "$INSTALL_DIR"

# ==========================================================================
# Phase 4: Validation
# ==========================================================================

info "=== Phase 4: Validation ==="

# Check Nginx
if systemctl is-active --quiet nginx; then
    info "Nginx: RUNNING"
else
    warn "Nginx: NOT RUNNING"
fi

# Check coup-server
sleep 2
if systemctl is-active --quiet coup-server; then
    info "coup-server: RUNNING"
else
    warn "coup-server: NOT RUNNING"
    journalctl -u coup-server --no-pager -n 10
fi

# Check ports
for port in $TCP_PORT $WS_PORT; do
    if ss -tlnp | grep -q ":$port "; then
        info "Port $port: LISTENING"
    else
        warn "Port $port: NOT LISTENING"
    fi
done

# Summary
echo ""
info "========================================"
info "  Deployment Complete!"
info "========================================"
info ""
info "  Web Client:  https://$DOMAIN/"
info "  WebSocket:   wss://$DOMAIN/ws"
info "  Saturn TCP:  $DOMAIN:$TCP_PORT"
info ""
info "  Service commands:"
info "    systemctl status coup-server"
info "    systemctl restart coup-server"
info "    journalctl -u coup-server -f"
info ""
info "  Update:  bash $INSTALL_DIR/deploy/update.sh"
info "========================================"
