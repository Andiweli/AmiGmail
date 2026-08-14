#include "gui_internal.h"
#include "banner_data.h"
#include "storage.h"
#include "i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if AMIGMAIL_AMIGA

#include <clib/alib_protos.h>
#include <classes/window.h>
#include <dos/dos.h>
#include <exec/libraries.h>
#include <gadgets/button.h>
#include <gadgets/layout.h>
#include <gadgets/string.h>
#include <intuition/classes.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <proto/button.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <proto/string.h>
#include <proto/window.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <utility/tagitem.h>

/*
 * Keep the same classic-GCC/ReAction NewObject handling as gui.c.  This is
 * purely a translation-unit requirement; it does not change object creation.
 */
#ifdef NewObject
#undef NewObject
#endif

#ifdef ButtonObject
#undef ButtonObject
#endif
#define ButtonObject NewObject(NULL, (CONST_STRPTR)"button.gadget"

#define ACCOUNT_PATH "ENVARC:AmiGmail/account.cfg"
#define ACCOUNT_DRAWER "ENVARC:AmiGmail"
#define GUI_ACCOUNT_FIELD_GAP 1
#define GUI_ACCOUNT_LABEL_WIDTH 150
#define T(de, en) amg_tr((de), (en))

enum AccountGadgetId {
    GID_ACCOUNT_NAME = 100,
    GID_ACCOUNT_EMAIL,
    GID_ACCOUNT_APP_PASSWORD,
    GID_ACCOUNT_MASTER_PASSWORD,
    GID_ACCOUNT_FETCH_DAYS,
    GID_ACCOUNT_FETCH_ON_START,
    GID_ACCOUNT_PERIODIC_FETCH,
    GID_ACCOUNT_STATUS,
    GID_ACCOUNT_UNLOCK,
    GID_ACCOUNT_SAVE,
    GID_ACCOUNT_CANCEL
};

enum ConfirmGadgetId {
    GID_CONFIRM_YES = 300,
    GID_CONFIRM_NO
};

enum AboutGadgetId {
    GID_ABOUT_OK = 400
};

static int ensure_config_drawer(AmgError *error)
{
    BPTR lock = Lock((CONST_STRPTR)ACCOUNT_DRAWER, ACCESS_READ);
    if (lock) {
        UnLock(lock);
        return AMG_OK;
    }
    lock = CreateDir((CONST_STRPTR)ACCOUNT_DRAWER);
    if (!lock) {
        amg_error_set(error, AMG_ERR_IO,
                      T("ENVARC:AmiGmail konnte nicht angelegt werden.", "ENVARC:AmiGmail could not be created."));
        return AMG_ERR_IO;
    }
    UnLock(lock);
    return AMG_OK;
}

static void normalize_app_password(const char *source, char output[64])
{
    size_t used = 0;
    while (source && *source && used + 1U < 64U) {
        unsigned char c = (unsigned char)*source++;
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            output[used++] = (char)c;
    }
    output[used] = 0;
}

int account_is_locked(const AmgAccount *account)
{
    if (!account || !account->email[0]) return 1;
    if (account->auth_mode == AMG_AUTH_OAUTH2)
        return !account->refresh_token;
    return !account->app_password;
}

static void replace_account(AmgGui *gui, AmgAccount *replacement)
{
    amg_network_stop(gui->network);
    gui->mail_network_started = 0;
    gui->periodic_check_pending = 0;
    amg_account_clear(gui->account);
    *gui->account = *replacement;
    replacement->app_password = NULL;
    replacement->refresh_token = NULL;
}

