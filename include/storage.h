#ifndef AMIGMAIL_STORAGE_H
#define AMIGMAIL_STORAGE_H

#include "account.h"

int amg_storage_load_account(const char *path, const char *master_password,
                             AmgAccount *account, AmgError *error);
int amg_storage_save_account(const char *path, const AmgAccount *account,
                             const char *master_password, AmgError *error);
int amg_storage_load_remembered_master(const char *path, char *output,
                                       size_t capacity);

#endif
