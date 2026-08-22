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
#include <libraries/asl.h>
#include <libraries/gadtools.h>
#include <proto/asl.h>
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
#define GUI_ACCOUNT_LABEL_WIDTH 150
#define GUI_ABOUT_BANNER_WIDTH 170L
#define GUI_ABOUT_BANNER_HEIGHT 28L
#define T(id, en) amg_tr((id), (en))

enum AccountGadgetId {
    GID_ACCOUNT_NAME = 100,
    GID_ACCOUNT_EMAIL,
    GID_ACCOUNT_APP_PASSWORD,
    GID_ACCOUNT_MASTER_PASSWORD,
    GID_ACCOUNT_FETCH_DAYS,
    GID_ACCOUNT_FETCH_ON_START,
    GID_ACCOUNT_PERIODIC_FETCH,
    GID_ACCOUNT_NOTIFICATION_SOUND,
    GID_ACCOUNT_NOTIFICATION_SOUND_PATH,
    GID_ACCOUNT_NOTIFICATION_SOUND_SELECT,
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
                      T(MSG_ENVARC_AMIGMAIL_COULD_NOT_BE_CREATED, "ENVARC:AmiGmail could not be created."));
        return AMG_ERR_IO;
    }
    UnLock(lock);
    return AMG_OK;
}

static int account_file_exists(void)
{
    BPTR lock = Lock((CONST_STRPTR)ACCOUNT_PATH, ACCESS_READ);
    if (!lock) return 0;
    UnLock(lock);
    return 1;
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
    amg_account_clear(gui->account);
    *gui->account = *replacement;
    replacement->app_password = NULL;
    replacement->refresh_token = NULL;
    /* The persisted Inbox UID high-water mark is account-specific. Reload
     * it after unlock or account replacement so the first fetch can already
     * recognise mail that arrived while AmiGmail was not running. */
    gui_state_load_inbox_notification(gui);
}

static int nullable_text_equal(const char *left, const char *right)
{
    if (!left) left = "";
    if (!right) right = "";
    return strcmp(left, right) == 0;
}

/* Only fields copied into the network worker matter here. Pure GUI/runtime
 * preferences (periodic fetch toggle, fetch-on-start and notification sound)
 * must not tear down a healthy Gmail connection. */
static int account_network_settings_equal(const AmgAccount *left,
                                          const AmgAccount *right)
{
    if (!left || !right) return 0;
    return !strcmp(left->display_name, right->display_name) &&
           !strcmp(left->email, right->email) &&
           left->auth_mode == right->auth_mode &&
           !strcmp(left->imap_host, right->imap_host) &&
           left->imap_port == right->imap_port &&
           !strcmp(left->smtp_host, right->smtp_host) &&
           left->smtp_port == right->smtp_port &&
           left->smtp_starttls == right->smtp_starttls &&
           left->fetch_days == right->fetch_days &&
           nullable_text_equal(left->app_password, right->app_password) &&
           nullable_text_equal(left->refresh_token, right->refresh_token);
}

static void notification_sound_initial_parts(const char *path,
                                             char *drawer,
                                             size_t drawer_capacity,
                                             char *file,
                                             size_t file_capacity)
{
    STRPTR part;
    if (!drawer || !drawer_capacity || !file || !file_capacity) return;
    drawer[0] = 0;
    file[0] = 0;
    if (!path || !*path) return;
    strncpy(drawer, path, drawer_capacity - 1U);
    drawer[drawer_capacity - 1U] = 0;
    part = FilePart((STRPTR)drawer);
    if (part && *part) {
        strncpy(file, (const char *)part, file_capacity - 1U);
        file[file_capacity - 1U] = 0;
        *part = 0;
    }
}

static void choose_notification_sound(AmgGui *gui,
                                      struct Window *window,
                                      struct Gadget *path_gadget,
                                      struct Gadget *enabled_gadget,
                                      struct Gadget *status_gadget)
{
    struct FileRequester *requester;
    char drawer[512];
    char file[256];
    char selected[512];
    char accept_pattern[96];
    LONG pattern_result;
    if (!path_gadget) return;

