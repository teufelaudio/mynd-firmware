#!/bin/bash
# generate_proto.sh - Generate Python protobuf files for ActionsLink

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROTO_BASE="${SCRIPT_DIR}/../proto"
NANOPB_PROTO="${SCRIPT_DIR}"
GENERATED_DIR="${SCRIPT_DIR}/../host/generated"

cd "${SCRIPT_DIR}"

echo "Generating Python protobuf files..."
echo "Proto base: ${PROTO_BASE}"

# Generate nanopb first (required)
protoc \
  --proto_path="${NANOPB_PROTO}" \
  --python_out="${GENERATED_DIR}" \
  "${NANOPB_PROTO}/nanopb.proto"

# Generate all common proto files
for proto in "${PROTO_BASE}/common"/*.proto; do
    echo "Generating $(basename $proto)..."
    protoc \
      --proto_path="${PROTO_BASE}/common" \
      --proto_path="${NANOPB_PROTO}" \
      --python_out="${GENERATED_DIR}" \
      "$proto"
done

# Generate rpi/host.proto (must be before message.proto)
echo "Generating rpi/host.proto..."
protoc \
  --proto_path="${PROTO_BASE}/common" \
  --proto_path="${PROTO_BASE}/rpi" \
  --proto_path="${NANOPB_PROTO}" \
  --python_out="${GENERATED_DIR}" \
  "${PROTO_BASE}/rpi/host.proto"

# Generate rpi/leds.proto (must be before message.proto)
echo "Generating rpi/leds.proto..."
protoc \
  --proto_path="${PROTO_BASE}/common" \
  --proto_path="${PROTO_BASE}/rpi" \
  --proto_path="${NANOPB_PROTO}" \
  --python_out="${GENERATED_DIR}" \
  "${PROTO_BASE}/rpi/leds.proto"

# Generate message.proto with all imports
echo "Generating rpi/message.proto..."
protoc \
  --proto_path="${PROTO_BASE}/common" \
  --proto_path="${PROTO_BASE}/rpi" \
  --proto_path="${NANOPB_PROTO}" \
  --python_out="${GENERATED_DIR}" \
  "${PROTO_BASE}/rpi/message.proto"

echo "Generated files:"
ls -la "${GENERATED_DIR}"/*.py

echo "Done!"
