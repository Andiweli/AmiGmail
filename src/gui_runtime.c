#include "gui_internal.h"
#include "i18n.h"

#if AMIGMAIL_AMIGA

#include <clib/alib_protos.h>
#include <classes/window.h>
#include <devices/timer.h>
#include <exec/io.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/window.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>

#define GUI_PERIODIC_FETCH_SECONDS 300UL
#define T(de, en) amg_tr((de), (en))

static void periodic_timer_disarm(AmgGui *gui)
{
    if (!gui || !gui->periodic_timer_request ||
        !gui->periodic_timer_pending)
        return;
    if (!CheckIO((struct IORequest *)gui->periodic_timer_request))
        AbortIO((struct IORequest *)gui->periodic_timer_request);
    WaitIO((struct IORequest *)gui->periodic_timer_request);
    gui->periodic_timer_pending = 0;
}

static void periodic_timer_arm(AmgGui *gui)
{
    if (!gui || !gui->periodic_timer_request ||
        !gui->periodic_timer_device_open || gui->periodic_timer_pending ||
        !gui->account || !gui->account->periodic_fetch)
        return;
    gui->periodic_timer_request->tr_node.io_Command = TR_ADDREQUEST;
    gui->periodic_timer_request->tr_time.tv_secs =
        GUI_PERIODIC_FETCH_SECONDS;
    gui->periodic_timer_request->tr_time.tv_micro = 0UL;
    SendIO((struct IORequest *)gui->periodic_timer_request);
    gui->periodic_timer_pending = 1;
}

static int periodic_timer_init(AmgGui *gui)
{
    if (!gui) return 0;
    if (gui->periodic_timer_device_open && gui->periodic_timer_request) {
        periodic_timer_restart(gui);
        return 1;
    }
    gui->periodic_timer_port = CreateMsgPort();
    if (!gui->periodic_timer_port) return 0;
    gui->periodic_timer_request = (struct timerequest *)CreateIORequest(
        gui->periodic_timer_port, sizeof(struct timerequest));
    if (!gui->periodic_timer_request) {
        DeleteMsgPort(gui->periodic_timer_port);
        gui->periodic_timer_port = NULL;
        return 0;
    }
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK,
                   (struct IORequest *)gui->periodic_timer_request, 0UL) != 0) {
        DeleteIORequest((struct IORequest *)gui->periodic_timer_request);
        DeleteMsgPort(gui->periodic_timer_port);
        gui->periodic_timer_request = NULL;
        gui->periodic_timer_port = NULL;
        return 0;
    }
    gui->periodic_timer_device_open = 1;
    gui->periodic_timer_pending = 0;
    periodic_timer_arm(gui);
    return 1;
}

void periodic_timer_restart(AmgGui *gui)
{
    if (!gui || !gui->periodic_timer_request) return;
    periodic_timer_disarm(gui);
    periodic_timer_arm(gui);
}

void periodic_timer_cleanup(AmgGui *gui)
{
    if (!gui) return;
    periodic_timer_disarm(gui);
    if (gui->periodic_timer_device_open && gui->periodic_timer_request) {
        CloseDevice((struct IORequest *)gui->periodic_timer_request);
        gui->periodic_timer_device_open = 0;
    }
    if (gui->periodic_timer_request) {
        DeleteIORequest((struct IORequest *)gui->periodic_timer_request);
        gui->periodic_timer_request = NULL;
    }
    if (gui->periodic_timer_port) {
        DeleteMsgPort(gui->periodic_timer_port);
        gui->periodic_timer_port = NULL;
    }
    gui->periodic_timer_pending = 0;
}

static ULONG periodic_timer_signal_mask(const AmgGui *gui)
{
    if (!gui || !gui->periodic_timer_port) return 0UL;
    return 1UL << gui->periodic_timer_port->mp_SigBit;
}

