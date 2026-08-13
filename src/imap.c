#include "imap.h"
#include "codec.h"
#include "imap_parser.h"
#include "i18n.h"

#define T(de, en) amg_tr((de), (en))

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if AMIGMAIL_AMIGA
#include <dos/dos.h>
#include <proto/dos.h>
#else
#include <time.h>
#endif

static const char imap_message_list_fetch_items[] =
    "(UID FLAGS RFC822.SIZE INTERNALDATE "
    "BODY.PEEK[HEADER.FIELDS (FROM TO SUBJECT DATE MESSAGE-ID REFERENCES "
    "IN-REPLY-TO CONTENT-TYPE)] X-GM-MSGID X-GM-THRID X-GM-LABELS)";

static int ascii_ci_contains(const char *text, const char *needle)
{
    size_t length = strlen(needle);
    for (; *text; ++text) {
        size_t i;
        for (i = 0; i < length && text[i] && tolower((unsigned char)text[i]) == tolower((unsigned char)needle[i]); ++i) {}
        if (i == length) return 1;
    }
    return 0;
}

static int ascii_ci_equal(const char *left, const char *right)
{
    while (*left && *right) {
        if (tolower((unsigned char)*left) !=
            tolower((unsigned char)*right))
            return 0;
        ++left;
        ++right;
    }
    return !*left && !*right;
}

static void set_parser_error(AmgError *error, int result,
                             const AmgImapParser *parser)
{
    char message[256];
    if (result != AMG_ERR_LIMIT || !parser) {
        amg_error_set(error, result,
                      T("IMAP-Antwort hat ein ung\303\274ltiges Format.", "IMAP response has an invalid format."));
        return;
    }
    switch (parser->failure) {
        case AMG_IMAP_PARSER_FAILURE_LINE_LIMIT:
            amg_tr_snprintf(message, sizeof(message),
                            "IMAP-Limit: Zeile %lu Bytes, erlaubt %lu Bytes.",
                            "IMAP limit: line %lu bytes, allowed %lu bytes.",
                            (unsigned long)parser->failure_size,
                            (unsigned long)parser->failure_limit);
            break;
        case AMG_IMAP_PARSER_FAILURE_LITERAL_LIMIT:
            if (parser->failure_size == SIZE_MAX)
                snprintf(message, sizeof(message), "%s",
                         T("IMAP-Limit: ung\303\274ltige Datenblock-Gr\303\266\303\237e.",
                           "IMAP limit: invalid literal size."));
            else
                amg_tr_snprintf(message, sizeof(message),
                                "IMAP-Limit: Datenblock %lu Bytes, erlaubt %lu Bytes.",
                                "IMAP limit: literal %lu bytes, allowed %lu bytes.",
                                (unsigned long)parser->failure_size,
                                (unsigned long)parser->failure_limit);
            break;
        case AMG_IMAP_PARSER_FAILURE_BUFFER_LIMIT:
            amg_tr_snprintf(message, sizeof(message),
                            "IMAP-Limit: Puffer %lu Bytes, erlaubt %lu Bytes.",
                            "IMAP limit: buffer %lu bytes, allowed %lu bytes.",
                            (unsigned long)parser->failure_size,
                            (unsigned long)parser->failure_limit);
            break;
        default:
            snprintf(message, sizeof(message), "%s",
                     T("IMAP-Limit wurde \303\274berschritten.",
                       "IMAP limit was exceeded."));
            break;
    }
    amg_error_set(error, result, message);
}

void amg_imap_session_init(AmgImapSession *session)
{
    if (!session) return;
    memset(session, 0, sizeof(*session));
    session->tag_counter = 1;
    strcpy(session->special_mailboxes[0], "INBOX");
}

void amg_imap_disconnect(AmgImapSession *session)
{
    if (!session) return;
    if (session->connection) {
        AmgError ignored;
        amg_tls_write_all(session->connection, "ZZZZ LOGOUT\r\n", 13U, &ignored);
        amg_tls_close(session->connection);
    }
    amg_imap_session_init(session);
}

static int imap_collect(AmgImapSession *session, const char *tag, AmgBuffer *response, AmgError *error)
{
    AmgImapParser parser;
    AmgImapEvent event;
    unsigned char input[4096];
    char rejection[192];
    int done = 0, ok = 0, result = AMG_OK;
    rejection[0] = 0;
    amg_imap_parser_init(&parser);
    while (!done) {
        long count = amg_tls_read(session->connection, input, sizeof(input), error);
        if (count <= 0) { result = AMG_ERR_IO; break; }
        result = amg_imap_parser_feed(&parser, input, (size_t)count);
        if (result != AMG_OK) {
            set_parser_error(error, result, &parser);
            break;
        }
        while ((result = amg_imap_parser_next(&parser, &event)) > 0) {
            if (response && amg_buffer_append(response, event.data, event.length) != AMG_OK) {
                result = AMG_ERR_MEMORY;
                amg_error_set(error, result,
                              T("Nicht genug Speicher f\303\274r die IMAP-Antwort.", "Not enough memory for the IMAP response."));
                done = 1;
                break;
            }
            if (event.type == AMG_IMAP_EVENT_LINE && event.length >= strlen(tag) + 4U &&
                !memcmp(event.data, tag, strlen(tag)) && event.data[strlen(tag)] == ' ') {
                const unsigned char *status = event.data + strlen(tag) + 1U;
                ok = event.length >= strlen(tag) + 4U && !memcmp(status, "OK", 2U);
                if (!ok) {
                    const unsigned char *detail = status + 2U;
                    size_t detail_length = event.length -
                        (size_t)(detail - event.data);
                    while (detail_length && (*detail == ' ' || *detail == '\t')) {
                        ++detail;
                        --detail_length;
                    }
                    while (detail_length &&
                           (detail[detail_length - 1U] == '\r' ||
                            detail[detail_length - 1U] == '\n'))
                        --detail_length;
                    if (detail_length >= sizeof(rejection))
                        detail_length = sizeof(rejection) - 1U;
                    if (detail_length)
                        memcpy(rejection, detail, detail_length);
                    rejection[detail_length] = 0;
                }
                done = 1; break;
            }
        }
        if (result < 0) {
            if (parser.failed)
                set_parser_error(error, result, &parser);
            else if (!error || error->code == AMG_OK)
                set_parser_error(error, result, NULL);
            break;
        }
        result = AMG_OK;
    }
    amg_imap_parser_free(&parser);
    if (result != AMG_OK) return result;
    if (!ok) {
        if (rejection[0]) {
            char message[256];
            amg_tr_snprintf(message, sizeof(message),
                            "IMAP-Server: %s", "IMAP server: %s",
                            rejection);
            amg_error_set(error, AMG_ERR_PROTOCOL, message);
        } else {
            amg_error_set(error, AMG_ERR_PROTOCOL,
                          T("Der IMAP-Server hat den Befehl abgelehnt.", "The IMAP server rejected the command."));
        }
        return AMG_ERR_PROTOCOL;
    }
    return AMG_OK;
}

