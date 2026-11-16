-- Tables for Neo-PostOffice's SQL mailbox type, MySQL/MariaDB version.
--
-- The SQL mailbox reads mail that another program has put into these
-- tables; tools/po-sqldeliver does that from a mail server. The layout is
-- reconstructed from the queries of PostOffice 0.7.3, whose own schema was
-- never published. Column names must stay as they are.
--
--   mysql postoffice < docs/sql-mailbox-mysql.sql

CREATE TABLE messages (
    msgid     INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    folder    VARCHAR(255) NOT NULL DEFAULT 'Inbox', -- PostOffice folder name
    namefrom  VARCHAR(255) NOT NULL DEFAULT '',      -- sender's display name
    mailfrom  VARCHAR(255) NOT NULL DEFAULT '',      -- sender's address
    mailto    TEXT         NOT NULL,                 -- recipient(s)
    subject   TEXT         NOT NULL,
    received  VARCHAR(64)  NOT NULL DEFAULT '',      -- shown as the message date
    msgread   TINYINT      NOT NULL DEFAULT 0,       -- set to 1 when opened
    body      LONGTEXT     NOT NULL,                 -- the whole raw message, encoded:
                                                     -- bytes other than letters, digits,
                                                     -- space and .,:;/!? as @ + 2 hex digits
    INDEX messages_folder (folder)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- One row per header line (filled by po-sqldeliver -H; used by the
-- original server-side tools, not by the PostOffice client).
CREATE TABLE headers (
    msgid  INT UNSIGNED NOT NULL,
    name   VARCHAR(255) NOT NULL,
    value  TEXT,
    INDEX headers_msgid (msgid),
    INDEX headers_name (name),
    FOREIGN KEY (msgid) REFERENCES messages (msgid) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
