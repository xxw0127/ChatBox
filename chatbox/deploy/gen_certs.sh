#!/usr/bin/env bash
set -euo pipefail

CERT_DIR=/etc/chatbox/certs
sudo mkdir -p "${CERT_DIR}"

# CA 只在缺失时生成。重新生成 CA（删除 ca.crt/ca.key 后再运行本脚本）会让所有
# 现有客户端拒绝连接，必须同时把新的 ca.crt 分发到各客户端 certs 目录；
# 只轮换服务器证书时保留 ca.crt / ca.key 不动。
if [ ! -f "${CERT_DIR}/ca.crt" ]; then
    sudo openssl req -x509 -newkey rsa:2048 -sha256 -days 3650 -nodes \
        -keyout "${CERT_DIR}/ca.key" -out "${CERT_DIR}/ca.crt" \
        -subj "/CN=ChatBox Dev CA" \
        -addext "basicConstraints=critical,CA:TRUE" \
        -addext "keyUsage=critical,keyCertSign,cRLSign"
fi

# 覆盖服务器证书前备份；CA 始终保留，客户端不需要重新分发 ca.crt。
if [ -f "${CERT_DIR}/chat.crt" ]; then
    sudo cp -a "${CERT_DIR}/chat.crt" "${CERT_DIR}/chat.crt.$(date +%Y%m%d-%H%M%S).bak"
fi
if [ -f "${CERT_DIR}/chat.key" ]; then
    sudo cp -a "${CERT_DIR}/chat.key" "${CERT_DIR}/chat.key.$(date +%Y%m%d-%H%M%S).bak"
fi

sudo openssl req -newkey rsa:2048 -sha256 -nodes \
    -keyout "${CERT_DIR}/chat.key" -out "${CERT_DIR}/chat.csr" \
    -subj "/CN=chatbox.local"

# 服务器 LAN 地址固定为 192.168.2.240；换网段时用 SAN_IP=... 覆盖默认值。
# SAN_DNS 支持逗号分隔的多个域名，例如 chatbox.local,frp-oil.com。
# 只轮换服务器证书不会影响客户端已信任的 ca.crt。
SAN_IP=${SAN_IP:-192.168.2.240}
SAN_DNS=${SAN_DNS:-chatbox.local,frp-oil.com}

san="IP:${SAN_IP}"
IFS=',' read -ra dns_names <<< "${SAN_DNS}"
for name in "${dns_names[@]}"; do
    if [ -n "${name}" ]; then
        san="${san},DNS:${name}"
    fi
done

sudo tee "${CERT_DIR}/ext.cnf" >/dev/null <<EOF
subjectAltName=${san}
extendedKeyUsage=serverAuth
EOF

sudo openssl x509 -req -in "${CERT_DIR}/chat.csr" \
    -CA "${CERT_DIR}/ca.crt" -CAkey "${CERT_DIR}/ca.key" -CAcreateserial \
    -out "${CERT_DIR}/chat.crt" -days 825 -sha256 \
    -extfile "${CERT_DIR}/ext.cnf"

sudo chmod 600 "${CERT_DIR}/ca.key" "${CERT_DIR}/chat.key"
sudo chmod 644 "${CERT_DIR}/ca.crt" "${CERT_DIR}/chat.crt"
sudo rm -f "${CERT_DIR}/chat.csr" "${CERT_DIR}/ext.cnf" "${CERT_DIR}/ca.srl"

echo "certificates ready in ${CERT_DIR}"