static int imap_command(AmgImapSession *session, const char *command, AmgBuffer *response, AmgError *error)
{
    char tag[16]; AmgBuffer wire; int result;
    if (!session || !session->connection || !command) return AMG_ERR_ARGUMENT;
    snprintf(tag, sizeof(tag), "A%06lu", session->tag_counter++);
    amg_buffer_init(&wire); amg_buffer_append_cstr(&wire, tag); amg_buffer_append_char(&wire, ' ');
    amg_buffer_append_cstr(&wire, command); amg_buffer_append_cstr(&wire, "\r\n");
    result = amg_tls_write_all(session->connection, wire.data, wire.length, error); amg_buffer_free(&wire);
    return result == AMG_OK ? imap_collect(session, tag, response, error) : result;
}

static int read_greeting(AmgImapSession *session, AmgError *error)
{
    AmgBuffer greeting;
    unsigned char input[1024];
    int result = AMG_OK;
    amg_buffer_init(&greeting);
    for (;;) {
        long count = amg_tls_read(session->connection, input, sizeof(input), error);
        if (count <= 0) {
            result = count < 0 ? (int)count : AMG_ERR_IO;
            if (!error || error->code == AMG_OK)
                amg_error_set(
                    error, result,
                    T("Gmail hat nach dem TLS-Aufbau keine IMAP-Daten gesendet.", "Gmail sent no IMAP data after the TLS connection was established."));
            break;
        }
        result = amg_buffer_append(&greeting, input, (size_t)count);
        if (result != AMG_OK) {
            amg_error_set(error, result,
                          T("IMAP-Begr\303\274\303\237ung ist zu lang.", "IMAP greeting is too long."));
            break;
        }
        if (amg_imap_greeting_status(greeting.data, greeting.length) > 0) {
            result = AMG_OK;
            break;
        }
        if (amg_imap_greeting_status(greeting.data, greeting.length) < 0) {
            size_t length = greeting.length;
            char detail[190], message[256];
            while (length && (greeting.data[length - 1U] == '\r' ||
                              greeting.data[length - 1U] == '\n'))
                --length;
            if (length >= sizeof(detail)) length = sizeof(detail) - 1U;
            memcpy(detail, greeting.data, length);
            detail[length] = 0;
            amg_tr_snprintf(message, sizeof(message),
                            "IMAP-Server meldet: %s",
                            "IMAP server reports: %s", detail);
            amg_error_set(error, AMG_ERR_PROTOCOL, message);
            result = AMG_ERR_PROTOCOL;
            break;
        }
        if (greeting.length > AMIGMAIL_MAX_LINE) {
            amg_error_set(error, AMG_ERR_PROTOCOL,
                          T("Gmail sendet keine g\303\274ltige IMAP-Begr\303\274\303\237ung.", "Gmail did not send a valid IMAP greeting."));
            result = AMG_ERR_PROTOCOL;
            break;
        }
    }
    amg_buffer_free(&greeting);
    return result;
}

static int build_auth(const AmgAccount *account, const char *access_token,
                      AmgBuffer *command, AmgError *error)
{
    AmgBuffer raw, encoded; int result = AMG_OK;
    amg_buffer_init(&raw); amg_buffer_init(&encoded);
    if (account->auth_mode == AMG_AUTH_OAUTH2) {
        if (!access_token) {
            result = AMG_ERR_AUTH;
            amg_error_set(error, result, T("OAuth-Zugriffstoken fehlt.", "OAuth access token is missing."));
        }
        else {
            amg_buffer_append_cstr(&raw, "user="); amg_buffer_append_cstr(&raw, account->email); amg_buffer_append_char(&raw, 1);
            amg_buffer_append_cstr(&raw, "auth=Bearer "); amg_buffer_append_cstr(&raw, access_token); amg_buffer_append_char(&raw, 1); amg_buffer_append_char(&raw, 1);
            result = amg_base64_encode(raw.data, raw.length, &encoded);
            if (result == AMG_OK) { amg_buffer_append_cstr(command, "AUTHENTICATE XOAUTH2 "); amg_buffer_append(command, encoded.data, encoded.length); }
        }
    } else {
        if (!account->app_password) {
            result = AMG_ERR_AUTH;
            amg_error_set(error, result, T("Gmail-App-Passwort fehlt.", "Gmail app password is missing."));
        }
        else {
            amg_buffer_append_char(&raw, 0); amg_buffer_append_cstr(&raw, account->email); amg_buffer_append_char(&raw, 0); amg_buffer_append_cstr(&raw, account->app_password);
            result = amg_base64_encode(raw.data, raw.length, &encoded);
            if (result == AMG_OK) { amg_buffer_append_cstr(command, "AUTHENTICATE PLAIN "); amg_buffer_append(command, encoded.data, encoded.length); }
        }
    }
    if (result != AMG_OK && (!error || error->code == AMG_OK))
        amg_error_set(error, result,
                      T("Gmail-Anmeldung konnte nicht vorbereitet werden.", "Gmail login could not be prepared."));
    amg_secure_clear(raw.data, raw.capacity); amg_secure_clear(encoded.data, encoded.capacity);
    amg_buffer_free(&raw); amg_buffer_free(&encoded); return result;
}

