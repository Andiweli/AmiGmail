#include "smtp.h"
#include "codec.h"
#include "tls.h"
#include "i18n.h"

#define T(id, en) amg_tr((id), (en))

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int header_safe(const char *value)
{
    return value && !strchr(value, '\r') && !strchr(value, '\n');
}

static unsigned long boundary_hash_text(unsigned long hash, const char *text)
{
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    while (*p) {
        hash ^= (unsigned long)*p++;
        hash *= 16777619UL;
    }
    return hash;
}

static void make_mime_boundary(const AmgMailDraft *draft, char boundary[96])
{
    static unsigned long sequence = 0UL;
    unsigned long hash = 2166136261UL;
    hash = boundary_hash_text(hash, draft->from);
    hash = boundary_hash_text(hash, draft->to);
    hash = boundary_hash_text(hash, draft->cc);
    hash = boundary_hash_text(hash, draft->bcc);
    hash = boundary_hash_text(hash, draft->subject);
    hash = boundary_hash_text(hash, draft->message_id);
    ++sequence;
    snprintf(boundary, 96U, "=_AmiGmail_%08lx_%08lx_%lu",
             hash, sequence, (unsigned long)draft->attachment_count);
}

int amg_smtp_dot_stuff(const char *message, size_t length, AmgBuffer *output)
{
    size_t i;
    int line_start = 1;
    if ((!message && length) || !output) return AMG_ERR_ARGUMENT;
    for (i = 0; i < length; ++i) {
        if (line_start && message[i] == '.' &&
            amg_buffer_append_char(output, '.') != AMG_OK)
            return AMG_ERR_MEMORY;
        if (amg_buffer_append_char(output, (unsigned char)message[i]) != AMG_OK)
            return AMG_ERR_MEMORY;
        line_start = message[i] == '\n';
    }
    return AMG_OK;
}

int amg_smtp_reply_subject(const char *subject, AmgBuffer *output)
{
    const char *p = subject ? subject : "";
    while (*p && isspace((unsigned char)*p)) ++p;
    if (tolower((unsigned char)p[0]) == 'r' &&
        tolower((unsigned char)p[1]) == 'e' && p[2] == ':')
        return amg_buffer_append_cstr(output, p);
    if (amg_buffer_append_cstr(output, "Re: ") != AMG_OK)
        return AMG_ERR_MEMORY;
    return amg_buffer_append_cstr(output, p);
}

static int append_header(AmgBuffer *output, const char *name, const char *value)
{
    if (!header_safe(value)) return AMG_ERR_ARGUMENT;
    if (amg_buffer_append_cstr(output, name) != AMG_OK ||
        amg_buffer_append_cstr(output, ": ") != AMG_OK ||
        amg_buffer_append_cstr(output, value) != AMG_OK ||
        amg_buffer_append_cstr(output, "\r\n") != AMG_OK)
        return AMG_ERR_MEMORY;
    return AMG_OK;
}

static int append_crlf_body(const char *body, AmgBuffer *output)
{
    const unsigned char *p = (const unsigned char *)(body ? body : "");
    while (*p) {
        if (*p == '\r') {
            if (p[1] == '\n') ++p;
            if (amg_buffer_append_cstr(output, "\r\n") != AMG_OK)
                return AMG_ERR_MEMORY;
        } else if (*p == '\n') {
            if (amg_buffer_append_cstr(output, "\r\n") != AMG_OK)
                return AMG_ERR_MEMORY;
        } else if (amg_buffer_append_char(output, *p) != AMG_OK) {
            return AMG_ERR_MEMORY;
        }
        ++p;
    }
    return AMG_OK;
}

