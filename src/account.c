#include "account.h"
#include "i18n.h"

#define T(de, en) amg_tr((de), (en))

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void amg_account_init(AmgAccount *account)
{
    if (!account) return;
    memset(account, 0, sizeof(*account));
    strcpy(account->imap_host, "imap.gmail.com");
    account->imap_port = 993;
    strcpy(account->smtp_host, "smtp.gmail.com");
    account->smtp_port = 465;
    account->smtp_starttls = 0;
    account->fetch_on_start = 0;
    account->periodic_fetch = 0;
    account->fetch_days = 180U;
    account->auth_mode = AMG_AUTH_APP_PASSWORD;
}

void amg_account_clear(AmgAccount *account)
{
    if (!account) return;
    if (account->app_password) {
        amg_secure_clear(account->app_password, strlen(account->app_password));
        free(account->app_password);
    }
    if (account->refresh_token) {
        amg_secure_clear(account->refresh_token, strlen(account->refresh_token));
        free(account->refresh_token);
    }
    amg_secure_clear(account, sizeof(*account));
}

int amg_account_set_secret(char **destination, const char *value)
{
    char *copy;
    if (!destination) return AMG_ERR_ARGUMENT;
    copy = NULL;
    if (value) {
        copy = (char *)malloc(strlen(value) + 1U);
        if (!copy) return AMG_ERR_MEMORY;
        strcpy(copy, value);
    }
    if (*destination) {
        amg_secure_clear(*destination, strlen(*destination));
        free(*destination);
    }
    *destination = copy;
    return AMG_OK;
}

static int valid_email(const char *email)
{
    const char *at;
    if (!email || !*email || strchr(email, ' ') || strchr(email, '\r') || strchr(email, '\n')) return 0;
    at = strchr(email, '@');
    return at && at != email && strchr(at + 1, '.') != NULL;
}

int amg_account_validate(const AmgAccount *account, AmgError *error)
{
    size_t digits = 0;
    const char *p;
    if (!account || !valid_email(account->email)) {
        amg_error_set(error, AMG_ERR_ARGUMENT, T("Bitte eine gültige Gmail-Adresse eingeben.", "Please enter a valid Gmail address."));
        return AMG_ERR_ARGUMENT;
    }
    if (!account->imap_host[0] || !account->smtp_host[0] || !account->imap_port || !account->smtp_port) {
        amg_error_set(error, AMG_ERR_ARGUMENT, T("Servername oder Port fehlt.", "Server name or port is missing."));
        return AMG_ERR_ARGUMENT;
    }
    if (account->fetch_days < 1U || account->fetch_days > 3650U) {
        amg_error_set(error, AMG_ERR_ARGUMENT,
                      T("Der Abruf-Zeitraum muss zwischen 1 und 3650 Tagen liegen.", "The fetch period must be between 1 and 3650 days."));
        return AMG_ERR_ARGUMENT;
    }
    if (account->auth_mode == AMG_AUTH_APP_PASSWORD) {
        if (!account->app_password) {
            amg_error_set(error, AMG_ERR_AUTH, T("Das Gmail-App-Passwort fehlt.", "The Gmail app password is missing."));
            return AMG_ERR_AUTH;
        }
        for (p = account->app_password; *p; ++p) {
            if (!isspace((unsigned char)*p)) ++digits;
        }
        if (digits != 16U) {
            amg_error_set(error, AMG_ERR_AUTH, T("Das Gmail-App-Passwort muss 16 Zeichen enthalten.", "The Gmail app password must contain 16 characters."));
            return AMG_ERR_AUTH;
        }
    } else if (!account->refresh_token) {
        amg_error_set(error, AMG_ERR_AUTH, T("Die Google-Autorisierung wurde noch nicht abgeschlossen.", "Google authorization has not been completed yet."));
        return AMG_ERR_AUTH;
    }
    amg_error_set(error, AMG_OK, "");
    return AMG_OK;
}