int amg_imap_connect(AmgImapSession *session, const AmgAccount *account,
                     const char *access_token, AmgError *error)
{
    AmgBuffer response, command; int result;
    if (!session || !account) return AMG_ERR_ARGUMENT;
    session->connection = amg_tls_connect(account->imap_host, account->imap_port, 30U, error);
    if (!session->connection) return error ? error->code : AMG_ERR_TLS;
    result = read_greeting(session, error); if (result != AMG_OK) goto fail;
    amg_buffer_init(&response); amg_buffer_init(&command);
    result = imap_command(session, "CAPABILITY", &response, error);
    if (result == AMG_OK) {
        amg_buffer_terminate(&response);
        session->capability_move = ascii_ci_contains((const char *)response.data, " MOVE");
        session->capability_uidplus =
            ascii_ci_contains((const char *)response.data, " UIDPLUS");
        session->capability_special_use = ascii_ci_contains((const char *)response.data, " SPECIAL-USE");
        session->capability_x_gm_ext1 = ascii_ci_contains((const char *)response.data, " X-GM-EXT-1");
    }
    if (result == AMG_OK)
        result = build_auth(account, access_token, &command, error);
    if (result == AMG_OK) { amg_buffer_terminate(&command); response.length = 0; result = imap_command(session, (const char *)command.data, &response, error); }
    amg_secure_clear(command.data, command.capacity); amg_buffer_free(&command); amg_buffer_free(&response);
    if (result == AMG_OK) { session->authenticated = 1; return AMG_OK; }
fail:
    amg_tls_close(session->connection); session->connection = NULL; return result;
}

static unsigned long special_flag(const char *flags)
{
    unsigned long result = 0;
    if (ascii_ci_contains(flags,"\\Inbox")) result|=AMG_LABEL_INBOX;
    if (ascii_ci_contains(flags,"\\Sent")) result|=AMG_LABEL_SENT;
    if (ascii_ci_contains(flags,"\\Drafts")) result|=AMG_LABEL_DRAFTS;
    if (ascii_ci_contains(flags,"\\Trash")) result|=AMG_LABEL_TRASH;
    if (ascii_ci_contains(flags,"\\Junk") ||
        ascii_ci_contains(flags,"\\Spam")) result|=AMG_LABEL_SPAM;
    if (ascii_ci_contains(flags,"\\All") ||
        ascii_ci_contains(flags,"\\AllMail")) result|=AMG_LABEL_ALL;
    if (ascii_ci_contains(flags,"\\Flagged") ||
        ascii_ci_contains(flags,"\\Starred")) result|=AMG_LABEL_FLAGGED;
    return result;
}

static unsigned long special_flag_from_name(const char *name_utf8,
                                            char delimiter)
{
    const char *leaf = name_utf8 ? name_utf8 : "";
    const char *cursor = leaf;
    if (delimiter) {
        while (*cursor) {
            if (*cursor == delimiter) leaf = cursor + 1;
            ++cursor;
        }
    }
    if (ascii_ci_equal(leaf, "INBOX") ||
        ascii_ci_equal(leaf, "Posteingang")) return AMG_LABEL_INBOX;
    if (ascii_ci_equal(leaf, "Sent") ||
        ascii_ci_equal(leaf, "Sent Mail") ||
        ascii_ci_equal(leaf, "Gesendet")) return AMG_LABEL_SENT;
    if (ascii_ci_equal(leaf, "Drafts") ||
        !strcmp(leaf, "Entw\303\274rfe")) return AMG_LABEL_DRAFTS;
    if (ascii_ci_equal(leaf, "Trash") || ascii_ci_equal(leaf, "Bin") ||
        ascii_ci_equal(leaf, "Papierkorb")) return AMG_LABEL_TRASH;
    if (ascii_ci_equal(leaf, "Spam") ||
        ascii_ci_equal(leaf, "Junk")) return AMG_LABEL_SPAM;
    if (ascii_ci_equal(leaf, "All Mail") ||
        ascii_ci_equal(leaf, "Alle Nachrichten")) return AMG_LABEL_ALL;
    if (ascii_ci_equal(leaf, "Starred") ||
        ascii_ci_equal(leaf, "Flagged") ||
        ascii_ci_equal(leaf, "Markiert")) return AMG_LABEL_FLAGGED;
    return 0;
}

static void remember_special_mailboxes(AmgImapSession *session,
                                       const AmgImapLabel *label)
{
    static const unsigned long flags[7] = {
        AMG_LABEL_INBOX, AMG_LABEL_SENT, AMG_LABEL_DRAFTS,
        AMG_LABEL_TRASH, AMG_LABEL_SPAM, AMG_LABEL_ALL, AMG_LABEL_FLAGGED
    };
    size_t i;
    if (!session || !label || !label->name_utf8[0]) return;
    for (i = 0; i < 7U; ++i) {
        if (label->special_use & flags[i]) {
            snprintf(session->special_mailboxes[i],
                     sizeof(session->special_mailboxes[i]), "%s",
                     label->name_utf8);
        }
    }
}

