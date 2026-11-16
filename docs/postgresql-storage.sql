-- Tables for Neo-PostOffice's PostgreSQL mail storage
-- ("Store mail in: PostgreSQL database" in a mailbox's settings).
--
-- PostOffice creates these itself the first time a mailbox uses them, if
-- the database user may create tables. Run this file instead when a
-- database administrator sets the database up in advance:
--
--   psql -d postoffice -f docs/postgresql-storage.sql
--
-- Rows belong to the database user that wrote them (owner), so several
-- people can share one database.

CREATE TABLE po_messages (
    owner text NOT NULL DEFAULT current_user,
    mailbox text NOT NULL,
    msgid text NOT NULL,
    parentid text,
    mailfrom text,
    namefrom text,
    subject text,
    msgdate text,
    folders text[] NOT NULL DEFAULT '{}',
    flags integer NOT NULL DEFAULT 0,
    readstate integer NOT NULL DEFAULT 0,
    partcount integer NOT NULL DEFAULT 0,
    received timestamptz,
    headers bytea,
    stored_size bigint NOT NULL DEFAULT 0,
    stored_parts integer NOT NULL DEFAULT 0,
    PRIMARY KEY (owner, mailbox, msgid)
);

CREATE TABLE po_parts (
    owner text NOT NULL,
    mailbox text NOT NULL,
    msgid text NOT NULL,
    partno integer NOT NULL,
    contenttype text,
    filename text,
    description text,
    contents bytea NOT NULL,
    PRIMARY KEY (owner, mailbox, msgid, partno),
    FOREIGN KEY (owner, mailbox, msgid) REFERENCES po_messages ON DELETE CASCADE ON UPDATE CASCADE
);