int account_dialog(AmgGui *gui, AmgError *error)
{
    Object *dialog;
    struct Window *window;
    struct Gadget *name_gadget, *email_gadget, *app_password_gadget;
    struct Gadget *master_password_gadget, *fetch_days_gadget;
    struct Gadget *fetch_on_start_gadget, *periodic_fetch_gadget;
    struct Gadget *dialog_status;
    ULONG signal_mask;
    ULONG account_width = 375UL;
    ULONG hint_gap = 4UL;
    char remembered_master[128];
    char fetch_days_text[16];
    int done = 0, changed = 0;
    int restart_network = gui->mail_network_started &&
                          amg_network_is_running(gui->network);

    if (restart_network) {
        amg_network_stop(gui->network);
        gui->periodic_check_pending = 0;
    }

    name_gadget = NULL;
    email_gadget = NULL;
    app_password_gadget = NULL;
    master_password_gadget = NULL;
    fetch_days_gadget = NULL;
    fetch_on_start_gadget = NULL;
    periodic_fetch_gadget = NULL;
    dialog_status = NULL;
    remembered_master[0] = 0;
    snprintf(fetch_days_text, sizeof(fetch_days_text), "%u",
             gui->account->fetch_days ? gui->account->fetch_days : 180U);
    amg_storage_load_remembered_master(
        ACCOUNT_PATH, remembered_master, sizeof(remembered_master));
    if (gui->screen && (ULONG)gui->screen->Width > 40UL) {
        ULONG available_width = (ULONG)gui->screen->Width - 20UL;
        if (account_width > available_width) account_width = available_width;
    }
    if (account_width < 360UL) account_width = 360UL;

    /* Eine halbe Textzeile Abstand ober- und unterhalb des Hinweises.
     * Bei der klassischen 8-Pixel-Topaz-Schrift sind das 4 Pixel. */
    if (gui->screen && gui->screen->Font && gui->screen->Font->ta_YSize)
        hint_gap = ((ULONG)gui->screen->Font->ta_YSize + 1UL) / 2UL;
    if (hint_gap < 2UL) hint_gap = 2UL;

    dialog = WindowObject,
        WA_Title, T("AmiGmail - Konto-Einstellungen", "AmiGmail - Account settings"),
        WA_Flags, WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                      WFLG_ACTIVATE,
        WA_PubScreen, gui->screen,
        WA_Width, account_width,
        WA_MinWidth, 360,
        WA_MaxWidth, 8192,
        WINDOW_Position, WPOS_CENTERSCREEN,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_RAWKEY,
        WINDOW_RefWindow, gui->window,
        WINDOW_ParentGroup, VGroupObject,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_SpaceInner, FALSE,

            /* Alle Dialoginhalte liegen in EINER ShrinkWrap-Gruppe.
             * So landet ueberschuessige Fensterhoehe am Aussenrand und
             * nicht als zwei Zeilen Leerraum ueber/unter dem Hinweistext. */
            LAYOUT_AddChild, VGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
                LAYOUT_ShrinkWrap, TRUE,

            /* Die Kontofelder bilden eine eigene ShrinkWrap-Gruppe.
             * layout.gadget verteilt freie Fensterhoehe sonst gleichmaessig
             * zwischen festen Kindern. Dadurch entstanden die grossen
             * vertikalen Luecken zwischen den Eingabezeilen. In dieser
             * Gruppe bleiben nur die expliziten 1-Pixel-Abstaende erhalten. */
            LAYOUT_AddChild, VGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
                LAYOUT_ShrinkWrap, TRUE,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T("Name:", "Name:")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        name_gadget = (struct Gadget *)StringObject,
                            GA_ID, GID_ACCOUNT_NAME,
                            GA_RelVerify, TRUE,
                            GA_TabCycle, TRUE,
                            STRINGA_MaxChars, 95,
                            STRINGA_TextVal, gui->account->display_name,
                        EndObject,
                    EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                EndObject,
                CHILD_MinHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_MaxHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T("Gmail-Adresse:", "Gmail address:")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        email_gadget = (struct Gadget *)StringObject,
                            GA_ID, GID_ACCOUNT_EMAIL,
                            GA_RelVerify, TRUE,
                            GA_TabCycle, TRUE,
                            STRINGA_MaxChars, 255,
                            STRINGA_TextVal, gui->account->email,
                        EndObject,
                    EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                EndObject,
                CHILD_MinHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_MaxHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T("Gmail-App-Passwort:", "Gmail app password:")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        app_password_gadget = (struct Gadget *)StringObject,
                            GA_ID, GID_ACCOUNT_APP_PASSWORD,
                            GA_RelVerify, TRUE,
                            GA_TabCycle, TRUE,
                            STRINGA_MaxChars, 63,
                            STRINGA_HookType, SHK_PASSWORD,
                            STRINGA_TextVal,
                                gui->account->app_password ?
                                    gui->account->app_password : "",
                        EndObject,
                    EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                EndObject,
                CHILD_MinHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_MaxHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T("Master-Passwort:", "Master password:")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        master_password_gadget = (struct Gadget *)StringObject,
                            GA_ID, GID_ACCOUNT_MASTER_PASSWORD,
                            GA_RelVerify, TRUE,
                            GA_TabCycle, TRUE,
                            STRINGA_MaxChars, 127,
                            STRINGA_HookType, SHK_PASSWORD,
                            STRINGA_TextVal, remembered_master,
                        EndObject,
                    EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                EndObject,
                CHILD_MinHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_MaxHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T("Abruf-Zeitraum (Tage):", "Fetch period (days):")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        fetch_days_gadget = (struct Gadget *)StringObject,
                            GA_ID, GID_ACCOUNT_FETCH_DAYS,
                            GA_RelVerify, TRUE,
                            GA_TabCycle, TRUE,
                            STRINGA_MaxChars, 5,
                            STRINGA_TextVal, fetch_days_text,
                        EndObject,
                    EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                EndObject,
                CHILD_MinHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_MaxHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, HGroupObject,
                        LAYOUT_SpaceOuter, FALSE,
                        LAYOUT_SpaceInner, FALSE,
                    EndObject,
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        fetch_on_start_gadget =
                            (struct Gadget *)ButtonObject,
                            GA_ID, GID_ACCOUNT_FETCH_ON_START,
                            GA_RelVerify, TRUE,
                            GA_Selected,
                                gui->account->fetch_on_start ? TRUE : FALSE,
                            BUTTON_AutoButton, BAG_CHECKBOX,
                            BUTTON_PushButton, TRUE,
                        EndObject,
                    CHILD_MinWidth, 24,
                    CHILD_MaxWidth, 24,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, static_text_label(
                        T("Gmail-Abruf beim Start", "Fetch Gmail at startup")),
                EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                EndObject,
                CHILD_MinHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_MaxHeight, GUI_ACCOUNT_FIELD_GAP,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, HGroupObject,
                        LAYOUT_SpaceOuter, FALSE,
                        LAYOUT_SpaceInner, FALSE,
                    EndObject,
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        periodic_fetch_gadget =
                            (struct Gadget *)ButtonObject,
                            GA_ID, GID_ACCOUNT_PERIODIC_FETCH,
                            GA_RelVerify, TRUE,
                            GA_Selected,
                                gui->account->periodic_fetch ? TRUE : FALSE,
                            BUTTON_AutoButton, BAG_CHECKBOX,
                            BUTTON_PushButton, TRUE,
                        EndObject,
                    CHILD_MinWidth, 24,
                    CHILD_MaxWidth, 24,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, static_text_label(
                        T("Periodischer Abruf (5 Min.)",
                          "Periodic fetch (5 min)")),
                EndObject,
                CHILD_WeightedHeight, 0,

            EndObject,
            CHILD_WeightedHeight, 0,

            /* Der Hinweis bekommt bewusst genau eine halbe Textzeile
             * Luft nach oben und unten. */
            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
            EndObject,
            CHILD_MinHeight, hint_gap,
            CHILD_MaxHeight, hint_gap,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild,
                dialog_status = (struct Gadget *)StringObject,
                    GA_ID, GID_ACCOUNT_STATUS,
                    GA_ReadOnly, TRUE,
                    STRINGA_TextVal,
                        T("Kein normales Google-Passwort verwenden.", "Do not use your normal Google password."),
                EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
            EndObject,
            CHILD_MinHeight, hint_gap,
            CHILD_MaxHeight, hint_gap,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_ACCOUNT_UNLOCK,
                    GA_RelVerify, TRUE,
                    GA_Text, T("_Entsperren", "_Unlock"),
                EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_ACCOUNT_SAVE,
                    GA_RelVerify, TRUE,
                    GA_Text, T("_Speichern", "_Save"),
                EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_ACCOUNT_CANCEL,
                    GA_RelVerify, TRUE,
                    GA_Text, T("Ab_brechen", "_Cancel"),
                EndObject,
            EndObject,
            CHILD_WeightedHeight, 0,

            EndObject,
            CHILD_WeightedHeight, 0,
        EndObject,
    EndWindow;

    if (!dialog) {
        amg_secure_clear(remembered_master, sizeof(remembered_master));
        amg_error_set(error, AMG_ERR_MEMORY,
                      T("Kontodialog konnte nicht erzeugt werden.", "Account dialog could not be created."));
        return 0;
    }
    window = RA_OpenWindow(dialog);
    if (!window) {
        DisposeObject(dialog);
        amg_secure_clear(remembered_master, sizeof(remembered_master));
        amg_error_set(error, AMG_ERR_IO,
                      T("Kontodialog konnte nicht ge\303\266ffnet werden.", "Account dialog could not be opened."));
        return 0;
    }
    GetAttr(WINDOW_SigMask, dialog, &signal_mask);

    while (!done) {
        ULONG signals = Wait(signal_mask | SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C) done = 1;
        if (signals & signal_mask) {
            ULONG result;
            while ((result = RA_HandleInput(dialog, NULL)) != WMHI_LASTMSG) {
                ULONG gadget_id = 0UL;
                switch (result & WMHI_CLASSMASK) {
                    case WMHI_CLOSEWINDOW:
                        done = 1;
                        break;

                    case WMHI_RAWKEY:
                        if (rawkey_is_cancel(result)) {
                            done = 1;
                        } else if (rawkey_is_accept(result)) {
                            /* Ein vorhandenes, noch gesperrtes Konto wird mit
                             * Return entsperrt. Bei neuen/geaenderten Daten ist
                             * Speichern die positive Standardaktion. */
                            gadget_id =
                                account_is_locked(gui->account) &&
                                gui->account->email[0] &&
                                !*string_text(app_password_gadget)
                                    ? GID_ACCOUNT_UNLOCK
                                    : GID_ACCOUNT_SAVE;
                        }
                        break;

                    case WMHI_GADGETUP:
                        gadget_id = result & WMHI_GADGETMASK;
                        break;
                }
                if (gadget_id) {
                        switch (gadget_id) {
                            case GID_ACCOUNT_CANCEL:
                                done = 1;
                                break;

                            case GID_ACCOUNT_UNLOCK:
                            {
                                AmgAccount loaded;
                                const char *master =
                                    string_text(master_password_gadget);
                                if (!*master) {
                                    set_string(dialog_status, window,
                                               T("Master-Passwort eingeben.", "Enter the master password."));
                                    break;
                                }
                                amg_account_init(&loaded);
                                if (amg_storage_load_account(
                                        ACCOUNT_PATH, master, &loaded,
                                        error) == AMG_OK) {
                                    if (amg_storage_save_account(
                                            ACCOUNT_PATH, &loaded, master,
                                            error) == AMG_OK) {
                                        replace_account(gui, &loaded);
                                        changed = 1;
                                        done = 1;
                                    } else {
                                        set_utf8_string(
                                            dialog_status, window,
                                            error->message);
                                        amg_account_clear(&loaded);
                                    }
                                } else {
                                    set_utf8_string(dialog_status, window,
                                                    error->message);
                                    amg_account_clear(&loaded);
                                }
                                break;
                            }

                            case GID_ACCOUNT_SAVE:
                            {
                                AmgAccount candidate;
                                char normalized_password[64];
                                char *days_end = NULL;
                                unsigned long fetch_days;
                                ULONG fetch_on_start = 0;
                                ULONG periodic_fetch = 0;
                                const char *master =
                                    string_text(master_password_gadget);
                                if (!*master) {
                                    set_string(
                                        dialog_status, window,
                                        T("Master-Passwort zum Verschl\374sseln eingeben.", "Enter a master password for encryption."));
                                    break;
                                }
                                amg_account_init(&candidate);
                                strncpy(candidate.display_name,
                                        string_text(name_gadget),
                                        sizeof(candidate.display_name) - 1U);
                                strncpy(candidate.email,
                                        string_text(email_gadget),
                                        sizeof(candidate.email) - 1U);
                                candidate.display_name[
                                    sizeof(candidate.display_name) - 1U] = 0;
                                candidate.email[sizeof(candidate.email) - 1U] = 0;
                                candidate.auth_mode = AMG_AUTH_APP_PASSWORD;
                                fetch_days = strtoul(
                                    string_text(fetch_days_gadget),
                                    &days_end, 10);
                                while (days_end && *days_end == ' ') ++days_end;
                                if (!string_text(fetch_days_gadget)[0] ||
                                    (days_end && *days_end) ||
                                    fetch_days < 1UL || fetch_days > 3650UL) {
                                    amg_account_clear(&candidate);
                                    set_string(
                                        dialog_status, window,
                                        T("Abruf-Zeitraum: 1 bis 3650 Tage eingeben.", "Fetch period: enter 1 to 3650 days."));
                                    break;
                                }
                                candidate.fetch_days = (unsigned int)fetch_days;
                                GetAttr(GA_Selected,
                                        (Object *)fetch_on_start_gadget,
                                        &fetch_on_start);
                                candidate.fetch_on_start =
                                    fetch_on_start ? 1 : 0;
                                GetAttr(GA_Selected,
                                        (Object *)periodic_fetch_gadget,
                                        &periodic_fetch);
                                candidate.periodic_fetch =
                                    periodic_fetch ? 1 : 0;
                                normalize_app_password(
                                    string_text(app_password_gadget),
                                    normalized_password);
                                if (amg_account_set_secret(
                                        &candidate.app_password,
                                        normalized_password) != AMG_OK) {
                                    amg_account_clear(&candidate);
                                    set_string(dialog_status, window,
                                               T("Nicht genug Speicher.", "Not enough memory."));
                                    break;
                                }
                                if (amg_account_validate(&candidate, error) !=
                                    AMG_OK) {
                                    set_utf8_string(dialog_status, window,
                                                    error->message);
                                    amg_account_clear(&candidate);
                                    break;
                                }
                                if (ensure_config_drawer(error) != AMG_OK ||
                                    amg_storage_save_account(
                                        ACCOUNT_PATH, &candidate, master,
                                        error) != AMG_OK) {
                                    set_utf8_string(dialog_status, window,
                                                    error->message);
                                    amg_account_clear(&candidate);
                                    break;
                                }
                                replace_account(gui, &candidate);
                                changed = 1;
                                done = 1;
                                break;
                            }
                        }
                }
            }
        }
    }
    DisposeObject(dialog);
    amg_secure_clear(remembered_master, sizeof(remembered_master));
    if (changed) {
        periodic_timer_restart(gui);
        status_local(gui, T("Konto ist eingerichtet und entsperrt.",
                            "Account is configured and unlocked."));
    }
    if (restart_network && !account_is_locked(gui->account)) {
        int result = amg_network_start(gui->network, gui->account, error);
        if (result == AMG_OK) {
            gui->mail_network_started = 1;
            result = amg_network_request(gui->network, AMG_NET_CONNECT, 0,
                                         NULL, NULL, error);
        }
        if (result == AMG_OK)
            status_local(gui, T("Verbinde erneut mit Gmail...", "Reconnecting to Gmail..."));
        else
            status_utf8(gui, error->message);
    }
    return changed;
}