int amg_smtp_build_reply(const AmgReplyDraft *draft, AmgBuffer *output,
                         AmgError *error)
{
    AmgBuffer subject, raw, stuffed;
    int result = AMG_OK;
    if (!draft || !output || !header_safe(draft->from) ||
        !header_safe(draft->to) || !header_safe(draft->subject) ||
        !header_safe(draft->date_rfc2822) || !header_safe(draft->message_id)) {
        amg_error_set(error, AMG_ERR_ARGUMENT,
                      T(MSG_INVALID_MAIL_ADDRESS_OR_HEADER_LINE, "Invalid mail address or header line."));
        return AMG_ERR_ARGUMENT;
    }
    amg_buffer_init(&subject);
    amg_buffer_init(&raw);
    amg_buffer_init(&stuffed);
    result = amg_smtp_reply_subject(draft->subject, &subject);
    if (result == AMG_OK) result = amg_buffer_terminate(&subject);
    if (result == AMG_OK) result = append_header(&raw, "From", draft->from);
    if (result == AMG_OK) result = append_header(&raw, "To", draft->to);
    if (result == AMG_OK)
        result = append_header(&raw, "Subject", (const char *)subject.data);
    if (result == AMG_OK)
        result = append_header(&raw, "Date", draft->date_rfc2822);
    if (result == AMG_OK)
        result = append_header(&raw, "Message-ID", draft->message_id);
    if (result == AMG_OK && draft->in_reply_to && *draft->in_reply_to)
        result = append_header(&raw, "In-Reply-To", draft->in_reply_to);
    if (result == AMG_OK && draft->references && *draft->references)
        result = append_header(&raw, "References", draft->references);
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(
            &raw,
            "MIME-Version: 1.0\r\n"
            "Content-Type: text/plain; charset=UTF-8\r\n"
            "Content-Transfer-Encoding: 8bit\r\n\r\n");
    if (result == AMG_OK)
        result = append_crlf_body(draft->body_utf8, &raw);
    if (result == AMG_OK &&
        (raw.length < 2U || raw.data[raw.length - 2U] != '\r' ||
         raw.data[raw.length - 1U] != '\n'))
        result = amg_buffer_append_cstr(&raw, "\r\n");
    if (result == AMG_OK)
        result = amg_smtp_dot_stuff((const char *)raw.data, raw.length, &stuffed);
    if (result == AMG_OK)
        result = amg_buffer_append(output, stuffed.data, stuffed.length);
    amg_buffer_free(&subject);
    amg_buffer_free(&raw);
    amg_buffer_free(&stuffed);
    amg_error_set(error, result,
                  result == AMG_OK
                      ? ""
                      : T(MSG_REPLY_MESSAGE_COULD_NOT_BE_CREATED, "Reply message could not be created."));
    return result;
}

static int smtp_response(AmgTlsConnection *connection, int expected_class,
                         AmgError *error)
{
    char line[1024];
    size_t used;
    int code, continued;
    do {
        used = 0;
        while (used + 1U < sizeof(line)) {
            long count = amg_tls_read(connection, line + used, 1U, error);
            if (count <= 0) return AMG_ERR_IO;
            if (line[used++] == '\n') break;
        }
        line[used] = 0;
        if (used < 4U || !isdigit((unsigned char)line[0]) ||
            !isdigit((unsigned char)line[1]) ||
            !isdigit((unsigned char)line[2])) {
            amg_error_set(error, AMG_ERR_PROTOCOL,
                          T(MSG_INVALID_SMTP_RESPONSE, "Invalid SMTP response."));
            return AMG_ERR_PROTOCOL;
        }
        code = (line[0] - '0') * 100 + (line[1] - '0') * 10 + line[2] - '0';
        continued = line[3] == '-';
    } while (continued);
    if (code / 100 != expected_class) {
        amg_error_set(error, AMG_ERR_PROTOCOL, line);
        return AMG_ERR_PROTOCOL;
    }
    return AMG_OK;
}

static int smtp_command(AmgTlsConnection *connection, const char *command,
                        int expected_class, AmgError *error)
{
    int result = amg_tls_write_all(connection, command, strlen(command), error);
    return result == AMG_OK
               ? smtp_response(connection, expected_class, error)
               : result;
}