static const char *resolve_special_mailbox(const AmgImapSession *session,
                                           const char *mailbox_utf8)
{
    static const char *aliases[7] = {
        "\\Inbox", "\\Sent", "\\Drafts", "\\Trash",
        "\\Spam", "\\AllMail", "\\Starred"
    };
    size_t i;
    if (!session || !mailbox_utf8) return mailbox_utf8;
    for (i = 0; i < 7U; ++i) {
        if (ascii_ci_equal(mailbox_utf8, aliases[i]) &&
            session->special_mailboxes[i][0])
            return session->special_mailboxes[i];
    }
    return mailbox_utf8;
}

static const char *parse_quoted(const char *p, AmgBuffer *value)
{
    if (*p != '"') return NULL;
    ++p;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) ++p;
        if (amg_buffer_append_char(value,(unsigned char)*p++)!=AMG_OK) return NULL;
    }
    return *p == '"' ? p + 1 : NULL;
}

static int parse_list_line(const char *line, AmgImapLabel *label)
{
    const char *p, *end; AmgBuffer wire, decoded;
    if (strncmp(line,"* LIST ",7U) && strncmp(line,"* XLIST ",8U)) return 0;
    p = strchr(line,'('); if(!p) return 0; end=strchr(p,')'); if(!end)return 0;
    { char flags[256]; size_t n=(size_t)(end-p-1);if(n>=sizeof(flags))n=sizeof(flags)-1U;memcpy(flags,p+1,n);flags[n]=0;label->special_use=special_flag(flags); }
    p=end+1;while(*p==' ')++p;
    if(*p=='"'){ if(!p[1]||p[2]!='"')return 0;label->delimiter=p[1];p+=3; } else if(!strncmp(p,"NIL",3U)){label->delimiter=0;p+=3;} else return 0;
    while (*p == ' ') ++p;
    amg_buffer_init(&wire);
    amg_buffer_init(&decoded);
    if(*p=='"'){if(!parse_quoted(p,&wire)){amg_buffer_free(&wire);amg_buffer_free(&decoded);return 0;}}
    else {const char *q=p;while(*q&&*q!='\r'&&*q!='\n'&&!isspace((unsigned char)*q))++q;amg_buffer_append(&wire,p,(size_t)(q-p));}
    amg_buffer_terminate(&wire);if(amg_modified_utf7_decode((const char*)wire.data,&decoded)!=AMG_OK){amg_buffer_free(&wire);amg_buffer_free(&decoded);return 0;}
    amg_buffer_terminate(&decoded);snprintf(label->wire_name,sizeof(label->wire_name),"%s",(const char*)wire.data);
    snprintf(label->name_utf8,sizeof(label->name_utf8),"%s",(const char*)decoded.data);
    if (!strcmp(label->wire_name,"INBOX")) label->special_use|=AMG_LABEL_INBOX;
    label->special_use |= special_flag_from_name(label->name_utf8,
                                                 label->delimiter);
    amg_buffer_free(&wire);amg_buffer_free(&decoded);return 1;
}

int amg_imap_list_labels(AmgImapSession *session, AmgImapLabel *labels,
                         size_t capacity, size_t *count, AmgError *error)
{
    AmgBuffer response; char *cursor; size_t found=0; int result;
    if (!session || !labels || !count) return AMG_ERR_ARGUMENT;
    amg_buffer_init(&response);
    /* Gmail liefert die RFC-6154-Sonderattribute (u.a. \\Drafts und
     * \\Junk fuer Spam) bereits bei normalem LIST. XLIST bleibt nur der
     * Kompatibilitaets-Fallback fuer alte Gmail-Konfigurationen. */
    result=imap_command(session,"LIST \"\" \"*\"",&response,error);
    if(result!=AMG_OK){response.length=0;result=imap_command(session,"LIST \"\" \"*\" RETURN (SPECIAL-USE)",&response,error);}
    if(result!=AMG_OK){response.length=0;result=imap_command(session,"XLIST \"\" \"*\"",&response,error);}
    if(result==AMG_OK){amg_buffer_terminate(&response);cursor=(char*)response.data;
        while(*cursor&&found<capacity){char *next=strstr(cursor,"\r\n");if(!next)break;*next=0;if(parse_list_line(cursor,&labels[found])){remember_special_mailboxes(session,&labels[found]);++found;}cursor=next+2;}}
    amg_buffer_free(&response);*count=found;return result;
}

static int quote_mailbox(const char *utf8, AmgBuffer *output)
{
    AmgBuffer wire; size_t i; int result;
    amg_buffer_init(&wire);result=amg_modified_utf7_encode(utf8,&wire);if(result!=AMG_OK){amg_buffer_free(&wire);return result;}
    amg_buffer_append_char(output,'"');for(i=0;i<wire.length;++i){if(wire.data[i]=='"'||wire.data[i]=='\\')amg_buffer_append_char(output,'\\');amg_buffer_append_char(output,wire.data[i]);}
    result=amg_buffer_append_char(output,'"');amg_buffer_free(&wire);return result;
}

