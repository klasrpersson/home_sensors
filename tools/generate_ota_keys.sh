#!/bin/sh
# Create the keys for over-the-air updates in keys/ at the repository root, which git ignores:
#
#   signing_key.pem      RSA-3072 key that signs app images. Devices only accept updates signed with it.
#                        Back it up: without it, devices can only be updated over USB.
#   ota_server_cert.pem  Self-signed certificate of the devices' update server, which tools/ota_push.py trusts
#   ota_server_key.pem   Its private key, built into the firmware
#   ota_token            Token that tools/ota_push.py sends to be allowed to update, built into the firmware
#
# Existing keys are kept, so devices keep accepting updates. Delete a file to replace it.
set -eu

keys="$(cd "$(dirname "$0")/.." && pwd)/keys"
mkdir -p "$keys"
chmod 700 "$keys"
umask 077

if [ ! -f "$keys/signing_key.pem" ]; then
    openssl genrsa -out "$keys/signing_key.pem" 3072
    echo "Created $keys/signing_key.pem"
fi

if [ ! -f "$keys/ota_server_key.pem" ] || [ ! -f "$keys/ota_server_cert.pem" ]; then
    openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 36500 \
        -subj "/CN=home-sensors-ota" -keyout "$keys/ota_server_key.pem" -out "$keys/ota_server_cert.pem"
    echo "Created $keys/ota_server_key.pem and $keys/ota_server_cert.pem"
fi

if [ ! -f "$keys/ota_token" ]; then
    openssl rand -hex 32 > "$keys/ota_token"
    echo "Created $keys/ota_token"
fi