static int smtp_authenticate(AmgTlsConnection *connection,
                             const AmgAccount *account,
                             const char *access_token, AmgError *error)
{
    AmgBuffer auth, encoded, command;
    int result;
    amg_buffer_init(&auth);
    amg_buffer_init(&encoded);
    amg_buffer_init(&command);
    result = smtp_response(connection, 2, error);
    if (result == AMG_OK)
        result = smtp_command(connection, "EHLO amigmail.local\r\n", 2, error);
    if (result == AMG_OK) {
        if (account->auth_mode == AMG_AUTH_OAUTH2) {
            if (!access_token || !*access_token) {
                amg_error_set(error, AMG_ERR_AUTH,
                              T(MSG_OAUTH_ACCESS_TOKEN_IS_MISSING, "OAuth access token is missing."));
                result = AMG_ERR_AUTH;
            } else {
                amg_buffer_append_cstr(&auth, "user=");
                amg_buffer_append_cstr(&auth, account->email);
                amg_buffer_append_char(&auth, 1);
                amg_buffer_append_cstr(&auth, "auth=Bearer ");
                amg_buffer_append_cstr(&auth, access_token);
                amg_buffer_append_char(&auth, 1);
                amg_buffer_append_char(&auth, 1);
            }
            if (result == AMG_OK)
                result = amg_base64_encode(auth.data, auth.length, &encoded);
            if (result == AMG_OK) {
                amg_buffer_append_cstr(&command, "AUTH XOAUTH2 ");
                amg_buffer_append(&command, encoded.data, encoded.length);
                amg_buffer_append_cstr(&command, "\r\n");
            }
        } else {
            amg_buffer_append_char(&auth, 0);
            amg_buffer_append_cstr(&auth, account->email);
            amg_buffer_append_char(&auth, 0);
            amg_buffer_append_cstr(&auth, account->app_password);
            result = amg_base64_encode(auth.data, auth.length, &encoded);
            if (result == AMG_OK) {
                amg_buffer_append_cstr(&command, "AUTH PLAIN ");
                amg_buffer_append(&command, encoded.data, encoded.length);
                amg_buffer_append_cstr(&command, "\r\n");
            }
        }
        if (result == AMG_OK)
            result = smtp_command(connection, (const char *)command.data, 2,
                                  error);
    }
    amg_secure_clear(auth.data, auth.capacity);
    amg_secure_clear(encoded.data, encoded.capacity);
    amg_buffer_free(&auth);
    amg_buffer_free(&encoded);
    amg_buffer_free(&command);
    return result;
}

static AmgTlsConnection *smtp_open(const AmgAccount *account,
                                   const char *access_token, AmgError *error)
{
    AmgTlsConnection *connection;
    int result;
    if (account->smtp_starttls || account->smtp_port != 465U) {
        amg_error_set(
            error, AMG_ERR_UNSUPPORTED,
            T(MSG_AMIGMAIL_CURRENTLY_SUPPORTS_SMTP_OVER_DIRECT_TLS_ON, "AmiGmail currently supports SMTP over direct TLS on port 465."));
        return NULL;
    }
    connection = amg_tls_connect(account->smtp_host, account->smtp_port, 30U,
                                 error);
    if (!connection) return NULL;
    result = smtp_authenticate(connection, account, access_token, error);
    if (result != AMG_OK) {
        amg_tls_close(connection);
        return NULL;
    }
    return connection;
}

static int smtp_mail_from(AmgTlsConnection *connection, const char *address,
                          AmgError *error)
{
    char command[640];
    int length;
    if (!header_safe(address) || !strchr(address, '@')) return AMG_ERR_ARGUMENT;
    length = snprintf(command, sizeof(command), "MAIL FROM:<%s>\r\n", address);
    if (length < 0 || (size_t)length >= sizeof(command)) return AMG_ERR_LIMIT;
    return smtp_command(connection, command, 2, error);
}

static int smtp_recipient(AmgTlsConnection *connection, const char *start,
                          size_t length, AmgError *error)
{
    char address[512], command[640];
    const char *left, *right;
    int count;
    while (length && isspace((unsigned char)*start)) {
        ++start;
        --length;
    }
    while (length && isspace((unsigned char)start[length - 1U])) --length;
    if (!length) return 0;
    left = memchr(start, '<', length);
    right = left ? memchr(left + 1, '>', length - (size_t)(left + 1 - start))
                 : NULL;
    if (left && right) {
        start = left + 1;
        length = (size_t)(right - start);
    }
    while (length && isspace((unsigned char)*start)) {
        ++start;
        --length;
    }
    while (length && isspace((unsigned char)start[length - 1U])) --length;
    if (!length || length >= sizeof(address)) return AMG_ERR_ARGUMENT;
    memcpy(address, start, length);
    address[length] = 0;
    if (!header_safe(address) || !strchr(address, '@') || strchr(address, ' '))
        return AMG_ERR_ARGUMENT;
    count = snprintf(command, sizeof(command), "RCPT TO:<%s>\r\n", address);
    if (count < 0 || (size_t)count >= sizeof(command)) return AMG_ERR_LIMIT;
    if (smtp_command(connection, command, 2, error) != AMG_OK)
        return error ? error->code : AMG_ERR_PROTOCOL;
    return 1;
}

