#ifndef AMIGMAIL_ACCOUNT_H
#define AMIGMAIL_ACCOUNT_H

#include "amigmail.h"

typedef struct AmgAccount {
    char display_name[96];
    char email[256];
    AmgAuthMode auth_mode;
    char imap_host[256];
    unsigned short imap_port;
    char smtp_host[256];
    unsigned short smtp_port;
    int smtp_starttls;
    int fetch_on_start;
    int periodic_fetch;
    unsigned int fetch_days;
    char *app_password;
    char *refresh_token;
} AmgAccount;

void amg_account_init(AmgAccount *account);
void amg_account_clear(AmgAccount *account);
int amg_account_set_secret(char **destination, const char *value);
int amg_account_validate(const AmgAccount *account, AmgError *error);

#endif
