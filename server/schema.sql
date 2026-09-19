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

-- Local KSA profiles, identified by VATSIM CID.
--
-- The CID and controller rating will be obtained from VATSIM OAuth in the
-- production version. The current development version accepts them manually.
-- The name fields are entered locally because the KSA displays the controller
-- name in Russian.
--
-- rating_id is VATSIM's own number for the rating - -1 INA, 0 SUS, 1 OBS,
-- 2 S1, and so on up to 12 ADM, the same numbers the data feed and the OAuth
-- profile use (lib/ratings.php). S1 and above may use the KSA; 0 is what a row
-- from before ratings is left at, and lets nobody in until it is corrected.
-- is_engineer grants access to the engineer panel.
--
-- On a server set up before ratings, add the two columns once, and then give
-- every controller already in the table their rating on the admin page:
--   ALTER TABLE user_names
--     ADD COLUMN rating_id   SMALLINT   NOT NULL DEFAULT 0 AFTER cid,
--     ADD COLUMN is_engineer TINYINT(1) NOT NULL DEFAULT 0 AFTER patronymic;
-- The first engineer has to be made by hand - the admin page cannot be opened
-- until there is one:
--   UPDATE user_names SET is_engineer = 1, rating_id = 5 WHERE cid = 'ВАШ_CID';
CREATE TABLE IF NOT EXISTS user_names (
    cid           VARCHAR(12)  NOT NULL,
    rating_id     SMALLINT     NOT NULL DEFAULT 0,
    name          VARCHAR(100) NOT NULL,
    surname       VARCHAR(40)  NULL,
    first_name    VARCHAR(40)  NULL,
    patronymic    VARCHAR(40)  NULL,
    is_engineer   TINYINT(1)   NOT NULL DEFAULT 0,
    registered_at DATETIME     NULL,
    updated_at    TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
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