static int smtp_recipient_list(AmgTlsConnection *connection, const char *list,
                               unsigned *recipient_count, AmgError *error)
{
    const char *p, *start;
    if (!list || !*list) return AMG_OK;
    if (!header_safe(list)) return AMG_ERR_ARGUMENT;
    p = start = list;
    for (;;) {
        if (*p == ',' || *p == ';' || *p == 0) {
            int result = smtp_recipient(connection, start,
                                        (size_t)(p - start), error);
            if (result < 0) return result;
            if (result > 0) ++*recipient_count;
            if (!*p) break;
            start = p + 1;
        }
        ++p;
    }
    return AMG_OK;
}

static int smtp_begin_data(AmgTlsConnection *connection, const char *from,
                           const char *to, const char *cc, const char *bcc,
                           AmgError *error)
{
    unsigned recipients = 0;
    int result = smtp_mail_from(connection, from, error);
    if (result == AMG_OK)
        result = smtp_recipient_list(connection, to, &recipients, error);
    if (result == AMG_OK)
        result = smtp_recipient_list(connection, cc, &recipients, error);
    if (result == AMG_OK)
        result = smtp_recipient_list(connection, bcc, &recipients, error);
    if (result == AMG_OK && !recipients) {
        amg_error_set(error, AMG_ERR_ARGUMENT,
                      T(MSG_AT_LEAST_ONE_RECIPIENT_IS_REQUIRED, "At least one recipient is required."));
        result = AMG_ERR_ARGUMENT;
    }
    if (result == AMG_OK)
        result = smtp_command(connection, "DATA\r\n", 3, error);
    return result;
}

int amg_smtp_send_reply(const AmgAccount *account, const char *access_token,
                        const AmgReplyDraft *draft, AmgError *error)
{
    AmgTlsConnection *connection;
    AmgBuffer message;
    int result;
    if (!account || !draft) return AMG_ERR_ARGUMENT;
    connection = smtp_open(account, access_token, error);
    if (!connection) return error ? error->code : AMG_ERR_TLS;
    amg_buffer_init(&message);
    result = smtp_begin_data(connection, account->email, draft->to, NULL, NULL,
                             error);
    if (result == AMG_OK) result = amg_smtp_build_reply(draft, &message, error);
    if (result == AMG_OK) {
        amg_buffer_append_cstr(&message, ".\r\n");
        result = amg_tls_write_all(connection, message.data, message.length,
                                   error);
    }
    if (result == AMG_OK) result = smtp_response(connection, 2, error);
    if (result == AMG_OK)
        (void)smtp_command(connection, "QUIT\r\n", 2, error);
    amg_buffer_free(&message);
    amg_tls_close(connection);
    return result;
}

static int attachment_file_size(const char *path, unsigned long *size)
{
    FILE *file;
    long length;
    file = fopen(path, "rb");
    if (!file) return AMG_ERR_IO;
    if (fseek(file, 0L, SEEK_END) != 0 || (length = ftell(file)) < 0) {
        fclose(file);
        return AMG_ERR_IO;
    }
    fclose(file);
    *size = (unsigned long)length;
    return AMG_OK;
}

static int validate_attachments(const AmgMailDraft *draft, AmgError *error)
{
    size_t i;
    unsigned long total = 0;
    if (draft->attachment_count > AMG_MAIL_MAX_ATTACHMENTS) {
        amg_error_set(error, AMG_ERR_LIMIT, T(MSG_TOO_MANY_ATTACHMENTS, "Too many attachments."));
        return AMG_ERR_LIMIT;
    }
    for (i = 0; i < draft->attachment_count; ++i) {
        unsigned long size;
        if (!draft->attachments || !draft->attachments[i].path ||
            !draft->attachments[i].name_utf8 ||
            attachment_file_size(draft->attachments[i].path, &size) != AMG_OK) {
            amg_error_set(error, AMG_ERR_IO,
                          T(MSG_AN_ATTACHMENT_COULD_NOT_BE_READ, "An attachment could not be read."));
            return AMG_ERR_IO;
        }
        if (size > AMG_MAIL_MAX_ATTACHMENT_TOTAL - total) {
            amg_error_set(error, AMG_ERR_LIMIT,
                          T(MSG_ATTACHMENTS_MAY_TOTAL_NO_MORE_THAN_10_MB_UTF8, "Attachments may total no more than 10 MB."));
            return AMG_ERR_LIMIT;
        }
        total += size;
    }
    return AMG_OK;
}