    /* ASLFR_AcceptPattern expects the tokenized output of
     * ParsePatternNoCase(), not the human-readable DOS pattern itself.
     * Passing the source pattern directly caused valid .iff files to be
     * filtered out on classic ASL. */
    pattern_result = ParsePatternNoCase(
        (CONST_STRPTR)"#?.(iff|8svx|wav)",
        (STRPTR)accept_pattern, (LONG)sizeof(accept_pattern));

    notification_sound_initial_parts(
        string_text(path_gadget), drawer, sizeof(drawer), file, sizeof(file));
    requester = AllocAslRequestTags(
        ASL_FileRequest,
        ASLFR_TitleText,
            (ULONG)(uintptr_t)T(MSG_SELECT_NOTIFICATION_SOUND, "Select notification sound"),
        ASLFR_Window, (ULONG)(uintptr_t)window,
        ASLFR_SleepWindow, TRUE,
        ASLFR_RejectIcons, TRUE,
        pattern_result >= 0 ? ASLFR_AcceptPattern : TAG_IGNORE,
            (ULONG)(uintptr_t)accept_pattern,
        drawer[0] ? ASLFR_InitialDrawer : TAG_IGNORE,
            (ULONG)(uintptr_t)drawer,
        file[0] ? ASLFR_InitialFile : TAG_IGNORE,
            (ULONG)(uintptr_t)file,
        TAG_DONE);
    if (!requester) return;

    if (AslRequest(requester, NULL)) {
        strncpy(selected, requester->fr_Drawer ? (const char *)requester->fr_Drawer : "",
                sizeof(selected) - 1U);
        selected[sizeof(selected) - 1U] = 0;
        if (requester->fr_File && requester->fr_File[0] &&
            AddPart((STRPTR)selected, (CONST_STRPTR)requester->fr_File,
                    (LONG)sizeof(selected))) {
            set_string(path_gadget, window, selected);
            /* Immediate preview: the same DataTypes playback path is used
             * later for real new-mail notifications. */
            if (gui_notify_preview_sound(gui, selected)) {
                if (status_gadget)
                    set_string(status_gadget, window,
                               T(MSG_PLAYING_SOUND_PREVIEW, "Playing sound preview..."));
            } else if (status_gadget) {
                set_string(status_gadget, window,
                           T(MSG_SOUND_FILE_COULD_NOT_BE_LOADED_PLAYED, "Sound file could not be loaded/played."));
            }
            if (enabled_gadget) {
                if (window)
                    SetGadgetAttrs(enabled_gadget, window, NULL,
                                   GA_Selected, TRUE, TAG_DONE);
                else
                    SetAttrs((Object *)enabled_gadget,
                             GA_Selected, TRUE, TAG_DONE);
            }
        }
    }
    FreeAslRequest(requester);
}

