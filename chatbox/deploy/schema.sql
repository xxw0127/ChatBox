CREATE DATABASE IF NOT EXISTS chatbox
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_general_ci;

USE chatbox;

CREATE TABLE IF NOT EXISTS users (
    id              BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    username        VARCHAR(32)     NOT NULL,
    nickname        VARCHAR(32)     NOT NULL,
    signature       VARCHAR(128)    NOT NULL DEFAULT '',
    avatar          MEDIUMBLOB      NULL,
    avatar_version  INT UNSIGNED    NOT NULL DEFAULT 0,
    password_hash   VARCHAR(255)    NOT NULL,
    status          TINYINT         NOT NULL DEFAULT 0 COMMENT '0正常 1禁用',
    created_at      DATETIME(3)     NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    last_login_at   DATETIME(3)     NULL,
    PRIMARY KEY (id),
    UNIQUE KEY uk_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

CREATE TABLE IF NOT EXISTS sessions (
    token_hash  CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    user_id     BIGINT UNSIGNED NOT NULL,
    expires_at  DATETIME        NOT NULL,
    created_at  DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    revoked     TINYINT         NOT NULL DEFAULT 0 COMMENT '0有效 1被新登录撤销 2主动登出',
    PRIMARY KEY (token_hash),
    KEY idx_user_id (user_id),
    KEY idx_user_revoked (user_id, revoked),
    KEY idx_expires_at (expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

CREATE TABLE IF NOT EXISTS rooms (
    id          INT UNSIGNED    NOT NULL AUTO_INCREMENT,
    name        VARCHAR(32)     NOT NULL,
    max_members INT UNSIGNED    NOT NULL DEFAULT 500,
    group_no    VARCHAR(12)     NOT NULL,
    owner_id    BIGINT UNSIGNED NOT NULL DEFAULT 0,
    is_default  TINYINT         NOT NULL DEFAULT 0,
    status      TINYINT         NOT NULL DEFAULT 0 COMMENT '0正常 1已解散',
    dissolved_at DATETIME(3)    NULL,
    created_at  DATETIME(3)     NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    PRIMARY KEY (id),
    UNIQUE KEY uk_group_no (group_no),
    KEY idx_room_status (status),
    KEY idx_room_owner (owner_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- Only the default hall remains. New users are inserted into room_members by
-- the registration transaction; max_members=0 means unlimited for this room.
INSERT IGNORE INTO rooms
    (id, name, max_members, group_no, owner_id, is_default, status)
VALUES
    (1, '群聊A', 0, '10000001', 4, 1, 0);

CREATE TABLE IF NOT EXISTS messages (
    id            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    room_id       INT UNSIGNED    NOT NULL,
    sender_id     BIGINT UNSIGNED NOT NULL,
    client_msg_id VARCHAR(36)     NOT NULL COMMENT '客户端 UUID v4，用于幂等',
    msg_type      TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0文本',
    content       VARCHAR(2000)   NOT NULL,
    reply_to_id   BIGINT UNSIGNED NOT NULL DEFAULT 0,
    reply_sender_name VARCHAR(32) NOT NULL DEFAULT '',
    reply_content VARCHAR(2000)   NOT NULL DEFAULT '',
    recalled      TINYINT         NOT NULL DEFAULT 0,
    created_at    DATETIME(3)     NOT NULL,
    PRIMARY KEY (id),
    KEY idx_room_id_id (room_id, id),
    KEY idx_created_at (created_at),
    UNIQUE KEY uk_room_sender_client (room_id, sender_id, client_msg_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

CREATE TABLE IF NOT EXISTS private_messages (
    id            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    sender_id     BIGINT UNSIGNED NOT NULL,
    receiver_id   BIGINT UNSIGNED NOT NULL,
    client_msg_id VARCHAR(36)     NOT NULL COMMENT '客户端 UUID v4，用于幂等',
    content       VARCHAR(2000)   NOT NULL,
    reply_to_id   BIGINT UNSIGNED NOT NULL DEFAULT 0,
    reply_sender_name VARCHAR(32) NOT NULL DEFAULT '',
    reply_content VARCHAR(2000)   NOT NULL DEFAULT '',
    recalled      TINYINT         NOT NULL DEFAULT 0,
    created_at    DATETIME(3)     NOT NULL,
    PRIMARY KEY (id),
    KEY idx_sender_receiver (sender_id, receiver_id, id),
    KEY idx_receiver_sender (receiver_id, sender_id, id),
    UNIQUE KEY uk_pair_sender_client (sender_id, receiver_id, client_msg_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

CREATE TABLE IF NOT EXISTS private_read_state (
    user_id              BIGINT UNSIGNED NOT NULL,
    peer_id              BIGINT UNSIGNED NOT NULL,
    last_read_message_id BIGINT UNSIGNED NOT NULL DEFAULT 0,
    updated_at           DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP
                         ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (user_id, peer_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

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

CREATE TABLE IF NOT EXISTS friend_requests (
    id           BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    requester_id BIGINT UNSIGNED NOT NULL,
    target_id    BIGINT UNSIGNED NOT NULL,
    status       TINYINT         NOT NULL DEFAULT 0 COMMENT '0待处理 1已接受 2已拒绝',
    created_at   DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at   DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP
                 ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_requester_target (requester_id, target_id),
    KEY idx_target_status (target_id, status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

CREATE TABLE IF NOT EXISTS friendships (
    user_low  BIGINT UNSIGNED NOT NULL,
    user_high BIGINT UNSIGNED NOT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (user_low, user_high)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

CREATE TABLE IF NOT EXISTS friend_remarks (
    owner_id  BIGINT UNSIGNED NOT NULL,
    friend_id BIGINT UNSIGNED NOT NULL,
    remark    VARCHAR(32)     NOT NULL,
    updated_at DATETIME       NOT NULL DEFAULT CURRENT_TIMESTAMP
              ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (owner_id, friend_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;