static void safe_filename(const char *source, char *destination,
                          size_t capacity)
{
    size_t used = 0;
    if (!source) source = "attachment.bin";
    while (*source && used + 1U < capacity) {
        unsigned char c = (unsigned char)*source++;
        if (c < 32U || c == 127U || c == '"' || c == '\\') c = '_';
        destination[used++] = (char)c;
    }
    destination[used] = 0;
    if (!used && capacity > 1U) strcpy(destination, "attachment.bin");
}

static int append_mail_headers_common(const AmgMailDraft *draft,
                                      const char *boundary, int include_bcc,
                                      AmgBuffer *output)
{
    int result = append_header(output, "From", draft->from);
    if (result == AMG_OK) result = append_header(output, "To", draft->to);
    if (result == AMG_OK && draft->cc && *draft->cc)
        result = append_header(output, "Cc", draft->cc);
    if (result == AMG_OK && include_bcc && draft->bcc && *draft->bcc)
        result = append_header(output, "Bcc", draft->bcc);
    if (result == AMG_OK) result = append_header(output, "Subject", draft->subject);
    if (result == AMG_OK)
        result = append_header(output, "Date", draft->date_rfc2822);
    if (result == AMG_OK)
        result = append_header(output, "Message-ID", draft->message_id);
    if (result == AMG_OK && draft->in_reply_to && *draft->in_reply_to)
        result = append_header(output, "In-Reply-To", draft->in_reply_to);
    if (result == AMG_OK && draft->references && *draft->references)
        result = append_header(output, "References", draft->references);
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(output, "MIME-Version: 1.0\r\n");
    if (result == AMG_OK && boundary) {
        result = amg_buffer_append_cstr(output,
                                        "Content-Type: multipart/mixed; boundary=\"");
        if (result == AMG_OK) result = amg_buffer_append_cstr(output, boundary);
        if (result == AMG_OK)
            result = amg_buffer_append_cstr(output, "\"\r\n\r\n--");
        if (result == AMG_OK) result = amg_buffer_append_cstr(output, boundary);
        if (result == AMG_OK)
            result = amg_buffer_append_cstr(
                output,
                "\r\nContent-Type: text/plain; charset=UTF-8\r\n"
                "Content-Transfer-Encoding: 8bit\r\n\r\n");
    } else if (result == AMG_OK) {
        result = amg_buffer_append_cstr(
            output,
            "Content-Type: text/plain; charset=UTF-8\r\n"
            "Content-Transfer-Encoding: 8bit\r\n\r\n");
    }
    if (result == AMG_OK) result = append_crlf_body(draft->body_utf8, output);
    if (result == AMG_OK &&
        (output->length < 2U || output->data[output->length - 2U] != '\r' ||
         output->data[output->length - 1U] != '\n'))
        result = amg_buffer_append_cstr(output, "\r\n");
    return result;
}

static int append_mail_headers(const AmgMailDraft *draft, const char *boundary,
                               AmgBuffer *output)
{
    return append_mail_headers_common(draft, boundary, 0, output);
}

static int smtp_write_text(AmgTlsConnection *connection, const AmgBuffer *text,
                           AmgError *error)
{
    AmgBuffer stuffed;
    int result;
    amg_buffer_init(&stuffed);
    result = amg_smtp_dot_stuff((const char *)text->data, text->length, &stuffed);
    if (result == AMG_OK)
        result = amg_tls_write_all(connection, stuffed.data, stuffed.length,
                                   error);
    amg_buffer_free(&stuffed);
    return result;
}