int amg_imap_select(AmgImapSession *session, const char *mailbox_utf8, AmgError *error)
{
    const char *resolved_mailbox;
    AmgBuffer command,response;unsigned long exists=0;int result;
    if (!session || !mailbox_utf8 || !*mailbox_utf8)
        return AMG_ERR_ARGUMENT;
    resolved_mailbox=resolve_special_mailbox(session,mailbox_utf8);amg_buffer_init(&command);amg_buffer_init(&response);amg_buffer_append_cstr(&command,"SELECT ");
    result=quote_mailbox(resolved_mailbox,&command);if(result==AMG_OK){amg_buffer_terminate(&command);result=imap_command(session,(char*)command.data,&response,error);}
    if(result==AMG_OK){amg_buffer_terminate(&response);if(!amg_imap_parse_exists(response.data,response.length,&exists)){result=AMG_ERR_PROTOCOL;amg_error_set(error,result,T("Gmail hat keine Nachrichtenanzahl f\303\274r den Ordner geliefert.", "Gmail did not provide a message count for the folder."));}}
    if(result==AMG_OK){session->selected_exists=exists;snprintf(session->selected_mailbox,sizeof(session->selected_mailbox),"%s",resolved_mailbox);}
    amg_buffer_free(&command);amg_buffer_free(&response);return result;
}

int amg_imap_fetch_page(AmgImapSession *session, unsigned long before_uid,
                        size_t limit, AmgBuffer *response, AmgError *error)
{
    AmgBuffer mapping;
    char command[512];
    unsigned long first, last;
    int result = AMG_OK;
    if (!session || !response || !limit) return AMG_ERR_ARGUMENT;
    last = session->selected_exists;
    if (before_uid) {
        unsigned long sequence = 0;
        amg_buffer_init(&mapping);
        snprintf(command, sizeof(command), "UID FETCH %lu (UID)", before_uid);
        result = imap_command(session, command, &mapping, error);
        if (result == AMG_OK) {
            amg_buffer_terminate(&mapping);
            if (!amg_imap_parse_fetch_sequence(mapping.data, mapping.length,
                                                before_uid, &sequence)) {
                result = AMG_ERR_PROTOCOL;
                amg_error_set(error, result,
                              T("Position der n\303\244chsten Nachrichtenseite fehlt.", "Position of the next message page is missing."));
            } else {
                last = sequence > 1UL ? sequence - 1UL : 0UL;
            }
        }
        amg_buffer_free(&mapping);
    }
    if (result != AMG_OK || !last) return result;
    first = (limit >= (size_t)last) ? 1UL : last - (unsigned long)limit + 1UL;
    snprintf(command, sizeof(command), "FETCH %lu:%lu %s",
             first, last, imap_message_list_fetch_items);
    return imap_command(session, command, response, error);
}

static int parse_uid_search_result(const unsigned char *data, size_t length,
                                   unsigned long **uids, size_t *count)
{
    size_t position = 0, found = 0, capacity = 0;
    unsigned long *values = NULL;
    if ((!data && length) || !uids || !count) return AMG_ERR_ARGUMENT;
    while (position < length) {
        size_t line_start = position, line_end;
        const unsigned char *cursor, *end;
        while (position < length && data[position] != '\n') ++position;
        line_end = position;
        if (position < length) ++position;
        if (line_end > line_start && data[line_end - 1U] == '\r') --line_end;
        if (line_end - line_start < 8U ||
            memcmp(data + line_start, "* SEARCH", 8U) != 0)
            continue;
        cursor = data + line_start + 8U;
        end = data + line_end;
        while (cursor < end) {
            unsigned long value = 0UL;
            while (cursor < end && isspace((unsigned char)*cursor)) ++cursor;
            if (cursor >= end) break;
            if (!isdigit((unsigned char)*cursor)) {
                while (cursor < end && !isspace((unsigned char)*cursor))
                    ++cursor;
                continue;
            }
            while (cursor < end && isdigit((unsigned char)*cursor)) {
                unsigned long digit = (unsigned long)(*cursor - '0');
                if (value > (0xffffffffUL - digit) / 10UL) {
                    free(values);
                    return AMG_ERR_LIMIT;
                }
                value = value * 10UL + digit;
                ++cursor;
            }
            if (!value) continue;
            if (found == capacity) {
                size_t next_capacity = capacity ? capacity * 2U : 128U;
                unsigned long *next;
                if (next_capacity < capacity ||
                    next_capacity > SIZE_MAX / sizeof(*values)) {
                    free(values);
                    return AMG_ERR_LIMIT;
                }
                next = (unsigned long *)realloc(
                    values, next_capacity * sizeof(*values));
                if (!next) {
                    free(values);
                    return AMG_ERR_MEMORY;
                }
                values = next;
                capacity = next_capacity;
            }
            values[found++] = value;
        }
    }
    *uids = values;
    *count = found;
    return AMG_OK;
}

static int append_uid_fetch_batch(AmgImapSession *session,
                                  const unsigned long *uids,
                                  size_t count, AmgBuffer *response,
                                  AmgError *error)
{
    AmgBuffer command, batch_response;
    size_t i;
    int result = AMG_OK;
    amg_buffer_init(&command);
    amg_buffer_init(&batch_response);
    result = amg_buffer_append_cstr(&command, "UID FETCH ");
    for (i = 0; result == AMG_OK && i < count; ++i) {
        char value[32];
        int written = snprintf(value, sizeof(value), "%s%lu",
                               i ? "," : "", uids[i]);
        if (written <= 0 || (size_t)written >= sizeof(value))
            result = AMG_ERR_LIMIT;
        else
            result = amg_buffer_append(&command, value, (size_t)written);
    }
    if (result == AMG_OK)
        result = amg_buffer_append_char(&command, ' ');
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(&command,
                                        imap_message_list_fetch_items);
    if (result == AMG_OK)
        result = amg_buffer_terminate(&command);
    if (result == AMG_OK)
        result = imap_command(session, (const char *)command.data,
                              &batch_response, error);
    if (result == AMG_OK && batch_response.length)
        result = amg_buffer_append(response, batch_response.data,
                                   batch_response.length);
    if (result != AMG_OK && (!error || error->code == AMG_OK))
        amg_error_set(error, result,
                      T("Nachrichtenliste konnte nicht geladen werden.", "Message list could not be loaded."));
    amg_buffer_free(&command);
    amg_buffer_free(&batch_response);
    return result;
}

