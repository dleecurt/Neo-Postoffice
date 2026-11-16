-- Neo-PostOffice SQL mailbox: PostgreSQL schema.
--
-- Several users share one table. Each user logs in to PostgreSQL with
-- their own role and, through row-level security, sees only the rows
-- whose owner is that role. Mail is written by a separate delivery
-- role (used by getmail/fetchmail through po-sqldeliver), which may
-- insert mail for anyone but cannot read it.
--
-- Run as a database owner or superuser:
--
--   psql -d postoffice -f docs/sql-mailbox-postgresql.sql
--
-- Then create the logins, for example:
--
--   CREATE ROLE alice LOGIN PASSWORD '...' IN ROLE po_users;
--   CREATE ROLE getmail LOGIN PASSWORD '...' IN ROLE po_delivery;
--
-- A user's mail is delivered with "po-sqldeliver -u alice ...".

DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'po_users') THEN
        CREATE ROLE po_users NOLOGIN;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'po_delivery') THEN
        CREATE ROLE po_delivery NOLOGIN;
    END IF;
END
$$;

CREATE TABLE po_mail (
    id          bigint      GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    owner       name        NOT NULL DEFAULT current_user, -- the user's PostgreSQL role
    folder      text        NOT NULL DEFAULT 'Inbox',      -- shown as a folder in the client
    message_id  text,                                      -- Message-ID header
    from_name   text        NOT NULL DEFAULT '',
    from_addr   text        NOT NULL DEFAULT '',
    to_addr     text        NOT NULL DEFAULT '',
    subject     text        NOT NULL DEFAULT '',
    sent        timestamptz,                               -- Date header, if it could be read
    received    timestamptz NOT NULL DEFAULT now(),        -- when it was delivered
    size        integer     NOT NULL,                      -- length of raw in bytes
    seen        boolean     NOT NULL DEFAULT false,        -- set when the user opens it
    raw         bytea       NOT NULL                       -- the complete message as received
);
CREATE INDEX po_mail_owner_folder ON po_mail (owner, folder, id);

-- Row-level security: users see, mark and delete only their own mail.
ALTER TABLE po_mail ENABLE ROW LEVEL SECURITY;
CREATE POLICY po_mail_read   ON po_mail FOR SELECT TO po_users USING (owner = current_user);
CREATE POLICY po_mail_mark   ON po_mail FOR UPDATE TO po_users USING (owner = current_user)
                                                             WITH CHECK (owner = current_user);
CREATE POLICY po_mail_delete ON po_mail FOR DELETE TO po_users USING (owner = current_user);
CREATE POLICY po_mail_deliver ON po_mail FOR INSERT TO po_delivery WITH CHECK (true);

REVOKE ALL ON po_mail FROM PUBLIC;
GRANT SELECT, DELETE    ON po_mail TO po_users;
GRANT UPDATE (seen)     ON po_mail TO po_users;   -- users can change nothing else
GRANT INSERT (owner, folder, message_id, from_name, from_addr, to_addr,
              subject, sent, received, size, raw)
                        ON po_mail TO po_delivery; -- no SELECT: delivery cannot read mail