static int smtp_write_attachment(AmgTlsConnection *connection,
                                 const AmgAttachmentInput *attachment,
                                 const char *boundary, AmgError *error)
{
    FILE *file;
    unsigned char block[57];
    char filename[256];
    AmgBuffer header, encoded;
    size_t count;
    int result = AMG_OK;
    safe_filename(attachment->name_utf8, filename, sizeof(filename));
    file = fopen(attachment->path, "rb");
    if (!file) {
        amg_error_set(error, AMG_ERR_IO,
                      T(MSG_AN_ATTACHMENT_COULD_NOT_BE_OPENED, "An attachment could not be opened."));
        return AMG_ERR_IO;
    }
    amg_buffer_init(&header);
    amg_buffer_init(&encoded);
    amg_buffer_append_cstr(&header, "--");
    amg_buffer_append_cstr(&header, boundary);
    amg_buffer_append_cstr(
        &header,
        "\r\nContent-Type: application/octet-stream; name=\"");
    amg_buffer_append_cstr(&header, filename);
    amg_buffer_append_cstr(
        &header,
        "\"\r\nContent-Transfer-Encoding: base64\r\n"
        "Content-Disposition: attachment; filename=\"");
    amg_buffer_append_cstr(&header, filename);
    amg_buffer_append_cstr(&header, "\"\r\n\r\n");
    result = amg_tls_write_all(connection, header.data, header.length, error);
    while (result == AMG_OK && (count = fread(block, 1U, sizeof(block), file)) > 0) {
        encoded.length = 0;
        result = amg_base64_encode(block, count, &encoded);
        if (result == AMG_OK) result = amg_buffer_append_cstr(&encoded, "\r\n");
        if (result == AMG_OK)
            result = amg_tls_write_all(connection, encoded.data, encoded.length,
                                       error);
    }
    if (result == AMG_OK && ferror(file)) result = AMG_ERR_IO;
    fclose(file);
    amg_buffer_free(&header);
    amg_buffer_free(&encoded);
    if (result != AMG_OK && error && error->code == AMG_OK)
        amg_error_set(error, result,
                      T(MSG_AN_ATTACHMENT_COULD_NOT_BE_SENT, "An attachment could not be sent."));
    return result;
}

static int append_attachment_to_buffer(const AmgAttachmentInput *attachment,
                                       const char *boundary,
                                       AmgBuffer *output, AmgError *error)
{
    FILE *file;
    unsigned char block[57];
    char filename[256];
    AmgBuffer encoded;
    size_t count;
    int result = AMG_OK;

    if (!attachment || !boundary || !output) return AMG_ERR_ARGUMENT;
    safe_filename(attachment->name_utf8, filename, sizeof(filename));
    file = fopen(attachment->path, "rb");
    if (!file) {
        amg_error_set(error, AMG_ERR_IO,
                      T(MSG_AN_ATTACHMENT_COULD_NOT_BE_OPENED, "An attachment could not be opened."));
        return AMG_ERR_IO;
    }

    result = amg_buffer_append_cstr(output, "--");
    if (result == AMG_OK) result = amg_buffer_append_cstr(output, boundary);
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(
            output, "\r\nContent-Type: application/octet-stream; name=\"");
    if (result == AMG_OK) result = amg_buffer_append_cstr(output, filename);
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(
            output,
            "\"\r\nContent-Transfer-Encoding: base64\r\n"
            "Content-Disposition: attachment; filename=\"");
    if (result == AMG_OK) result = amg_buffer_append_cstr(output, filename);
    if (result == AMG_OK)
        result = amg_buffer_append_cstr(output, "\"\r\n\r\n");

    amg_buffer_init(&encoded);
    while (result == AMG_OK &&
           (count = fread(block, 1U, sizeof(block), file)) > 0U) {
        encoded.length = 0;
        result = amg_base64_encode(block, count, &encoded);
        if (result == AMG_OK)
            result = amg_buffer_append_cstr(&encoded, "\r\n");
        if (result == AMG_OK)
            result = amg_buffer_append(output, encoded.data, encoded.length);
    }
    if (result == AMG_OK && ferror(file)) result = AMG_ERR_IO;
    fclose(file);
    amg_buffer_free(&encoded);

    if (result != AMG_OK && error && error->code == AMG_OK)
        amg_error_set(error, result,
                      T(MSG_AN_ATTACHMENT_COULD_NOT_BE_WRITTEN_TO_THE, "An attachment could not be written to the draft."));
    return result;
}

