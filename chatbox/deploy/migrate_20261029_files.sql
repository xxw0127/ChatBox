-- One-time Phase 2 migration. Run against the chatbox database after a backup.
-- Re-running the ALTER statements fails; table creation is idempotent.
USE chatbox;

ALTER TABLE messages
    ADD COLUMN file_id BIGINT UNSIGNED NOT NULL DEFAULT 0 AFTER msg_type;

ALTER TABLE private_messages
    ADD COLUMN msg_type TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER client_msg_id,
    ADD COLUMN file_id BIGINT UNSIGNED NOT NULL DEFAULT 0 AFTER msg_type;

CREATE TABLE IF NOT EXISTS files (
    id                  BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    uploader_id         BIGINT UNSIGNED NOT NULL,
    kind                TINYINT UNSIGNED NOT NULL COMMENT '1 room 2 private',
    room_id             INT UNSIGNED    NULL,
    private_sender_id   BIGINT UNSIGNED NULL,
    private_receiver_id BIGINT UNSIGNED NULL,
    message_id          BIGINT UNSIGNED NOT NULL DEFAULT 0,
    name                VARCHAR(180)    NOT NULL,
    size                BIGINT UNSIGNED NOT NULL,
    mime                VARCHAR(128)    NOT NULL DEFAULT '',
    sha256              CHAR(64)        NOT NULL,
    storage_path        VARCHAR(255)    NOT NULL,
    thumb_path          VARCHAR(255)    NOT NULL DEFAULT '',
    thumb_available     TINYINT         NOT NULL DEFAULT 0,
    width               INT UNSIGNED    NOT NULL DEFAULT 0,
    height              INT UNSIGNED    NOT NULL DEFAULT 0,
    duration_ms         INT UNSIGNED    NOT NULL DEFAULT 0,
    status              TINYINT         NOT NULL DEFAULT 0 COMMENT '0 ready 1 deleted',
    created_at          DATETIME(3)     NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    PRIMARY KEY (id),
    KEY idx_files_room_status (room_id, status, id),
    KEY idx_files_private_sender (private_sender_id, private_receiver_id, status, id),
    KEY idx_files_message (message_id),
    KEY idx_files_uploader (uploader_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

CREATE TABLE IF NOT EXISTS file_uploads (
    upload_id           CHAR(36)        NOT NULL,
    uploader_id         BIGINT UNSIGNED NOT NULL,
    kind                TINYINT UNSIGNED NOT NULL,
    room_id             INT UNSIGNED    NULL,
    private_sender_id   BIGINT UNSIGNED NULL,
    private_receiver_id BIGINT UNSIGNED NULL,
    name                VARCHAR(180)    NOT NULL,
    size                BIGINT UNSIGNED NOT NULL,
    mime                VARCHAR(128)    NOT NULL DEFAULT '',
    sha256              CHAR(64)        NOT NULL,
    temp_path           VARCHAR(255)    NOT NULL,
    offset              BIGINT UNSIGNED NOT NULL DEFAULT 0,
    status              TINYINT         NOT NULL DEFAULT 0 COMMENT '0 active 1 committed 2 cancelled',
    created_at          DATETIME(3)     NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at          DATETIME(3)     NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
    expires_at          DATETIME(3)     NOT NULL,
    committed_message_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    committed_file_id    BIGINT UNSIGNED NOT NULL DEFAULT 0,
    committed_at         DATETIME(3)     NULL,
    PRIMARY KEY (upload_id),
    KEY idx_file_uploads_uploader (uploader_id),
    KEY idx_file_uploads_expiry (status, expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- Additive upgrade for databases that already ran an earlier Phase 2 draft.
SET @ddl = (SELECT IF(COUNT(*) = 0,
    'ALTER TABLE file_uploads ADD COLUMN committed_message_id BIGINT UNSIGNED NOT NULL DEFAULT 0',
    'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'file_uploads'
    AND column_name = 'committed_message_id');
PREPARE stmt FROM @ddl; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @ddl = (SELECT IF(COUNT(*) = 0,
    'ALTER TABLE file_uploads ADD COLUMN committed_file_id BIGINT UNSIGNED NOT NULL DEFAULT 0',
    'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'file_uploads'
    AND column_name = 'committed_file_id');
PREPARE stmt FROM @ddl; EXECUTE stmt; DEALLOCATE PREPARE stmt;

SET @ddl = (SELECT IF(COUNT(*) = 0,
    'ALTER TABLE file_uploads ADD COLUMN committed_at DATETIME(3) NULL',
    'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'file_uploads'
    AND column_name = 'committed_at');
PREPARE stmt FROM @ddl; EXECUTE stmt; DEALLOCATE PREPARE stmt;