static void draw_about_banner(AmgGui *gui, struct Window *window,
                              Object *banner_slot)
{
    struct Gadget *gadget;
    if (!gui || !window || !banner_slot) return;
    gadget = (struct Gadget *)banner_slot;
    draw_embedded_banner_at(gui, window,
                            (LONG)gadget->LeftEdge,
                            (LONG)gadget->TopEdge,
                            (LONG)gadget->Width,
                            (LONG)gadget->Height);
}

void about_dialog(AmgGui *gui)
{
    Object *dialog;
    Object *banner_slot;
    struct Window *window;
    ULONG signal_mask = 0;
    LONG font_height = 8L;
    LONG line_height;
    int done = 0;

    if (!gui || !gui->window || !gui->screen) return;
    if (gui->screen->Font && gui->screen->Font->ta_YSize)
        font_height = (LONG)gui->screen->Font->ta_YSize;
    line_height = font_height + 2L;

    banner_slot = HGroupObject,
        LAYOUT_SpaceOuter, FALSE,
        LAYOUT_SpaceInner, FALSE,
    EndObject;
    if (!banner_slot) return;

    /* Eigenes ReAction-Fenster statt EasyRequestArgs(): WINDOW_RefWindow in
     * Kombination mit WPOS_CENTERWINDOW bestimmt die Position bereits vor
     * RA_OpenWindow(). Dadurch erscheint der About-Requester sofort sauber
     * zentriert ueber dem AmiGmail-Hauptfenster und springt nicht nachtraeglich. */
    dialog = WindowObject,
        WA_Title, T("\334ber AmiGmail", "About AmiGmail"),
        WA_PubScreen, gui->screen,
        WA_Flags, WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                  WFLG_ACTIVATE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_RAWKEY |
                  IDCMP_REFRESHWINDOW,
        WINDOW_RefWindow, gui->window,
        WINDOW_Position, WPOS_CENTERWINDOW,
        WINDOW_ParentGroup, VGroupObject,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_SpaceInner, FALSE,
            LAYOUT_ShrinkWrap, TRUE,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, banner_slot,
                CHILD_MinWidth, 170,
                CHILD_MaxWidth, 170,
                CHILD_WeightedWidth, 0,
                CHILD_MinHeight, 28,
                CHILD_MaxHeight, 28,
                CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, VGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                    LAYOUT_AddChild, static_text_label("AmiGmail " AMIGMAIL_VERSION),
                    CHILD_MinHeight, line_height,
                    CHILD_MaxHeight, line_height,
                    CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild,
                        static_text_label(T("Gmail-Client f\374r AmigaOS 3.2",
                                            "Gmail client for AmigaOS 3.2")),
                    CHILD_MinHeight, line_height,
                    CHILD_MaxHeight, line_height,
                    CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild,
                        static_text_label(T("ReAction, IMAP, SMTP und AmiSSL",
                                            "ReAction, IMAP, SMTP and AmiSSL")),
                    CHILD_MinHeight, line_height,
                    CHILD_MaxHeight, line_height,
                    CHILD_WeightedHeight, 0,
                EndObject,
            EndObject,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
            EndObject,
            CHILD_MinHeight, font_height / 2L,
            CHILD_MaxHeight, font_height / 2L,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild,
                static_text_label("\251 Andreas 'Andiweli' St\374rmer"),
            CHILD_MinHeight, line_height,
            CHILD_MaxHeight, line_height,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
            EndObject,
            CHILD_MinHeight, font_height / 2L,
            CHILD_MaxHeight, font_height / 2L,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild,
                static_text_label(T("Rechtlicher Hinweis:", "Legal notice:")),
            CHILD_MinHeight, line_height,
            CHILD_MaxHeight, line_height,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,
                static_text_label(T(
                    "AmiGmail ist ein unabh\344ngiges, nichtkommerzielles Freizeitprojekt.",
                    "AmiGmail is an independent, non-commercial hobby project.")),
            CHILD_MinHeight, line_height,
            CHILD_MaxHeight, line_height,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,
                static_text_label(T(
                    "Es steht in keiner Verbindung zu Google LLC und wird von Google",
                    "It is not affiliated with Google LLC and is not developed,")),
            CHILD_MinHeight, line_height,
            CHILD_MaxHeight, line_height,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,
                static_text_label(T(
                    "weder entwickelt, unterst\374tzt noch gesponsert.",
                    "supported or sponsored by Google.")),
            CHILD_MinHeight, line_height,
            CHILD_MaxHeight, line_height,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,
                static_text_label(T("Gmail ist eine Marke von Google LLC.",
                                    "Gmail is a trademark of Google LLC.")),
            CHILD_MinHeight, line_height,
            CHILD_MaxHeight, line_height,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
            EndObject,
            CHILD_MinHeight, font_height / 2L,
            CHILD_MaxHeight, font_height / 2L,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
                LAYOUT_AddChild, static_text_label(""),
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_ABOUT_OK,
                    GA_RelVerify, TRUE,
                    GA_Text, "OK",
                EndObject,
                CHILD_MinWidth, 80,
                CHILD_MaxWidth, 100,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, static_text_label(""),
            EndObject,
            CHILD_MinHeight, font_height + 8L,
            CHILD_MaxHeight, font_height + 8L,
            CHILD_WeightedHeight, 0,
        EndObject,
    EndWindow;
    if (!dialog) {
        DisposeObject(banner_slot);
        return;
    }

    window = RA_OpenWindow(dialog);
    if (!window) {
        DisposeObject(dialog);
        return;
    }

    draw_about_banner(gui, window, banner_slot);
    GetAttr(WINDOW_SigMask, dialog, &signal_mask);
    while (!done) {
        ULONG signals = Wait(signal_mask | SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C) done = 1;
        if (signals & signal_mask) {
            ULONG result;
            while ((result = RA_HandleInput(dialog, NULL)) != WMHI_LASTMSG) {
                switch (result & WMHI_CLASSMASK) {
                    case WMHI_CLOSEWINDOW:
                        done = 1;
                        break;
                    case WMHI_RAWKEY:
                        if (rawkey_is_cancel(result) || rawkey_is_accept(result))
                            done = 1;
                        break;
                    case WMHI_GADGETUP:
                        if ((result & WMHI_GADGETMASK) == GID_ABOUT_OK)
                            done = 1;
                        break;
                }
            }
            if (!done)
                draw_about_banner(gui, window, banner_slot);
        }
    }
    DisposeObject(dialog);
}

