CREATE TABLE IF NOT EXISTS assignments (
    id               INT UNSIGNED NOT NULL AUTO_INCREMENT,
    callsign         VARCHAR(12)  NOT NULL,
    code             CHAR(4)      NOT NULL,
    assigned_by      VARCHAR(20)  NOT NULL,
    assigned_cid     VARCHAR(12)  NULL,
    assigned_at      DATETIME     NOT NULL,
    last_seen_online DATETIME     NULL,
    released_at      DATETIME     NULL,
    active_code      CHAR(4)      AS (IF(released_at IS NULL, code, NULL)) STORED,
    active_callsign  VARCHAR(12)  AS (IF(released_at IS NULL, callsign, NULL)) STORED,
    PRIMARY KEY (id),
    UNIQUE KEY uq_active_code (active_code),
    UNIQUE KEY uq_active_callsign (active_callsign),
    KEY idx_code_released (code, released_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS network_pilots (
    callsign             VARCHAR(12) NOT NULL,
    transponder          CHAR(4)     NULL,
    assigned_transponder CHAR(4)     NULL,
    latitude             DOUBLE      NOT NULL,
    longitude            DOUBLE      NOT NULL,
    PRIMARY KEY (callsign)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS network_controllers (
    callsign VARCHAR(20) NOT NULL,
    cid      VARCHAR(12) NOT NULL,
    PRIMARY KEY (callsign)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS network_observers (
    callsign VARCHAR(20) NOT NULL,
    cid      VARCHAR(12) NOT NULL,
    PRIMARY KEY (callsign)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS user_names (
    cid           VARCHAR(12)  NOT NULL,
    name          VARCHAR(100) NOT NULL,
    surname       VARCHAR(40)  NULL,
    first_name    VARCHAR(40)  NULL,
    patronymic    VARCHAR(40)  NULL,
    password_hash VARCHAR(255) NULL,
    registered_at DATETIME     NULL,
    updated_at    TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (cid)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS rate_limit (
    bucket       VARCHAR(40)  NOT NULL,
    window_start DATETIME     NOT NULL,
    hits         INT UNSIGNED NOT NULL,
    PRIMARY KEY (bucket)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS sync_state (
    name  VARCHAR(32) NOT NULL,
    value VARCHAR(64) NOT NULL,
    PRIMARY KEY (name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
