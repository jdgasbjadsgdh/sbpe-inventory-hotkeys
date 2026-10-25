#!/bin/bash
set -euo pipefail
PACKAGE_DIR="$(cd "$(dirname "$0")" && pwd -P)"
export SBPE_INSTALL_EMERGENCY_HOLD=1
exec /bin/bash "$PACKAGE_DIR/UPDATE_EXISTING.command" "$@"
