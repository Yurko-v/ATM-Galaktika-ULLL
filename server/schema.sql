-- Galaxy ATM System - squawk server schema (MySQL 5.7+ / MariaDB 10.2+).
-- Import once through phpMyAdmin or: mysql DBNAME < schema.sql

-- Every code ever handed out. A row is active while released_at is NULL.
-- The two generated columns are NULL on a released row, so the unique keys
-- below only bite on active rows: one aircraft holds at most one code, and a
-- code is held by at most one aircraft - two positions asking at the same
-- moment cannot both get it.
CREATE TABLE IF NOT EXISTS assignments (
    id               INT UNSIGNED NOT NULL AUTO_INCREMENT,
    callsign         VARCHAR(12)  NOT NULL,
    code             CHAR(4)      NOT NULL,
    assigned_by      VARCHAR(20)  NOT NULL,
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

-- The VATSIM network as of the last cron run: every pilot online, the code
-- they squawk and the code a controller gave them.
CREATE TABLE IF NOT EXISTS network_pilots (
    callsign             VARCHAR(12) NOT NULL,
    transponder          CHAR(4)     NULL,
    assigned_transponder CHAR(4)     NULL,
    latitude             DOUBLE      NOT NULL,
    longitude            DOUBLE      NOT NULL,
    PRIMARY KEY (callsign)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Small key/value store; 'network_updated' is the feed time of the last sync.
CREATE TABLE IF NOT EXISTS sync_state (
    name  VARCHAR(32) NOT NULL,
    value VARCHAR(64) NOT NULL,
    PRIMARY KEY (name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
