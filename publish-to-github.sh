#!/bin/zsh
set -euo pipefail

OWNER="StefanAlMare"
REPO="macOS-Cellular-EM7455"
FULL="$OWNER/$REPO"
DESCRIPTION="User-space MBIM/NCM cellular networking for modern macOS using Sierra Wireless EM7455 / Dell DW5811e, with Cellular.app, auto fallback and macOS .pkg builder."
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

echo "============================================================"
echo " Publish $FULL"
echo "============================================================"
echo

if ! command -v git >/dev/null 2>&1; then
  echo "ERROR: git is not installed. Install Xcode Command Line Tools first."
  exit 1
fi

if ! command -v gh >/dev/null 2>&1; then
  if command -v brew >/dev/null 2>&1; then
    echo "GitHub CLI not found. Installing with Homebrew..."
    brew install gh
  else
    echo "ERROR: GitHub CLI (gh) is not installed and Homebrew is unavailable."
    echo "Install gh, then run this script again."
    exit 2
  fi
fi

if ! gh auth status >/dev/null 2>&1; then
  echo
  echo "GitHub authentication is required."
  gh auth login --hostname github.com --git-protocol https --web
fi

LOGIN="$(gh api user --jq .login)"
if [[ "$LOGIN" != "$OWNER" ]]; then
  echo "ERROR: gh is authenticated as '$LOGIN', expected '$OWNER'."
  echo "Run: gh auth switch --user $OWNER"
  exit 3
fi

if [[ ! -d .git ]]; then
  echo "Initializing local git repository..."
  git init -b main
  git config user.name "$OWNER"
  git config user.email "92223268+StefanAlMare@users.noreply.github.com"
  git add .
  git commit -m "Initial public release of macOS Cellular EM7455"
fi

if gh repo view "$FULL" >/dev/null 2>&1; then
  echo "Repository already exists: $FULL"

  if ! git remote get-url origin >/dev/null 2>&1; then
    git remote add origin "https://github.com/$FULL.git"
  fi

  git branch -M main
  git push -u origin main
else
  echo "Creating public repository: $FULL"
  gh repo create "$FULL" \
    --public \
    --description "$DESCRIPTION" \
    --source . \
    --remote origin \
    --push
fi

# Best-effort topics. A failure here must not undo a successful publish.
gh repo edit "$FULL" \
  --add-topic macos \
  --add-topic hackintosh \
  --add-topic cellular \
  --add-topic wwan \
  --add-topic mbim \
  --add-topic cdc-ncm \
  --add-topic em7455 \
  --add-topic dw5811e \
  --add-topic sierra-wireless \
  --add-topic libusb \
  --add-topic utun \
  --add-topic lte || true

echo
echo "Published: https://github.com/$FULL"
echo
