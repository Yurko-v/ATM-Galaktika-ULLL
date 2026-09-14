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

-- Who is online as a controller, as of the last look at the feed - written by
-- the cron job, and by an endpoint that had to look for itself (lib/vatsim.php).
-- This is what stands in for an API key: a request is let in because the
-- position it claims is really controlling right now. Observers are not here.
CREATE TABLE IF NOT EXISTS network_controllers (
    callsign VARCHAR(20) NOT NULL,
    cid      VARCHAR(12) NOT NULL,
    PRIMARY KEY (callsign)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- The observers (facility 0) online, written alongside network_controllers.
-- Read by api/name.php alone: an OBS may open the plug-in's panel, but is
-- never let in to hand out a code.
CREATE TABLE IF NOT EXISTS network_observers (
    callsign VARCHAR(20) NOT NULL,
    cid      VARCHAR(12) NOT NULL,
    PRIMARY KEY (callsign)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- The name each controller is shown under on the Пользователь block, by CID.
-- Entered by the controller from the plug-in's Регистрация window when there is
-- none yet (POST api/name.php, which never overwrites a name), or by hand in
-- phpMyAdmin for the names the plug-in's own reading of the VATSIM name gets
-- wrong, or to give the patronymic the network never has.
-- Written out in full - "Велбовец Юрий Владимирович" - or already shortened,
-- "Велбовец Ю.В."; the plug-in shortens it either way. See api/name.php.
CREATE TABLE IF NOT EXISTS user_names (
    cid        VARCHAR(12)  NOT NULL,
    name       VARCHAR(100) NOT NULL,
    updated_at TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (cid)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Requests counted per position over a rolling minute; see check_rate_limit().
CREATE TABLE IF NOT EXISTS rate_limit (
    bucket       VARCHAR(40)  NOT NULL,
    window_start DATETIME     NOT NULL,
    hits         INT UNSIGNED NOT NULL,
    PRIMARY KEY (bucket)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Small key/value store: 'network_updated' and 'controllers_updated' are the
-- feed times of the last sync of each list, 'controllers_polled' the last time
-- an endpoint went to the feed itself.
CREATE TABLE IF NOT EXISTS sync_state (
    name  VARCHAR(32) NOT NULL,
    value VARCHAR(64) NOT NULL,
    PRIMARY KEY (name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
