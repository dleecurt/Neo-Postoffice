# The built-in SMTP client

Neo-PostOffice can send mail itself, straight to your provider's or your
own mail server, instead of handing it to sendmail or msmtp. This page
covers how a message is sent, how the client was tested, setting up a
server for it, and what its error messages mean. Choosing the client and
filling in an identity's server are covered in the README under
*Sending mail*.

The code is in `src/postoffice/smtpclient.cpp` (the protocol) and
`src/postoffice/outboxhandler.cpp` (the queue, retries and Drafts). TLS
is done by `NetStream`, the same as for POP3 and IMAP.

## How a message is sent

1. The identity is found by the message's sender (*Email From*); its
   *Outgoing mail server* settings are used.
2. The client connects: TLS from the start on port 465, or plain text
   upgraded with `STARTTLS` on 587. The certificate must be trusted (the
   system's CAs plus Config → Certificates) and must name the host you
   typed. There is no way to skip this check.
3. `EHLO` with this machine's full domain name, or its address in
   brackets. `HELO` is tried only on an unencrypted connection to a very
   old server.
4. Login, if the identity has a user name: SCRAM-SHA-256, SCRAM-SHA-1,
   PLAIN, LOGIN, in that order of preference, from what the server
   offers. A password is never sent without encryption.
5. The message is checked against what the server said it takes: its
   size against `SIZE`, 8-bit text against `8BITMIME`, non-ASCII
   addresses against `SMTPUTF8`.
6. `MAIL FROM`, then `RCPT TO` for every recipient (Bcc included). If
   any recipient is refused, the transaction is reset and nothing is
   sent, so nobody gets a copy that the others didn't.
7. `DATA`: the message with CRLF line ends and dot-escaping, then `QUIT`.

Waits: 5 minutes for each answer, 10 minutes after the end of the
message (some servers scan it for viruses before answering). **Stop**
cuts the connection at once.

## What happens on failure

Every failure falls into one of four kinds, and the outbox acts on it:

| Kind | Examples | Outbox |
|---|---|---|
| Passing | Server unreachable, connection lost, any 4xx answer that isn't about policy | Retried after 1, 5, 15, 30 minutes, then hourly, for three days; then moved to Drafts |
| Settings | Login refused, certificate not trusted, no STARTTLS offered, 5.7.x refusals (relaying denied, login needed) | Held in Queue until Check Mail |
| The message | Unknown recipient, too large, 8-bit or non-ASCII address the server won't take | Moved to OutBox → Drafts |
| Stopped | You pressed Stop | Held in Queue until Check Mail |

The 5.7.x rule follows RFC 3463: enhanced status codes in class 7 are
about security and policy, so they point at the settings rather than at
the message. Postfix, for instance, answers a submission without a login
with `554 5.7.1 Client host rejected: Access denied`; the client adds a
hint to set a user name and password when the server offers a login and
none was given.

## Testing

The client was tested in two ways.

**A scripted server** written for the purpose, which can be switched
into each awkward case: `4xx` on every command, login required, no
`AUTH`, a SCRAM server with too few rounds or a false proof, no
`8BITMIME`, a `SIZE` limit, a `554` greeting, no `EHLO`, no `STARTTLS`,
data injected after the `STARTTLS` answer, a dropped connection during
`DATA`, and a slow answer after the final dot.

**A real Postfix 3.11.7** built from source with Cyrus SASL 2.1.28
(logins from a `sasldb` file) and delivering into Maildirs, with:

- port 465 (`smtpd_tls_wrappermode=yes`) and 587
  (`smtpd_tls_security_level=encrypt`), both requiring a login;
- port 25 style, no login, delivering only to local domains.

Results:

| Case | Result |
|---|---|
| TLS on 465 with a login | Sent; Postfix logged `sasl_method=SCRAM-SHA-256` |
| STARTTLS on 587, two recipients | Sent to both |
| No encryption, no login, local recipient | Sent |
| Each method alone (SCRAM-SHA-256, SCRAM-SHA-1, PLAIN, LOGIN) | Sent; Postfix logged the method used |
| Wrong password | Refused (535 5.7.8); settings kind, held |
| Submission without a login | Refused (554 5.7.1); settings kind, with the login hint |
| Relaying from port 25 | Refused (554 5.7.1 Relay access denied); settings kind |
| One unknown recipient among good ones | Nothing sent; message kind, to Drafts |
| Message over `message_size_limit` | Refused before sending; message kind |
| Non-ASCII address, server without SMTPUTF8 | Refused before sending; message kind |
| Login set but no encryption | Refused before connecting |
| CA not in the store | Refused, pointing to Config → Certificates |
| Certificate for another host name | Refused: hostname mismatch |
| Body with lone dots, leading dots, CRLF, a lone CR, UTF-8, no final newline | Delivered identical to the original |
| From the app, through the outbox | Delivered with correct `Date`, `Message-ID` and headers |
| From the app with Postfix stopped, then started and Check Mail | Queued with a retry time, then sent; retry list emptied |

All of it ran under AddressSanitizer and UndefinedBehaviorSanitizer with
no reports.

Not tested yet: OAuth2 (not supported), and servers at real providers.
Reports from your own server are welcome.

## Setting up your own server

### Postfix

A submission service on 587 and 465 in `master.cf`:

```
submission inet n - n - - smtpd
  -o syslog_name=postfix/submission
  -o smtpd_tls_security_level=encrypt
  -o smtpd_sasl_auth_enable=yes
  -o smtpd_tls_auth_only=yes
  -o smtpd_client_restrictions=permit_sasl_authenticated,reject
submissions inet n - n - - smtpd
  -o syslog_name=postfix/submissions
  -o smtpd_tls_wrappermode=yes
  -o smtpd_sasl_auth_enable=yes
  -o smtpd_client_restrictions=permit_sasl_authenticated,reject
```

(`submissions` is called `smtps` in older `/etc/services` files.) In
`main.cf`, `smtpd_tls_cert_file` and `smtpd_tls_key_file` point at the
certificate, and `smtpd_sasl_type` is `dovecot` or `cyrus`.

Which login methods are offered depends on the SASL side. With Cyrus
SASL, `mech_list` in `smtpd.conf` chooses them; SCRAM needs the
passwords stored in a form SCRAM can use (`sasldb` does this). With
Dovecot, `auth_mechanisms` chooses them, and SCRAM works only when the
password database holds passwords in a SCRAM scheme or in plain text.
Otherwise the client uses PLAIN inside TLS, which is fine.

If the certificate comes from your own CA, import the CA certificate in
Config → Certificates; the host name in the identity must be one the
certificate names.

### Providers

- **Gmail**: `smtp.gmail.com`, TLS on 465, your address as user name and
  an *app password* (needs two-step verification on the account).
- **Fastmail**: `smtp.fastmail.com`, TLS on 465, with an app password.
- **Microsoft 365 / Outlook.com**: needs OAuth2, which is not supported.
  Use msmtp with an OAuth2 helper, or your system MTA, instead.

Most other providers publish a host for 465 or 587 and accept your mail
address and password (or an app password).

## Error messages

| Message (shortened) | What to do |
|---|---|
| cannot reach the mail server *host* | Check the host name and port; the message is retried |
| the certificate of *host* is not trusted | If your own CA issued it, import the CA in Config → Certificates |
| hostname mismatch | Use the host name the certificate was issued for |
| *host* does not offer STARTTLS | Use TLS on 465, or another server |
| refused the login for *user* | Check the user name and password (or Password cmd) |
| has a login … but no encryption | Choose TLS or STARTTLS for the identity |
| refused *address* (5.7.x …) | The server wants a login, or this login may not send as this sender |
| refused *address* (550 5.1.1 …) | The address is wrong; fix it in Drafts and send again |
| the message is *N* KB and *host* takes at most *M* KB | Send smaller attachments |
| non-ASCII characters … (no SMTPUTF8) | Use an ASCII address, or another server |
| cannot get the password for *identity* | Run the Password cmd by hand and check its first line of output |
| … no outgoing mail server … | Fill in Config → Identities → *Outgoing mail server* |
| has a user name … but no password | Enter the password or a Password cmd |