#if AMIGMAIL_AMIGA
static int imap_is_leap_year(unsigned long year)
{
    return (year % 4UL == 0UL &&
            (year % 100UL != 0UL || year % 400UL == 0UL));
}

static unsigned long imap_days_in_year(unsigned long year)
{
    return imap_is_leap_year(year) ? 366UL : 365UL;
}

static int imap_date_from_days_since_1978(unsigned long days_since_1978,
                                          unsigned long *day,
                                          unsigned long *month,
                                          unsigned long *year)
{
    static const unsigned char month_lengths[] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };
    unsigned long current_year = 1978UL;
    unsigned long current_month;

    if (!day || !month || !year) return AMG_ERR_ARGUMENT;

    while (days_since_1978 >= imap_days_in_year(current_year)) {
        days_since_1978 -= imap_days_in_year(current_year);
        ++current_year;
    }

    for (current_month = 0UL; current_month < 12UL; ++current_month) {
        unsigned long length = month_lengths[current_month];
        if (current_month == 1UL && imap_is_leap_year(current_year))
            ++length;
        if (days_since_1978 < length) break;
        days_since_1978 -= length;
    }
    if (current_month >= 12UL) return AMG_ERR_IO;

    *day = days_since_1978 + 1UL;
    *month = current_month;
    *year = current_year;
    return AMG_OK;
}

#endif

static int imap_recent_since_date(unsigned int days,
                                  unsigned long *day,
                                  unsigned long *month,
                                  unsigned long *year)
{
#if AMIGMAIL_AMIGA
    struct DateStamp stamp;
    unsigned long target_days;

    DateStamp(&stamp);
    if (stamp.ds_Days < 0) return AMG_ERR_IO;
    if ((unsigned long)(days - 1U) > (unsigned long)stamp.ds_Days)
        return AMG_ERR_IO;

    target_days = (unsigned long)stamp.ds_Days - (unsigned long)(days - 1U);
    return imap_date_from_days_since_1978(target_days, day, month, year);
#else
    time_t now, since_time;
    struct tm *since_tm;

    now = time(NULL);
    if (now == (time_t)-1) return AMG_ERR_IO;
    since_time = now - (time_t)(days - 1U) * (time_t)86400;
    since_tm = localtime(&since_time);
    if (!since_tm || since_tm->tm_mon < 0 || since_tm->tm_mon > 11)
        return AMG_ERR_IO;

    *day = (unsigned long)since_tm->tm_mday;
    *month = (unsigned long)since_tm->tm_mon;
    *year = (unsigned long)(since_tm->tm_year + 1900);
    return AMG_OK;
#endif
}

int amg_imap_fetch_recent(AmgImapSession *session, unsigned int days,
                          AmgBuffer *response, AmgError *error)
{
    static const char *months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    enum { UID_BATCH = 100 };
    AmgBuffer search_response;
    unsigned long *uids = NULL;
    size_t uid_count = 0, offset;
    unsigned long since_day, since_month, since_year;
    char command[96];
    int result;

    if (!session || !response || days < 1U || days > 3650U)
        return AMG_ERR_ARGUMENT;
    if (!session->selected_exists) {
        amg_error_set(error, AMG_OK, "");
        return AMG_OK;
    }

    result = imap_recent_since_date(days, &since_day, &since_month,
                                    &since_year);
    if (result != AMG_OK || since_month > 11UL) {
        amg_error_set(error, AMG_ERR_IO,
                      T("Abrufdatum konnte nicht berechnet werden.", "Fetch date could not be calculated."));
        return AMG_ERR_IO;
    }

    snprintf(command, sizeof(command), "UID SEARCH SINCE %02lu-%s-%04lu",
             since_day, months[since_month], since_year);
    amg_buffer_init(&search_response);
    result = imap_command(session, command, &search_response, error);
    if (result == AMG_OK)
        result = parse_uid_search_result(search_response.data,
                                         search_response.length,
                                         &uids, &uid_count);
    if (result != AMG_OK && (!error || error->code == AMG_OK))
        amg_error_set(error, result,
                      T("Nachrichten im Abruf-Zeitraum konnten nicht ermittelt werden.", "Messages in the fetch period could not be determined."));
    amg_buffer_free(&search_response);
    if (result != AMG_OK) {
        free(uids);
        return result;
    }

    for (offset = 0U; offset < uid_count; offset += UID_BATCH) {
        size_t batch = uid_count - offset;
        if (batch > UID_BATCH) batch = UID_BATCH;
        result = append_uid_fetch_batch(session, uids + offset, batch,
                                        response, error);
        if (result != AMG_OK) break;
    }
    free(uids);
    if (result == AMG_OK) amg_error_set(error, AMG_OK, "");
    return result;
}

