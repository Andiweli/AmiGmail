#include "app.h"
#include "account.h"
#include "buffer.h"
#include "codec.h"
#include "gui.h"
#include "i18n.h"
#include "storage.h"

#include <stdio.h>
#include <string.h>

static void print_local_error(const char *message)
{
    AmgBuffer local;
    amg_buffer_init(&local);
    if (message && amg_utf8_to_local(message, &local) == AMG_OK &&
        amg_buffer_terminate(&local) == AMG_OK)
        fprintf(stderr, "AmiGmail: %s\n", (const char *)local.data);
    else
        fprintf(stderr, "AmiGmail: %s\n", amg_tr("Fehler", "Error"));
    amg_buffer_free(&local);
}

int amg_app_run(int argc, char **argv)
{
    AmgAccount account;
    AmgGui *gui;
    AmgError error;
    int result;
    const char *config = "ENVARC:AmiGmail/account.cfg";
    (void)argc;
    (void)argv;
    memset(&error, 0, sizeof(error));
    amg_i18n_init();
    amg_account_init(&account);
#if AMIGMAIL_AMIGA
    result = amg_storage_load_account(config, NULL, &account, &error);
    /* Older account files may not contain automatic local unlocking yet. */
    if (result != AMG_OK && result != AMG_ERR_AUTH) {
        amg_account_clear(&account);
        amg_account_init(&account);
    }
#else
    (void)config;
#endif
    gui = amg_gui_create(&account, &error);
    if (!gui) {
        print_local_error(error.message);
        amg_account_clear(&account);
        return 20;
    }
    result = amg_gui_run(gui, &error);
    if (result != AMG_OK) print_local_error(error.message);
    amg_gui_destroy(gui);
    amg_account_clear(&account);
    return result == AMG_OK ? 0 : 20;
}