int amg_smtp_build_mail(const AmgMailDraft *draft, int include_bcc,
                        AmgBuffer *output, AmgError *error)
{
    char boundary[96];
    const char *used_boundary = NULL;
    size_t i;
    int result;

    if (!draft || !output || !header_safe(draft->from) ||
        !header_safe(draft->to ? draft->to : "") ||
        !header_safe(draft->cc ? draft->cc : "") ||
        !header_safe(draft->bcc ? draft->bcc : "") ||
        !header_safe(draft->subject ? draft->subject : "") ||
        !header_safe(draft->date_rfc2822) ||
        !header_safe(draft->message_id) ||
        !header_safe(draft->in_reply_to ? draft->in_reply_to : "") ||
        !header_safe(draft->references ? draft->references : "")) {
        amg_error_set(error, AMG_ERR_ARGUMENT,
                      T(MSG_INVALID_MAIL_ADDRESS_OR_HEADER_LINE, "Invalid mail address or header line."));
        return AMG_ERR_ARGUMENT;
    }

    result = validate_attachments(draft, error);
    if (result != AMG_OK) return result;
    if (draft->attachment_count) {
        make_mime_boundary(draft, boundary);
        used_boundary = boundary;
    }

    result = append_mail_headers_common(draft, used_boundary,
                                        include_bcc ? 1 : 0, output);
    for (i = 0; result == AMG_OK && i < draft->attachment_count; ++i)
        result = append_attachment_to_buffer(&draft->attachments[i],
                                             used_boundary, output, error);
    if (result == AMG_OK && used_boundary) {
        result = amg_buffer_append_cstr(output, "--");
        if (result == AMG_OK)
            result = amg_buffer_append_cstr(output, used_boundary);
        if (result == AMG_OK)
            result = amg_buffer_append_cstr(output, "--\r\n");
    }
    if (result != AMG_OK && error && error->code == AMG_OK)
        amg_error_set(error, result,
                      T(MSG_MAIL_DRAFT_COULD_NOT_BE_CREATED, "Mail draft could not be created."));
    return result;
}

int amg_smtp_send_mail(const AmgAccount *account, const char *access_token,
                       const AmgMailDraft *draft, AmgError *error)
{
    AmgTlsConnection *connection;
    AmgBuffer text, ending;
    char boundary[96];
    const char *used_boundary = NULL;
    size_t i;
    int result;
    if (!account || !draft || !header_safe(draft->from) ||
        !header_safe(draft->to) || !header_safe(draft->cc ? draft->cc : "") ||
        !header_safe(draft->bcc ? draft->bcc : "") ||
        !header_safe(draft->subject) || !header_safe(draft->date_rfc2822) ||
        !header_safe(draft->message_id) ||
        !header_safe(draft->in_reply_to ? draft->in_reply_to : "") ||
        !header_safe(draft->references ? draft->references : "")) {
        amg_error_set(error, AMG_ERR_ARGUMENT,
                      T(MSG_INVALID_MAIL_ADDRESS_OR_HEADER_LINE, "Invalid mail address or header line."));
        return AMG_ERR_ARGUMENT;
    }
    result = validate_attachments(draft, error);
    if (result != AMG_OK) return result;
    if (draft->attachment_count) {
        make_mime_boundary(draft, boundary);
        used_boundary = boundary;
    }
    connection = smtp_open(account, access_token, error);
    if (!connection) return error ? error->code : AMG_ERR_TLS;
    result = smtp_begin_data(connection, draft->from, draft->to, draft->cc,
                             draft->bcc, error);
    amg_buffer_init(&text);
    amg_buffer_init(&ending);
    if (result == AMG_OK)
        result = append_mail_headers(draft, used_boundary, &text);
    if (result == AMG_OK) result = smtp_write_text(connection, &text, error);
    for (i = 0; result == AMG_OK && i < draft->attachment_count; ++i)
        result = smtp_write_attachment(connection, &draft->attachments[i],
                                       used_boundary, error);
    if (result == AMG_OK && used_boundary) {
        amg_buffer_append_cstr(&ending, "--");
        amg_buffer_append_cstr(&ending, used_boundary);
        amg_buffer_append_cstr(&ending, "--\r\n");
        result = amg_tls_write_all(connection, ending.data, ending.length,
                                   error);
    }
    if (result == AMG_OK)
        result = amg_tls_write_all(connection, ".\r\n", 3U, error);
    if (result == AMG_OK) result = smtp_response(connection, 2, error);
    if (result == AMG_OK)
        (void)smtp_command(connection, "QUIT\r\n", 2, error);
    amg_buffer_free(&text);
    amg_buffer_free(&ending);
    amg_tls_close(connection);
    return result;
}