int account_dialog(AmgGui *gui, AmgError *error)
{
    Object *dialog;
    struct Window *window;
    struct Gadget *name_gadget, *email_gadget, *app_password_gadget;
    struct Gadget *master_password_gadget, *fetch_days_gadget;
    struct Gadget *fetch_on_start_gadget, *periodic_fetch_gadget;
    struct Gadget *notification_sound_gadget, *notification_sound_path_gadget;
    struct Gadget *dialog_status;
    ULONG signal_mask;
    ULONG notify_signal;
    ULONG account_width = 375UL;
    ULONG hint_gap = 4UL;
    char remembered_master[128];
    char fetch_days_text[16];
    int done = 0, changed = 0;
    int network_settings_changed = 0;
    int periodic_fetch_changed = 0;
    int stored_account_available = account_file_exists();
    int network_was_running = gui->mail_network_started &&
                              amg_network_is_running(gui->network);

    name_gadget = NULL;
    email_gadget = NULL;
    app_password_gadget = NULL;
    master_password_gadget = NULL;
    fetch_days_gadget = NULL;
    fetch_on_start_gadget = NULL;
    periodic_fetch_gadget = NULL;
    notification_sound_gadget = NULL;
    notification_sound_path_gadget = NULL;
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
        WA_Title, T(MSG_AMIGMAIL_ACCOUNT_SETTINGS, "AmiGmail - Account settings"),
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
             * Gruppe gelten die von ReAction vorgegebenen Innenabstaende. */
            LAYOUT_AddChild, VGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_ShrinkWrap, TRUE,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T(MSG_NAME, "Name:")),
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
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T(MSG_GMAIL_ADDRESS, "Gmail address:")),
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
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T(MSG_GMAIL_APP_PASSWORD, "Gmail app password:")),
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
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T(MSG_MASTER_PASSWORD, "Master password:")),
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
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(T(MSG_FETCH_PERIOD_DAYS, "Fetch period (days):")),
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
                        T(MSG_FETCH_GMAIL_AT_STARTUP, "Fetch Gmail at startup")),
                EndObject,
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
                        T(MSG_PERIODIC_FETCH_5_MIN, "Periodic fetch (5 min)")),
                EndObject,
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
                        notification_sound_gadget =
                            (struct Gadget *)ButtonObject,
                            GA_ID, GID_ACCOUNT_NOTIFICATION_SOUND,
                            GA_RelVerify, TRUE,
                            GA_Selected,
                                gui->account->notification_sound ? TRUE : FALSE,
                            BUTTON_AutoButton, BAG_CHECKBOX,
                            BUTTON_PushButton, TRUE,
                        EndObject,
                    CHILD_MinWidth, 24,
                    CHILD_MaxWidth, 24,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, static_text_label(
                        T(MSG_NOTIFICATION_SOUND, "Notification Sound")),
                EndObject,
                CHILD_WeightedHeight, 0,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceInner, TRUE,
                    LAYOUT_AddChild, static_text_label(
                        T(MSG_SOUND_FILE, "Sound file:")),
                    CHILD_MinWidth, GUI_ACCOUNT_LABEL_WIDTH,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        notification_sound_path_gadget =
                            (struct Gadget *)StringObject,
                            GA_ID, GID_ACCOUNT_NOTIFICATION_SOUND_PATH,
                            GA_ReadOnly, TRUE,
                            STRINGA_MaxChars, 511,
                            STRINGA_TextVal,
                                gui->account->notification_sound_path,
                        EndObject,
                    LAYOUT_AddChild, ButtonObject,
                        GA_ID, GID_ACCOUNT_NOTIFICATION_SOUND_SELECT,
                        GA_RelVerify, TRUE,
                        GA_Text, "...",
                    EndObject,
                    CHILD_MinWidth, 32,
                    CHILD_MaxWidth, 32,
                    CHILD_WeightedWidth, 0,
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
                        T(MSG_DO_NOT_USE_YOUR_NORMAL_GOOGLE_PASSWORD, "Do not use your normal Google password."),
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
                    GA_Disabled, stored_account_available ? FALSE : TRUE,
                    GA_Text, T(MSG_UNLOCK, "_Unlock"),
                EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_ACCOUNT_SAVE,
                    GA_RelVerify, TRUE,
                    GA_Text, T(MSG_SAVE, "_Save"),
                EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_ACCOUNT_CANCEL,
                    GA_RelVerify, TRUE,
                    GA_Text, T(MSG_CANCEL, "_Cancel"),
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
                      T(MSG_ACCOUNT_DIALOG_COULD_NOT_BE_CREATED, "Account dialog could not be created."));
        return 0;
    }
    window = RA_OpenWindow(dialog);
    if (!window) {
        DisposeObject(dialog);
        amg_secure_clear(remembered_master, sizeof(remembered_master));
        amg_error_set(error, AMG_ERR_IO,
                      T(MSG_ACCOUNT_DIALOG_COULD_NOT_BE_OPENED, "Account dialog could not be opened."));
        return 0;
    }
    GetAttr(WINDOW_SigMask, dialog, &signal_mask);
    notify_signal = gui_notify_signal_mask(gui);

    while (!done) {
        ULONG signals = Wait(signal_mask | notify_signal | SIGBREAKF_CTRL_C);
        if (notify_signal && (signals & notify_signal))
            gui_notify_handle_signal(gui);
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
                                stored_account_available &&
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

                            case GID_ACCOUNT_NOTIFICATION_SOUND_SELECT:
                                choose_notification_sound(
                                    gui, window, notification_sound_path_gadget,
                                    notification_sound_gadget, dialog_status);
                                break;

                            case GID_ACCOUNT_UNLOCK:
                            {
                                AmgAccount loaded;
                                const char *master =
                                    string_text(master_password_gadget);
                                if (!stored_account_available) {
                                    set_string(
                                        dialog_status, window,
                                        T(MSG_NO_ACCOUNT_HAS_BEEN_SAVED_YET_PLEASE_USE, "No account has been saved yet. Please use Save."));
                                    break;
                                }
                                if (!*master) {
                                    set_string(dialog_status, window,
                                               T(MSG_ENTER_THE_MASTER_PASSWORD, "Enter the master password."));
                                    break;
                                }
                                set_string(
                                    dialog_status, window,
                                    T(MSG_DECRYPTING_ACCOUNT_DATA, "Decrypting account data..."));
                                RefreshGList(dialog_status, window, NULL, 1);
                                amg_account_init(&loaded);
                                if (amg_storage_load_account(
                                        ACCOUNT_PATH, master, &loaded,
                                        error) == AMG_OK) {
                                    if (amg_storage_save_account(
                                            ACCOUNT_PATH, &loaded, master,
                                            error) == AMG_OK) {
                                        network_settings_changed =
                                            !account_network_settings_equal(
                                                gui->account, &loaded);
                                        periodic_fetch_changed =
                                            gui->account->periodic_fetch !=
                                            loaded.periodic_fetch;
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
                                ULONG notification_sound = 0;
                                const char *master =
                                    string_text(master_password_gadget);
                                if (!*master) {
                                    set_string(
                                        dialog_status, window,
                                        T(MSG_ENTER_A_MASTER_PASSWORD_FOR_ENCRYPTION, "Enter a master password for encryption."));
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
                                        T(MSG_FETCH_PERIOD_ENTER_1_TO_3650_DAYS, "Fetch period: enter 1 to 3650 days."));
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
                                GetAttr(GA_Selected,
                                        (Object *)notification_sound_gadget,
                                        &notification_sound);
                                candidate.notification_sound =
                                    notification_sound ? 1 : 0;
                                strncpy(candidate.notification_sound_path,
                                        string_text(
                                            notification_sound_path_gadget),
                                        sizeof(candidate.notification_sound_path) - 1U);
                                candidate.notification_sound_path[
                                    sizeof(candidate.notification_sound_path) - 1U] = 0;
                                if (candidate.notification_sound) {
                                    BPTR sound_lock;
                                    if (!candidate.notification_sound_path[0]) {
                                        amg_account_clear(&candidate);
                                        set_string(
                                            dialog_status, window,
                                            T(MSG_PLEASE_SELECT_AN_IFF_8SVX_WAV_SOUND_FILE, "Please select an IFF/8SVX/WAV sound file."));
                                        break;
                                    }
                                    sound_lock = Lock(
                                        (CONST_STRPTR)candidate.notification_sound_path,
                                        ACCESS_READ);
                                    if (!sound_lock) {
                                        amg_account_clear(&candidate);
                                        set_string(
                                            dialog_status, window,
                                            T(MSG_THE_SELECTED_SOUND_FILE_WAS_NOT_FOUND, "The selected sound file was not found."));
                                        break;
                                    }
                                    UnLock(sound_lock);
                                }
                                normalize_app_password(
                                    string_text(app_password_gadget),
                                    normalized_password);
                                if (amg_account_set_secret(
                                        &candidate.app_password,
                                        normalized_password) != AMG_OK) {
                                    amg_account_clear(&candidate);
                                    set_string(dialog_status, window,
                                               T(MSG_NOT_ENOUGH_MEMORY, "Not enough memory."));
                                    break;
                                }
                                if (amg_account_validate(&candidate, error) !=
                                    AMG_OK) {
                                    set_utf8_string(dialog_status, window,
                                                    error->message);
                                    amg_account_clear(&candidate);
                                    break;
                                }
                                set_string(
                                    dialog_status, window,
                                    T(MSG_ENCRYPTING_ACCOUNT_DATA, "Encrypting account data..."));
                                RefreshGList(dialog_status, window, NULL, 1);
                                if (ensure_config_drawer(error) != AMG_OK ||
                                    amg_storage_save_account(
                                        ACCOUNT_PATH, &candidate, master,
                                        error) != AMG_OK) {
                                    set_utf8_string(dialog_status, window,
                                                    error->message);
                                    amg_account_clear(&candidate);
                                    break;
                                }
                                network_settings_changed =
                                    !account_network_settings_equal(
                                        gui->account, &candidate);
                                periodic_fetch_changed =
                                    gui->account->periodic_fetch !=
                                    candidate.periodic_fetch;
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
        /* The five-minute timer only needs to be touched when its enable
         * state actually changed. Restarting it for unrelated settings (for
         * example the notification sound) risks carrying a stale timer
         * signal into the main event loop. */
        if (periodic_fetch_changed)
            periodic_timer_restart(gui);
        status_local(gui, T(MSG_ACCOUNT_IS_CONFIGURED_AND_UNLOCKED, "Account is configured and unlocked."));
    }
    if (changed && network_was_running && network_settings_changed &&
        !account_is_locked(gui->account)) {
        int result = amg_network_request_reconfigure(
            gui->network, gui->account, error);
        if (result == AMG_OK) {
            gui->network_reconfigure_pending = 1;
            status_local(gui,
                T(MSG_RECONNECTING_TO_GMAIL, "Reconnecting to Gmail..."));
        } else {
            status_utf8(gui, error->message);
        }
    }
    return changed;
}

static UWORD about_header_fill_pattern[2] = {0xffffU, 0xffffU};

static void draw_about_banner(AmgGui *gui, struct Window *window,
                              Object *banner_slot)
{
    struct Gadget *gadget;
    if (!gui || !window || !banner_slot) return;
    LONG banner_top;
    gadget = (struct Gadget *)banner_slot;

    /* The header row follows the selected Workbench font. Keep the fixed
     * 170x28 artwork vertically centred when three text lines make the row
     * taller than the bitmap itself. */
    banner_top = (LONG)gadget->TopEdge;
    if ((LONG)gadget->Height > GUI_ABOUT_BANNER_HEIGHT)
        banner_top +=
            ((LONG)gadget->Height - GUI_ABOUT_BANNER_HEIGHT) / 2L;

    draw_embedded_banner_at(gui, window,
                            (LONG)gadget->LeftEdge,
                            banner_top,
                            GUI_ABOUT_BANNER_WIDTH,
                            GUI_ABOUT_BANNER_HEIGHT);
}

void about_dialog(AmgGui *gui)
{
    Object *dialog;
    Object *banner_slot;
    struct Window *window;
    ULONG signal_mask = 0;
    LONG font_height = 8L;
    LONG line_height;
    LONG header_height;
    int done = 0;

    if (!gui || !gui->window || !gui->screen) return;
    if (gui->screen->Font && gui->screen->Font->ta_YSize)
        font_height = (LONG)gui->screen->Font->ta_YSize;
    line_height = font_height + 2L;
    header_height = line_height * 3L;
    if (header_height < GUI_ABOUT_BANNER_HEIGHT)
        header_height = GUI_ABOUT_BANNER_HEIGHT;

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
        WA_Title, T(MSG_ABOUT_AMIGMAIL, "About AmiGmail"),
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
                /* One continuous #888888 header band behind both the logo
                 * and the three transparent ReAction text labels. */
                LAYOUT_FillPen, gui->banner_pens[0],
                LAYOUT_FillPattern,
                    (ULONG)(uintptr_t)about_header_fill_pattern,
                LAYOUT_AddChild, banner_slot,
                CHILD_MinWidth, GUI_ABOUT_BANNER_WIDTH,
                CHILD_MaxWidth, GUI_ABOUT_BANNER_WIDTH,
                CHILD_WeightedWidth, 0,
                CHILD_MinHeight, header_height,
                CHILD_MaxHeight, header_height,
                CHILD_WeightedHeight, 0,
                LAYOUT_AddChild, VGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                    LAYOUT_AddChild, static_text_label("AmiGmail " AMIGMAIL_VERSION),
                    CHILD_MinHeight, line_height,
                    CHILD_MaxHeight, line_height,
                    CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild,
                        static_text_label(T(MSG_GMAIL_CLIENT_FOR_AMIGAOS_3_2, "Gmail client for AmigaOS 3.2")),
                    CHILD_MinHeight, line_height,
                    CHILD_MaxHeight, line_height,
                    CHILD_WeightedHeight, 0,
                    LAYOUT_AddChild,
                        static_text_label(T(MSG_REACTION_IMAP_SMTP_AND_AMISSL, "ReAction, IMAP, SMTP and AmiSSL")),
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
                static_text_label("\251 Andreas St\374rmer"),
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
                static_text_label(T(MSG_LEGAL_NOTICE, "Legal notice:")),
            CHILD_MinHeight, line_height,
            CHILD_MaxHeight, line_height,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,
                static_text_label(T(MSG_AMIGMAIL_IS_AN_INDEPENDENT_NON_COMMERCIAL_HOBBY_PROJECT, "AmiGmail is an independent, non-commercial hobby project.")),
            CHILD_MinHeight, line_height,
            CHILD_MaxHeight, line_height,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,
                static_text_label(T(MSG_IT_IS_NOT_AFFILIATED_WITH_GOOGLE_LLC_AND, "It is not affiliated with Google LLC and is not developed,")),
            CHILD_MinHeight, line_height,
            CHILD_MaxHeight, line_height,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,
                static_text_label(T(MSG_SUPPORTED_OR_SPONSORED_BY_GOOGLE, "supported or sponsored by Google.")),
            CHILD_MinHeight, line_height,
            CHILD_MaxHeight, line_height,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,
                static_text_label(T(MSG_GMAIL_IS_A_TRADEMARK_OF_GOOGLE_LLC, "Gmail is a trademark of Google LLC.")),
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
                    GA_Text, T(MSG_YES, "_Yes"),
                EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_CONFIRM_NO,
                    GA_RelVerify, TRUE,
                    GA_Text, T(MSG_NO, "_No"),
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
        gui, T(MSG_REALLY_DELETE_MAIL, "Really delete mail?"),
        T(MSG_THIS_ACTION_CANNOT_BE_UNDONE, "This action cannot be undone."), 310L);
}

int confirm_empty_trash_dialog(AmgGui *gui)
{
    return confirm_question_dialog(
        gui, T(MSG_REALLY_EMPTY_TRASH, "Really empty Trash?"),
        NULL, 280L);
}

int confirm_empty_spam_dialog(AmgGui *gui)
{
    return confirm_question_dialog(
        gui, T(MSG_REALLY_EMPTY_SPAM, "Really empty Spam?"),
        NULL, 280L);
}

#endif /* AMIGMAIL_AMIGA */
