USE chatbox;

DROP PROCEDURE IF EXISTS migrate_multidevice_sessions;

DELIMITER //

CREATE PROCEDURE migrate_multidevice_sessions()
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM information_schema.COLUMNS
        WHERE TABLE_SCHEMA = DATABASE()
          AND TABLE_NAME = 'sessions'
          AND COLUMN_NAME = 'session_id'
    ) THEN
        ALTER TABLE sessions
            ADD COLUMN session_id CHAR(36) NULL AFTER token_hash;
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM information_schema.COLUMNS
        WHERE TABLE_SCHEMA = DATABASE()
          AND TABLE_NAME = 'sessions'
          AND COLUMN_NAME = 'device_id'
    ) THEN
        ALTER TABLE sessions
            ADD COLUMN device_id VARCHAR(36) NOT NULL DEFAULT '' AFTER session_id;
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM information_schema.COLUMNS
        WHERE TABLE_SCHEMA = DATABASE()
          AND TABLE_NAME = 'sessions'
          AND COLUMN_NAME = 'device_name'
    ) THEN
        ALTER TABLE sessions
            ADD COLUMN device_name VARCHAR(64) NOT NULL DEFAULT '' AFTER device_id;
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM information_schema.COLUMNS
        WHERE TABLE_SCHEMA = DATABASE()
          AND TABLE_NAME = 'sessions'
          AND COLUMN_NAME = 'platform'
    ) THEN
        ALTER TABLE sessions
            ADD COLUMN platform VARCHAR(16) NOT NULL DEFAULT '' AFTER device_name;
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM information_schema.COLUMNS
        WHERE TABLE_SCHEMA = DATABASE()
          AND TABLE_NAME = 'sessions'
          AND COLUMN_NAME = 'app_version'
    ) THEN
        ALTER TABLE sessions
            ADD COLUMN app_version VARCHAR(32) NOT NULL DEFAULT '' AFTER platform;
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM information_schema.COLUMNS
        WHERE TABLE_SCHEMA = DATABASE()
          AND TABLE_NAME = 'sessions'
          AND COLUMN_NAME = 'caps'
    ) THEN
        ALTER TABLE sessions
            ADD COLUMN caps INT UNSIGNED NOT NULL DEFAULT 0 AFTER app_version;
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM information_schema.COLUMNS
        WHERE TABLE_SCHEMA = DATABASE()
          AND TABLE_NAME = 'sessions'
          AND COLUMN_NAME = 'last_seen_at'
    ) THEN
        ALTER TABLE sessions
            ADD COLUMN last_seen_at DATETIME NULL AFTER caps;
    END IF;

    UPDATE sessions
    SET session_id = UUID()
    WHERE session_id IS NULL OR session_id = '';

    UPDATE sessions
    SET last_seen_at = created_at
    WHERE last_seen_at IS NULL;

    IF EXISTS (
        SELECT 1 FROM information_schema.COLUMNS
        WHERE TABLE_SCHEMA = DATABASE()
          AND TABLE_NAME = 'sessions'
          AND COLUMN_NAME = 'last_seen_at'
          AND IS_NULLABLE = 'YES'
    ) THEN
        ALTER TABLE sessions
            MODIFY COLUMN last_seen_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP;
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM information_schema.STATISTICS
        WHERE TABLE_SCHEMA = DATABASE()
          AND TABLE_NAME = 'sessions'
          AND INDEX_NAME = 'uk_session_id'
    ) THEN
        ALTER TABLE sessions ADD UNIQUE KEY uk_session_id (session_id);
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM information_schema.STATISTICS
        WHERE TABLE_SCHEMA = DATABASE()
          AND TABLE_NAME = 'sessions'
          AND INDEX_NAME = 'idx_user_device'
    ) THEN
        ALTER TABLE sessions ADD KEY idx_user_device (user_id, device_id);
    END IF;

    IF NOT EXISTS (
        SELECT 1 FROM information_schema.STATISTICS
        WHERE TABLE_SCHEMA = DATABASE()
          AND TABLE_NAME = 'sessions'
          AND INDEX_NAME = 'idx_user_seen'
    ) THEN
        ALTER TABLE sessions ADD KEY idx_user_seen (user_id, last_seen_at);
    END IF;
END//

DELIMITER ;

CALL migrate_multidevice_sessions();
DROP PROCEDURE migrate_multidevice_sessions;
