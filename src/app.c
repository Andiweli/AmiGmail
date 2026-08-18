#include "app.h"
#include "account.h"
#include "buffer.h"
#include "codec.h"
#include "gui.h"
#include "i18n.h"
#include "mailto.h"
#include "storage.h"
#include "splash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if AMIGMAIL_AMIGA
#include <proto/dos.h>
#endif

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
    AmgMailtoServer *mailto_server = NULL;
    AmgMailtoRequest mailto_request;
    AmgError error;
    int result;
    int detached_mailto_child = 0;
    char *startup_mailto = NULL;
    const char *raw_arguments = NULL;
    const char *config = "ENVARC:AmiGmail/account.cfg";

    memset(&error, 0, sizeof(error));
    amg_i18n_init();
#if AMIGMAIL_AMIGA
    raw_arguments = (const char *)GetArgStr();
#endif
    startup_mailto = amg_mailto_startup_url(
        argc, argv, raw_arguments, &detached_mailto_child, &error);
    if (error.code != 0 && !startup_mailto) {
        print_local_error(error.message);
        return 20;
    }

    amg_mailto_request_init(&mailto_request);
    if (startup_mailto) {
        result = amg_mailto_parse(startup_mailto, &mailto_request, &error);
        amg_mailto_request_clear(&mailto_request);
        if (result != AMG_OK) {
            print_local_error(error.message);
            free(startup_mailto);
            return 20;
        }

        /* First try the already-running instance.  This is the fast path for
         * every browser click after AmiGmail is open. */
        if (amg_mailto_forward_to_running(startup_mailto)) {
            free(startup_mailto);
            return 0;
        }

        /* A browser's external-command action may wait for the command to
         * terminate.  The first mailto: launch therefore turns itself into a
         * short-lived hand-off process and starts the real AmiGmail instance
         * asynchronously.  The detached child is marked by the private temp
         * file argument so it does not recurse here. */
        if (!detached_mailto_child &&
            amg_mailto_spawn_detached(startup_mailto)) {
            free(startup_mailto);
            return 0;
        }
    }

    /* Register the public mailto hand-off port before opening dialogs.
     * This lets a browser hand a request to AmiGmail even while the primary
     * instance is still asking for its master password.  If another detached
     * instance won the startup race, forward this request to it and exit. */
    mailto_server = amg_mailto_server_create();
    if (!mailto_server && startup_mailto &&
        amg_mailto_forward_to_running(startup_mailto)) {
        free(startup_mailto);
        return 0;
    }

    /* The splash appears only for the real primary instance. Mailto hand-off
     * helper processes have already returned above, so browser clicks do not
     * flash an unnecessary startup window. */
    amg_splash_open();

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
        amg_splash_close();
        print_local_error(error.message);
        amg_mailto_server_destroy(mailto_server);
        amg_account_clear(&account);
        free(startup_mailto);
        return 20;
    }

    /* All account/configuration data is loaded and the complete ReAction GUI
     * object tree exists now. Hand over directly to the real main window. */
    amg_splash_close();
    result = amg_gui_run(gui, mailto_server, startup_mailto, &error);
    if (result != AMG_OK) print_local_error(error.message);
    amg_gui_destroy(gui);
    amg_mailto_server_destroy(mailto_server);
    amg_account_clear(&account);
    free(startup_mailto);
    return result == AMG_OK ? 0 : 20;
}