int amg_imap_fetch_after_uid(AmgImapSession *session, unsigned long uid,
                             AmgBuffer *response, AmgError *error)
{
    enum { UID_BATCH = 100 };
    AmgBuffer search_response;
    unsigned long *uids = NULL;
    size_t uid_count = 0U, offset;
    char command[64];
    int result;

    if (!session || !response || uid == 0UL) return AMG_ERR_ARGUMENT;
    if (!session->selected_exists || uid == ~0UL) {
        amg_error_set(error, AMG_OK, "");
        return AMG_OK;
    }

    snprintf(command, sizeof(command), "UID SEARCH UID %lu:*", uid + 1UL);
    amg_buffer_init(&search_response);
    result = imap_command(session, command, &search_response, error);
    if (result == AMG_OK)
        result = parse_uid_search_result(search_response.data,
                                         search_response.length,
                                         &uids, &uid_count);
    if (result != AMG_OK && (!error || error->code == AMG_OK))
        amg_error_set(error, result,
                      T("Neue Nachrichten konnten nicht ermittelt werden.", "New messages could not be determined."));
    amg_buffer_free(&search_response);
    if (result != AMG_OK) {
        free(uids);
        return result;
    }

    /* In an IMAP sequence-set, a range whose first value is greater than
     * '*' can be interpreted as a reversed range.  Therefore SEARCH
     * UID <last+1>:* may legally include the previous highest UID when no
     * newer message exists.  Filter the search result explicitly so the
     * five-minute check never refetches an old message. */
    {
        size_t read_index, write_index = 0U;
        for (read_index = 0U; read_index < uid_count; ++read_index) {
            if (uids[read_index] > uid)
                uids[write_index++] = uids[read_index];
        }
        uid_count = write_index;
    }

    for (offset = 0U; offset < uid_count; offset += UID_BATCH) {
        size_t batch = uid_count - offset;
        if (batch > UID_BATCH) batch = UID_BATCH;
        result = append_uid_fetch_batch(session, uids + offset, batch,
                                        response, error);
        if (result != AMG_OK) break;
    }
    free(uids);
    if (result == AMG_OK) amg_error_set(error, AMG_OK, "");
    return result;
}

int amg_imap_fetch_message(AmgImapSession *session, unsigned long uid, AmgBuffer *message, AmgError *error)
{
    char command[112];if(!uid)return AMG_ERR_ARGUMENT;snprintf(command,sizeof(command),"UID FETCH %lu (UID FLAGS BODY.PEEK[])",uid);return imap_command(session,command,message,error);
}

int amg_imap_set_seen(AmgImapSession *session, unsigned long uid, int seen, AmgError *error)
{
    char command[128];snprintf(command,sizeof(command),"UID STORE %lu %sFLAGS.SILENT (\\Seen)",uid,seen?"+":"-");return imap_command(session,command,NULL,error);
}

int amg_imap_set_flagged(AmgImapSession *session, unsigned long uid,
                         int flagged, AmgError *error)
{
    char command[128];
    if (!session || !uid) return AMG_ERR_ARGUMENT;
    snprintf(command, sizeof(command),
             "UID STORE %lu %sFLAGS.SILENT (\\Flagged)",
             uid, flagged ? "+" : "-");
    return imap_command(session, command, NULL, error);
}

static int move_with_uid_move(AmgImapSession *session, unsigned long uid,
                              const char *destination_mailbox,
                              AmgError *error)
{
    AmgBuffer command;
    char prefix[64];
    int result;
    amg_buffer_init(&command);
    snprintf(prefix, sizeof(prefix), "UID MOVE %lu ", uid);
    result = amg_buffer_append_cstr(&command, prefix);
    if (result == AMG_OK)
        result = quote_mailbox(destination_mailbox, &command);
    if (result == AMG_OK)
        result = amg_buffer_terminate(&command);
    if (result == AMG_OK)
        result = imap_command(session, (const char *)command.data, NULL,
                              error);
    amg_buffer_free(&command);
    return result;
}

static int move_with_copy_delete(AmgImapSession *session, unsigned long uid,
                                 const char *destination_mailbox,
                                 AmgError *error)
{
    AmgBuffer command;
    char prefix[64];
    char flag_command[128];
    int result;

    amg_buffer_init(&command);
    snprintf(prefix, sizeof(prefix), "UID COPY %lu ", uid);
    result = amg_buffer_append_cstr(&command, prefix);
    if (result == AMG_OK)
        result = quote_mailbox(destination_mailbox, &command);
    if (result == AMG_OK)
        result = amg_buffer_terminate(&command);
    if (result == AMG_OK)
        result = imap_command(session, (const char *)command.data, NULL,
                              error);
    amg_buffer_free(&command);
    if (result != AMG_OK) return result;

    snprintf(flag_command, sizeof(flag_command),
             "UID STORE %lu +FLAGS.SILENT (\\Deleted)", uid);
    result = imap_command(session, flag_command, NULL, error);
    if (result != AMG_OK) return result;

    if (session->capability_uidplus) {
        snprintf(flag_command, sizeof(flag_command),
                 "UID EXPUNGE %lu", uid);
        result = imap_command(session, flag_command, NULL, error);
    } else {
        result = imap_command(session, "EXPUNGE", NULL, error);
    }
    return result;
}

int amg_imap_move_label(AmgImapSession *session, unsigned long uid,
                        const char *source_label, const char *destination_label,
                        AmgError *error)
{
    const char *source_mailbox;
    const char *destination_mailbox;
    int result;
    if (!session || !uid || !destination_label || !*destination_label)
        return AMG_ERR_ARGUMENT;

    source_mailbox = source_label && *source_label
        ? resolve_special_mailbox(session, source_label)
        : session->selected_mailbox;
    destination_mailbox =
        resolve_special_mailbox(session, destination_label);
    if (!source_mailbox || !*source_mailbox ||
        !destination_mailbox || !*destination_mailbox)
        return AMG_ERR_ARGUMENT;
    if (ascii_ci_equal(source_mailbox, destination_mailbox))
        return AMG_ERR_ARGUMENT;

    if (!ascii_ci_equal(session->selected_mailbox, source_mailbox)) {
        result = amg_imap_select(session, source_mailbox, error);
        if (result != AMG_OK) return result;
    }

    if (session->capability_move)
        result = move_with_uid_move(session, uid, destination_mailbox, error);
    else
        result = move_with_copy_delete(session, uid, destination_mailbox,
                                       error);
    if (result == AMG_OK && session->selected_exists)
        --session->selected_exists;
    return result;
}

