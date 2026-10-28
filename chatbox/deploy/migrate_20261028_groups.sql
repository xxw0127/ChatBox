-- One-time migration for durable groups. Run against the chatbox database
-- once, with a backup. This script is intentionally not idempotent: the
-- ALTER TABLE statements fail if it is run twice.
USE chatbox;

ALTER TABLE rooms
    DROP INDEX uk_name,
    ADD COLUMN group_no VARCHAR(12) NOT NULL DEFAULT '' AFTER max_members,
    ADD COLUMN owner_id BIGINT UNSIGNED NOT NULL DEFAULT 0 AFTER group_no,
    ADD COLUMN is_default TINYINT NOT NULL DEFAULT 0 AFTER owner_id,
    ADD COLUMN status TINYINT NOT NULL DEFAULT 0 AFTER is_default,
    ADD COLUMN dissolved_at DATETIME(3) NULL AFTER status;

INSERT IGNORE INTO rooms
    (id, name, max_members, group_no, owner_id, is_default, status)
VALUES
    (1, '群聊A', 0, '10000001', 0, 1, 0);

-- Keep room 1; mark every other seeded hall dissolved but retain its rows for
-- audit. Their group numbers must still be unique.
UPDATE rooms
SET group_no = '10000001',
    max_members = 0,
    is_default = 1,
    status = 0,
    dissolved_at = NULL
WHERE id = 1;

UPDATE rooms
SET group_no = CONCAT('9', LPAD(id, 7, '0')),
    max_members = 500,
    status = 1,
    dissolved_at = COALESCE(dissolved_at, NOW(3))
WHERE id <> 1;

SET @default_owner := (SELECT id FROM users WHERE id = 4);
SET @fallback_owner := (SELECT MIN(id) FROM users);
UPDATE rooms
SET owner_id = COALESCE(@default_owner, @fallback_owner, 0)
WHERE id = 1;

ALTER TABLE rooms ADD UNIQUE KEY uk_group_no (group_no);
ALTER TABLE rooms ADD KEY idx_room_status (status);
ALTER TABLE rooms ADD KEY idx_room_owner (owner_id);

CREATE TABLE IF NOT EXISTS room_members (
    room_id          INT UNSIGNED    NOT NULL,
    user_id          BIGINT UNSIGNED NOT NULL,
    role             TINYINT         NOT NULL DEFAULT 0 COMMENT '0成员 1管理员 2群主',
    joined_at        DATETIME(3)     NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    join_message_id  BIGINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (room_id, user_id),
    KEY idx_room_member_user (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

CREATE TABLE IF NOT EXISTS room_read_state (
    room_id              INT UNSIGNED    NOT NULL,
    user_id              BIGINT UNSIGNED NOT NULL,
    last_read_message_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    updated_at           DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP
                         ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (room_id, user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

CREATE TABLE IF NOT EXISTS room_join_requests (
    id           BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    room_id      INT UNSIGNED    NOT NULL,
    user_id      BIGINT UNSIGNED NOT NULL,
    message      VARCHAR(200)    NOT NULL DEFAULT '',
    status       TINYINT         NOT NULL DEFAULT 0 COMMENT '0待处理 1通过 2拒绝',
    created_at   DATETIME(3)     NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    reviewed_by  BIGINT UNSIGNED NOT NULL DEFAULT 0,
    reviewed_at  DATETIME(3)     NULL,
    PRIMARY KEY (id),
    KEY idx_join_room_status (room_id, status),
    KEY idx_join_user_status (user_id, status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

CREATE TABLE IF NOT EXISTS room_invites (
    id           BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    room_id      INT UNSIGNED    NOT NULL,
    inviter_id   BIGINT UNSIGNED NOT NULL,
    invitee_id   BIGINT UNSIGNED NOT NULL,
    status       TINYINT         NOT NULL DEFAULT 0 COMMENT '0待处理 1接受 2拒绝 3撤销',
    created_at   DATETIME(3)     NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    responded_at DATETIME(3)     NULL,
    PRIMARY KEY (id),
    KEY idx_invite_room_status (room_id, status),
    KEY idx_invite_invitee_status (invitee_id, status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- Every existing user is a member of the default hall; the configured owner
-- (user 4 when present, otherwise the earliest user) becomes group owner.
INSERT IGNORE INTO room_members
    (room_id, user_id, role, joined_at, join_message_id)
SELECT 1, id, 0, NOW(3), 0 FROM users;

UPDATE room_members rm
JOIN rooms r ON r.id = rm.room_id
SET rm.role = 2
WHERE rm.room_id = 1 AND rm.user_id = r.owner_id;

-- Existing users already saw the default hall's history. Keep it visible
-- (join cursor 0) but do not surface the entire backlog as unread.
SET @default_last_message :=
    (SELECT COALESCE(MAX(id), 0) FROM messages WHERE room_id = 1);
INSERT IGNORE INTO room_read_state
    (room_id, user_id, last_read_message_id)
SELECT 1, id, @default_last_message FROM users;
