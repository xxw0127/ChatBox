#!/usr/bin/env bash
set -euo pipefail
umask 077
backup_dir=/var/backups/chatbox
config=/etc/chatbox/backup.cnf
stamp=$(date -u +%Y%m%d-%H%M%S)
mkdir -p "${backup_dir}"
tmp="${backup_dir}/.chatbox-${stamp}.sql.gz.tmp"
trap 'rm -f "${tmp}"' EXIT
mysqldump --defaults-extra-file="${config}" \
    --single-transaction --quick --no-tablespaces --set-gtid-purged=OFF \
    --routines --triggers chatbox | gzip -9 > "${tmp}"
gzip -t "${tmp}"
mv "${tmp}" "${backup_dir}/chatbox-${stamp}.sql.gz"
find "${backup_dir}" -maxdepth 1 -type f -name 'chatbox-*.sql.gz' -mtime +7 -delete