int amg_imap_move_to_trash(AmgImapSession *session, unsigned long uid,
                           const char *trash_label, AmgError *error)
{
    if (!session || !uid || !trash_label) return AMG_ERR_ARGUMENT;
    return amg_imap_move_label(session, uid, session->selected_mailbox, trash_label, error);
}

static int imap_wait_append_continuation(AmgImapSession *session,
                                         const char *tag, AmgError *error)
{
    char line[1024];
    size_t used = 0;
    if (!session || !session->connection || !tag) return AMG_ERR_ARGUMENT;

    for (;;) {
        long count;
        if (used + 1U >= sizeof(line)) {
            amg_error_set(error, AMG_ERR_PROTOCOL,
                          T("IMAP-APPEND-Antwort ist zu lang.",
                            "IMAP APPEND response is too long."));
            return AMG_ERR_PROTOCOL;
        }
        count = amg_tls_read(session->connection, line + used, 1U, error);
        if (count <= 0) return AMG_ERR_IO;
        if (line[used++] != '\n') continue;
        line[used] = 0;

        if (line[0] == '+') return AMG_OK;
        if (!strncmp(line, tag, strlen(tag)) &&
            line[strlen(tag)] == ' ') {
            char message[256];
            size_t length = used;
            while (length && (line[length - 1U] == '\r' ||
                              line[length - 1U] == '\n'))
                --length;
            line[length] = 0;
            amg_tr_snprintf(message, sizeof(message),
                            "IMAP-Server: %s", "IMAP server: %s", line);
            amg_error_set(error, AMG_ERR_PROTOCOL, message);
            return AMG_ERR_PROTOCOL;
        }
        used = 0;
    }
}

int amg_imap_append_draft(AmgImapSession *session, const char *mailbox_utf8,
                          const unsigned char *message, size_t length,
                          AmgError *error)
{
    const char *mailbox;
    AmgBuffer wire;
    char tag[16], literal[64];
    int result;

    if (!session || !session->connection || !mailbox_utf8 ||
        !*mailbox_utf8 || (!message && length))
        return AMG_ERR_ARGUMENT;

    mailbox = resolve_special_mailbox(session, mailbox_utf8);
    if (ascii_ci_equal(mailbox_utf8, "\\Drafts") &&
        ascii_ci_equal(mailbox, "\\Drafts")) {
        AmgImapLabel *labels = (AmgImapLabel *)calloc(
            AMIGMAIL_MAX_LABELS, sizeof(*labels));
        size_t count = 0;
        if (!labels) {
            amg_error_set(error, AMG_ERR_MEMORY,
                          T("Nicht genug Speicher.", "Not enough memory."));
            return AMG_ERR_MEMORY;
        }
        result = amg_imap_list_labels(session, labels, AMIGMAIL_MAX_LABELS,
                                      &count, error);
        free(labels);
        if (result != AMG_OK) return result;
        mailbox = resolve_special_mailbox(session, mailbox_utf8);
    }
    if (!mailbox || !*mailbox || ascii_ci_equal(mailbox, "\\Drafts")) {
        amg_error_set(error, AMG_ERR_PROTOCOL,
                      T("Der Gmail-Entwurfsordner wurde nicht gefunden.",
                        "The Gmail Drafts folder was not found."));
        return AMG_ERR_PROTOCOL;
    }

    snprintf(tag, sizeof(tag), "A%06lu", session->tag_counter++);
    amg_buffer_init(&wire);
    result = amg_buffer_append_cstr(&wire, tag);
    if (result == AMG_OK) result = amg_buffer_append_cstr(&wire, " APPEND ");
    if (result == AMG_OK) result = quote_mailbox(mailbox, &wire);
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(&wire, " (\\Draft) {");
    if (result == AMG_OK) {
        snprintf(literal, sizeof(literal), "%lu}", (unsigned long)length);
        result = amg_buffer_append_cstr(&wire, literal);
    }
    if (result == AMG_OK) result = amg_buffer_append_cstr(&wire, "\r\n");
    if (result != AMG_OK) {
        amg_buffer_free(&wire);
        amg_error_set(error, result,
                      T("IMAP-APPEND-Befehl konnte nicht erstellt werden.",
                        "IMAP APPEND command could not be created."));
        return result;
    }

    result = amg_tls_write_all(session->connection, wire.data, wire.length,
                               error);
    amg_buffer_free(&wire);
    if (result != AMG_OK) return result;

    result = imap_wait_append_continuation(session, tag, error);
    if (result != AMG_OK) return result;
    if (length) {
        result = amg_tls_write_all(session->connection, message, length, error);
        if (result != AMG_OK) return result;
    }
    result = amg_tls_write_all(session->connection, "\r\n", 2U, error);
    if (result != AMG_OK) return result;
    return imap_collect(session, tag, NULL, error);
}

int amg_imap_empty_mailbox(AmgImapSession *session, const char *mailbox_utf8,
                           AmgError *error)
{
    const char *mailbox;
    int result;
    if (!session || !mailbox_utf8 || !*mailbox_utf8)
        return AMG_ERR_ARGUMENT;
    mailbox = resolve_special_mailbox(session, mailbox_utf8);
    if (!mailbox || !*mailbox) return AMG_ERR_ARGUMENT;
    result = amg_imap_select(session, mailbox, error);
    if (result != AMG_OK) return result;
    if (!session->selected_exists) return AMG_OK;
    result = imap_command(
        session, "STORE 1:* +FLAGS.SILENT (\\Deleted)", NULL, error);
    if (result == AMG_OK)
        result = imap_command(session, "EXPUNGE", NULL, error);
    if (result == AMG_OK) session->selected_exists = 0;
    return result;
}