int confirm_question_dialog_for_window(AmgGui *gui,
                                              struct Window *ref_window,
                                              const char *question,
                                              const char *note, LONG width)
{
    LONG font_height = 8L;
    LONG question_height, note_height, text_height, button_height;
    Object *dialog;
    struct Window *window;
    ULONG signal_mask = 0;
    int done = 0;
    int confirmed = 0;

    if (!gui || !ref_window || !gui->screen) return 0;
    if (gui->screen->Font && gui->screen->Font->ta_YSize)
        font_height = (LONG)gui->screen->Font->ta_YSize;
    question_height = font_height + 4L;
    note_height = font_height + 2L;
    text_height = question_height + (note && *note ? note_height : 0L);
    button_height = font_height + 8L;

    /* Positionierung relativ zum Hauptfenster wird bereits vor RA_OpenWindow()
     * festgelegt. Dadurch gibt es keinen sichtbaren Sprung von einer
     * Standardposition in die Fenstermitte. */
    dialog = WindowObject,
        WA_Title, "AmiGmail",
        WA_Width, width,
        WA_MinWidth, width,
        WA_MaxWidth, width,
        WA_PubScreen, gui->screen,
        WA_Flags, WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                  WFLG_ACTIVATE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_RAWKEY,
        /* Haupt- und Compose-Fenster sind bereits auf dem Bildschirm
         * zentriert. Auf AmigaOS 3.2 verschiebt WINDOW_RefWindow zusammen
         * mit WINDOW_Position diesen kleinen Requester reproduzierbar nach
         * rechts. Daher hier bewusst direkt auf dem Bildschirm zentrieren. */
        WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_ParentGroup, VGroupObject,
            LAYOUT_SpaceOuter, TRUE,
            /* Den Abstand zwischen Fragetext und Buttons nicht vom Layout
             * verteilen lassen. Die Warnzeile bekommt nur noch minimale
             * Innenhoehe und schliesst direkt an die Buttonzeile an. */
            LAYOUT_SpaceInner, FALSE,
            /* Frage + optionale Warnzeile bilden EIN Layout-Kind. */
            LAYOUT_AddChild, VGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
                LAYOUT_AddChild, ButtonObject,
                    GA_ReadOnly, TRUE,
                    GA_Text, question ? question : "",
                    BUTTON_BevelStyle, BVS_NONE,
                    BUTTON_Transparent, TRUE,
                EndObject,
                CHILD_MinHeight, question_height,
                CHILD_MaxHeight, question_height,
                CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, ButtonObject,
                    GA_ReadOnly, TRUE,
                    GA_Text, note && *note ? note : "",
                    BUTTON_BevelStyle, BVS_NONE,
                    BUTTON_Transparent, TRUE,
                EndObject,
                CHILD_MinHeight, note && *note ? note_height : 0,
                CHILD_MaxHeight, note && *note ? note_height : 0,
                CHILD_WeightedHeight, 0,
            EndObject,
            CHILD_MinHeight, text_height,
            CHILD_MaxHeight, text_height,
            CHILD_WeightedHeight, 0,

            /* Genau eine Leerzeile zwischen Textblock und Ja/Nein.
             * Die Fensterhoehe wird vom Layout selbst bestimmt, damit hier
             * keine zusaetzlichen Leerzeilen durch eine feste WA_Height
             * entstehen. Das gilt fuer Loeschen und Papierkorb leeren. */
            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
            EndObject,
            CHILD_MinHeight, font_height,
            CHILD_MaxHeight, font_height,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_CONFIRM_YES,
                    GA_RelVerify, TRUE,
                    GA_Text, T("_Ja", "_Yes"),
                EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_CONFIRM_NO,
                    GA_RelVerify, TRUE,
                    GA_Text, T("_Nein", "_No"),
                EndObject,
            EndObject,
            CHILD_MinHeight, button_height,
            CHILD_MaxHeight, button_height,
            CHILD_WeightedHeight, 0,
        EndObject,
    EndWindow;
    if (!dialog) return 0;

    window = RA_OpenWindow(dialog);
    if (!window) {
        DisposeObject(dialog);
        return 0;
    }
    GetAttr(WINDOW_SigMask, dialog, &signal_mask);
    while (!done) {
        ULONG signals = Wait(signal_mask | SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C) done = 1;
        if (signals & signal_mask) {
            ULONG result;
            while ((result = RA_HandleInput(dialog, NULL)) != WMHI_LASTMSG) {
                switch (result & WMHI_CLASSMASK) {
                    case WMHI_CLOSEWINDOW:
                        done = 1;
                        break;
                    case WMHI_RAWKEY:
                        if (rawkey_is_accept(result)) {
                            confirmed = 1;
                            done = 1;
                        } else if (rawkey_is_cancel(result)) {
                            done = 1;
                        }
                        break;
                    case WMHI_GADGETUP:
                        if ((result & WMHI_GADGETMASK) == GID_CONFIRM_YES) {
                            confirmed = 1;
                            done = 1;
                        } else if ((result & WMHI_GADGETMASK) ==
                                   GID_CONFIRM_NO) {
                            done = 1;
                        }
                        break;
                }
            }
        }
    }
    DisposeObject(dialog);
    return confirmed;
}

static int confirm_question_dialog(AmgGui *gui, const char *question,
                                   const char *note, LONG width)
{
    return confirm_question_dialog_for_window(
        gui, gui ? gui->window : NULL, question, note, width);
}

int confirm_delete_dialog(AmgGui *gui)
{
    return confirm_question_dialog(
        gui, T("Mail wirklich l\366schen?", "Really delete mail?"),
        T("Dieser Vorgang kann nicht widerrufen werden.",
          "This action cannot be undone."), 310L);
}

int confirm_empty_trash_dialog(AmgGui *gui)
{
    return confirm_question_dialog(
        gui, T("Papierkorb wirklich leeren?", "Really empty Trash?"),
        NULL, 280L);
}

int confirm_empty_spam_dialog(AmgGui *gui)
{
    return confirm_question_dialog(
        gui, T("Spam wirklich leeren?", "Really empty Spam?"),
        NULL, 280L);
}

#endif /* AMIGMAIL_AMIGA */