int amg_gui_run(AmgGui *gui, AmgMailtoServer *mailto_server,
                const char *startup_mailto, AmgError *error)
{
    ULONG window_signal;
    ULONG mailto_signal;
    if (!gui) return AMG_ERR_ARGUMENT;
    gui->window = RA_OpenWindow(gui->window_object);
    if (!gui->window) {
        amg_error_set(error, AMG_ERR_IO,
                      T("Workbench-Fenster konnte nicht ge\303\266ffnet werden.", "Workbench window could not be opened."));
        return AMG_ERR_IO;
    }
    if (!gui->window_state_valid)
        center_window_on_screen(gui->window);
    draw_window_overlays(gui);
    GetAttr(WINDOW_SigMask, gui->window_object, &window_signal);
    mailto_signal = amg_mailto_server_signal_mask(mailto_server);
    gui->running = 1;
    gui->preview_url_signal_task = FindTask(NULL);
    gui->preview_url_signal_bit = AllocSignal(-1);
    if (gui->preview_url_signal_bit >= 0)
        gui->preview_url_signal_mask =
            1UL << (ULONG)gui->preview_url_signal_bit;
    else
        gui->preview_url_signal_mask = 0;

    if (account_is_locked(gui->account)) {
        account_dialog(gui, error);
        draw_window_overlays(gui);
    }
    if (!periodic_timer_init(gui) && gui->account->periodic_fetch)
        status_local(gui,
            T("Periodischer Abruf ist nicht verf\374gbar (timer.device).",
              "Periodic fetch is unavailable (timer.device)."));

    if (!account_is_locked(gui->account) && gui->account->fetch_on_start) {
        /* Der Update-Check soll den initialen Gmail-Abruf nicht in der
         * seriellen Netzwerk-Queue ueberholen. */
        gui->update_check_deferred = 1;
        fetch_mail(gui, error);
    } else {
        gui_update_request_check(gui);
    }

    if (startup_mailto && !account_is_locked(gui->account))
        (void)open_mailto_compose(gui, startup_mailto, error);
    else if (startup_mailto)
        status_local(gui,
            T("mailto:-Link abgebrochen: Gmail-Konto ist gesperrt.",
              "mailto: link cancelled: Gmail account is locked."));

    while (gui->running) {
        ULONG network_signal = amg_network_signal_mask(gui->network);
        ULONG timer_signal = periodic_timer_signal_mask(gui);
        ULONG signals = Wait(window_signal | network_signal | timer_signal |
                             mailto_signal | gui->preview_url_signal_mask |
                             SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C) gui->running = 0;
        if (network_signal && (signals & network_signal)) {
            handle_network(gui);
            draw_window_overlays(gui);
        }
        if (mailto_signal && (signals & mailto_signal)) {
            handle_mailto_requests(gui, mailto_server, error);
            draw_window_overlays(gui);
        }
        if (timer_signal && (signals & timer_signal)) {
            if (gui->periodic_timer_pending) {
                WaitIO((struct IORequest *)gui->periodic_timer_request);
                gui->periodic_timer_pending = 0;
            }
            periodic_timer_arm(gui);
            periodic_fetch_mail(gui, error);
        }
        if (signals & window_signal) {
            ULONG result;
            while ((result = RA_HandleInput(gui->window_object, NULL)) !=
                   WMHI_LASTMSG) {
                switch (result & WMHI_CLASSMASK) {
                    case WMHI_CLOSEWINDOW:
                        gui->running = 0;
                        break;
                    case WMHI_GADGETUP:
                        handle_main_gadget(
                            gui, result & WMHI_GADGETMASK, error);
                        break;
                    case WMHI_MENUPICK:
                        handle_menu(gui, result & 0xffffUL, error);
                        break;
                    case WMHI_RAWKEY:
                        if (gui->move_pending && rawkey_is_cancel(result))
                            cancel_pending_move(gui);
                        break;
                }
            }
            draw_window_overlays(gui);
        }
        if (gui->pending_preview_url_ready)
            open_pending_preview_url(gui);
    }
    if (gui->preview_url_signal_bit >= 0) {
        FreeSignal(gui->preview_url_signal_bit);
        gui->preview_url_signal_bit = -1;
        gui->preview_url_signal_mask = 0;
    }
    gui->preview_url_signal_task = NULL;
    periodic_timer_cleanup(gui);
    gui_state_save_window(gui);
    gui_state_set_mail_status_inactive();
    amg_network_stop(gui->network);
    gui->window = NULL;
    return AMG_OK;
}

#endif /* AMIGMAIL_AMIGA */
