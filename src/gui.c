#include "gui.h"
#include "banner_data.h"
#include "buffer.h"
#include "codec.h"
#include "imap.h"
#include "imap_parser.h"
#include "mime.h"
#include "network_task.h"
#include "storage.h"
#include "i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if AMIGMAIL_AMIGA
#include <clib/alib_protos.h>
#include <classes/window.h>
#include <dos/dos.h>
#include <devices/timer.h>
#include <exec/io.h>
#include <exec/libraries.h>
#include <exec/lists.h>
#include <exec/memory.h>
#include <gadgets/button.h>
#include <gadgets/layout.h>
#include <gadgets/listbrowser.h>
#include <gadgets/scroller.h>
#include <gadgets/string.h>
#include <gadgets/texteditor.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <images/bevel.h>
#include <intuition/classes.h>
#include <intuition/intuition.h>
#include <inline/macros.h>
#include <libraries/asl.h>
#include <libraries/gadtools.h>
#include <proto/asl.h>
#include <proto/button.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <proto/listbrowser.h>
#include <proto/scroller.h>
#include <proto/string.h>
#include <proto/texteditor.h>
#include <proto/window.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <utility/hooks.h>
#include <utility/tagitem.h>

/*
 * The classic GCC headers expose NewObject() as an inline varargs macro.
 * ReAction's WindowObject/EndWindow notation spans several expressions and
 * therefore needs the amiga.lib NewObject() varargs stub in this unit.
 */
#ifdef NewObject
#undef NewObject
#endif

#ifdef ButtonObject
#undef ButtonObject
#endif
#define ButtonObject NewObject(NULL, (CONST_STRPTR)"button.gadget"

/* Minimaler openurl.library-Aufruf ohne Abhaengigkeit vom OpenURL-SDK.
 * URL_OpenA() liegt bei der klassischen API am Library-Vektor 0x1e. */
#define AMG_URL_OpenA(url, tags) \
    LP2(0x1e, ULONG, URL_OpenA, STRPTR, (url), a0, \
        struct TagItem *, (tags), a1, , OpenURLBase)

struct Library *WindowBase = NULL;
struct Library *LayoutBase = NULL;
struct Library *ButtonBase = NULL;
struct Library *ListBrowserBase = NULL;
struct Library *ScrollerBase = NULL;
struct Library *StringBase = NULL;
struct Library *TextEditorBase = NULL;
struct Library *OpenURLBase = NULL;
struct Library *AslBase = NULL;
struct GfxBase *GfxBase = NULL;

#define ACCOUNT_PATH "ENVARC:AmiGmail/account.cfg"
#define ACCOUNT_DRAWER "ENVARC:AmiGmail"
#define LABEL_STATE_PATH "ENVARC:AmiGmail/labels.state"
#define BANNER_COLOR_COUNT 8U
#define COMPOSE_PATH_MAX 512U
#define COMPOSE_NAME_MAX 256U
#define GUI_REPLY_BODY_MAX 32768U
#define GUI_SCROLLBAR_WIDTH 16
#define GUI_MESSAGE_FLAG_COLUMN_WIDTH 16
#define GUI_TREE_IMAGE_WIDTH 9
#define GUI_TREE_IMAGE_HEIGHT 7
#define GUI_TREE_LEVEL_WIDTH GUI_TREE_IMAGE_WIDTH
/* The custom +/- hierarchy image is followed by a tiny text gap inside
 * listbrowser.gadget. Keep the tree renderer aligned to that compact layout
 * so the dotted trunk starts visually from the middle of the box. */
#define GUI_TREE_IMAGE_TRAILING_GAP 1
/* listbrowser.gadget reports the custom column bounds to the right of the
 * hierarchy slot. Shift the computed axis left by half the visible box width
 * so the vertical dotted line runs from the middle of [+]/[-]. */
#define GUI_TREE_AXIS_LEFT_SHIFT ((GUI_TREE_IMAGE_WIDTH - 1) / 2)
/* Child rows stay slightly left-shifted so the text block sits more directly
 * underneath the parent label instead of drifting to the right. */
#define GUI_TREE_CHILD_LEFT_SHIFT (GUI_TREE_AXIS_LEFT_SHIFT + 1)
#define GUI_TREE_DOT_STEP 2
#define GUI_URL_MAX 1024U
#define GUI_ACCOUNT_FIELD_GAP 1
#define GUI_ACCOUNT_LABEL_WIDTH 150
#define GUI_PERIODIC_FETCH_SECONDS 300UL
#define T(de, en) amg_tr((de), (en))

enum MainGadgetId {
    GID_NEW_MAIL = 1,
    GID_FETCH,
    GID_REPLY,
    GID_DELETE,
    GID_MOVE,
    GID_SEEN,
    GID_SYSTEM_LABELS,
    GID_LABELS,
    GID_LABELS_SCROLL,
    GID_MESSAGES,
    GID_MESSAGES_SCROLL,
    GID_PREVIEW,
    GID_PREVIEW_SCROLL,
    GID_SAVE_ATTACHMENTS,
    GID_STATUS
};

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

enum ComposeGadgetId {
    GID_COMPOSE_TO = 200,
    GID_COMPOSE_CC,
    GID_COMPOSE_BCC,
    GID_COMPOSE_SUBJECT,
    GID_COMPOSE_BODY,
    GID_COMPOSE_BODY_SCROLL,
    GID_COMPOSE_ATTACHMENTS,
    GID_COMPOSE_ATTACHMENTS_SCROLL,
    GID_COMPOSE_ADD_ATTACHMENT,
    GID_COMPOSE_REMOVE_ATTACHMENT,
    GID_COMPOSE_STATUS,
    GID_COMPOSE_SEND,
    GID_COMPOSE_CANCEL
};

enum ConfirmGadgetId {
    GID_CONFIRM_YES = 300,
    GID_CONFIRM_NO
};

enum AboutGadgetId {
    GID_ABOUT_OK = 400
};

#define MENU_ACCOUNT FULLMENUNUM(0, 0, NOSUB)
#define MENU_ABOUT FULLMENUNUM(0, 1, NOSUB)
#define MENU_QUIT FULLMENUNUM(0, 3, NOSUB)
#define MENU_EMPTY_TRASH FULLMENUNUM(1, 0, NOSUB)
#define MENU_EMPTY_SPAM FULLMENUNUM(1, 1, NOSUB)

typedef struct ComposeAttachment {
    char path[COMPOSE_PATH_MAX];
    char name_local[COMPOSE_NAME_MAX];
    char name_utf8[COMPOSE_NAME_MAX * 2U];
    unsigned long size;
} ComposeAttachment;

#define GUI_SYSTEM_LABEL_COUNT 7U
#define GUI_SYSTEM_LABEL_HIDDEN_INDEX 1U
#define GUI_SYSTEM_LABEL_VISIBLE_COUNT 6U

typedef struct GuiLabel {
    char display_local[128];
    char path_local[512];
    char mailbox_utf8[512];
    char gmail_label_utf8[512];
    char delimiter;
    unsigned short generation;
    unsigned long special_use;
    int available;
    int has_children;
    int expanded;
    size_t parent_index;
    int has_next_sibling;
} GuiLabel;

struct AmgGui {
    AmgAccount *account;
    AmgNetwork *network;
    Object *window_object;
    struct Window *window;
    struct Gadget *new_mail_gadget;
    struct Gadget *system_labels_gadget;
    struct Gadget *labels_gadget;
    struct Gadget *labels_scroller;
    struct Gadget *messages_gadget;
    struct Gadget *messages_scroller;
    struct Gadget *preview_gadget;
    struct Gadget *preview_scroller;
    struct Gadget *save_attachments_gadget;
    struct Image label_show_image;
    struct Image label_hide_image;
    UWORD *label_show_image_data;
    UWORD *label_hide_image_data;
    ULONG label_image_data_bytes;
    struct Hook preview_url_hook;
    struct Hook label_tree_render_hook;
    struct Hook system_label_render_hook;
    struct Hook message_flag_render_hook;
    UWORD label_tree_hook_height;
    UWORD list_row_hook_height;
    struct Gadget *status_gadget;
    struct ColumnInfo *columns;
    struct List system_labels_list;
    struct List labels_list;
    struct List messages_list;
    GuiLabel labels[AMIGMAIL_MAX_LABELS];
    size_t label_count;
    char current_mailbox_utf8[512];
    char current_label_local[512];
    unsigned long active_message_uid;
    unsigned long move_uid;
    char move_source_mailbox_utf8[512];
    int move_pending;
    char reply_to_local[768];
    char reply_subject_local[512];
    char reply_body_local[GUI_REPLY_BODY_MAX];
    char reply_in_reply_to_utf8[512];
    char reply_references_utf8[1024];
    ULONG preview_line_count;
    unsigned char *current_message_payload;
    size_t current_message_payload_length;
    size_t current_attachment_count;
    struct Screen *screen;
    LONG banner_pens[BANNER_COLOR_COUNT];
    unsigned char banner_pen_owned[BANNER_COLOR_COUNT];
    LONG unread_pen;
    unsigned char unread_pen_owned;
    LONG text_pen;
    struct MsgPort *periodic_timer_port;
    struct timerequest *periodic_timer_request;
    int periodic_timer_device_open;
    int periodic_timer_pending;
    int periodic_check_pending;
    unsigned long inbox_latest_uid;
    int inbox_baseline_ready;
    ULONG message_click_seconds;
    ULONG message_click_micros;
    ULONG message_click_uid;
    int message_click_valid;
    int running;
};

static void load_label_expansion_state(AmgGui *gui);
static void save_label_expansion_state(const AmgGui *gui);
static void apply_label_expansion_state(AmgGui *gui);
static int periodic_timer_init(AmgGui *gui);
static void periodic_timer_restart(AmgGui *gui);
static void periodic_timer_cleanup(AmgGui *gui);
static ULONG periodic_timer_signal_mask(const AmgGui *gui);
static void periodic_fetch_mail(AmgGui *gui, AmgError *error);
static void set_message_selected_visual(AmgGui *gui, ULONG uid);
static void sync_messages_scroller(AmgGui *gui);
static void sync_labels_scroller(AmgGui *gui);
static struct Node *one_column_node(const char *text, ULONG user_data);
static int confirm_question_dialog_for_window(AmgGui *gui,
                                              struct Window *ref_window,
                                              const char *question,
                                              const char *note, LONG width);

/* LAYOUT_FillPattern uses the classic two-row, 16-bit area pattern. */
static UWORD solid_fill_pattern[2] = {0xffffU, 0xffffU};
/* In den Nachrichtenzeilen zeigt AmigaOS-Zeichen 0xb7 die gesetzte
 * Sternmarkierung als mittigen Punkt an. Die Spaltenueberschrift selbst ist
 * ein schlichtes zentriertes Rufzeichen. */
static const char message_flag_marker[] = { (char)0xb7, '\0' };

/* ReAction-Beschriftung ohne Eingabefeld-Rahmen. button.gadget kann mit
 * BVS_NONE + transparentem Hintergrund als sauberer statischer Text dienen;
 * BCJ_LEFT haelt die Beschriftungen linksbuendig. */
static Object *static_text_label(const char *text)
{
    return NewObject(NULL, (CONST_STRPTR)"button.gadget",
                     GA_ReadOnly, TRUE,
                     GA_Text, (ULONG)(uintptr_t)(text ? text : ""),
                     BUTTON_BevelStyle, BVS_NONE,
                     BUTTON_Transparent, TRUE,
                     BUTTON_Justification, BCJ_LEFT,
                     TAG_DONE);
}

static struct NewMenu menus_de[] = {
    {NM_TITLE, (STRPTR)"Datei", NULL, 0, 0, NULL},
    {NM_ITEM, (STRPTR)"Konto-Einstellungen...", (STRPTR)"E", 0, 0, NULL},
    {NM_ITEM, (STRPTR)"\334ber AmiGmail...", NULL, 0, 0, NULL},
    {NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL},
    {NM_ITEM, (STRPTR)"Beenden", (STRPTR)"Q", 0, 0, NULL},
    {NM_TITLE, (STRPTR)"Bearbeiten", NULL, 0, 0, NULL},
    {NM_ITEM, (STRPTR)"Papierkorb leeren...", NULL, 0, 0, NULL},
    {NM_ITEM, (STRPTR)"Spam leeren...", NULL, 0, 0, NULL},
    {NM_END, NULL, NULL, 0, 0, NULL}
};

static struct NewMenu menus_en[] = {
    {NM_TITLE, (STRPTR)"File", NULL, 0, 0, NULL},
    {NM_ITEM, (STRPTR)"Account settings...", (STRPTR)"E", 0, 0, NULL},
    {NM_ITEM, (STRPTR)"About AmiGmail...", NULL, 0, 0, NULL},
    {NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL},
    {NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, NULL},
    {NM_TITLE, (STRPTR)"Edit", NULL, 0, 0, NULL},
    {NM_ITEM, (STRPTR)"Empty Trash...", NULL, 0, 0, NULL},
    {NM_ITEM, (STRPTR)"Empty Spam...", NULL, 0, 0, NULL},
    {NM_END, NULL, NULL, 0, 0, NULL}
};

static int open_classes(void)
{
    WindowBase = OpenLibrary((CONST_STRPTR)"window.class", 44);
    LayoutBase = OpenLibrary((CONST_STRPTR)"gadgets/layout.gadget", 44);
    ButtonBase = OpenLibrary((CONST_STRPTR)"gadgets/button.gadget", 44);
    ListBrowserBase =
        OpenLibrary((CONST_STRPTR)"gadgets/listbrowser.gadget", 44);
    ScrollerBase = OpenLibrary((CONST_STRPTR)"gadgets/scroller.gadget", 44);
    StringBase = OpenLibrary((CONST_STRPTR)"gadgets/string.gadget", 44);
    TextEditorBase =
        OpenLibrary((CONST_STRPTR)"gadgets/texteditor.gadget", 44);
    /* OpenURL ist optional: Ohne Library bleibt AmiGmail voll
     * funktionsfaehig, lediglich das Oeffnen erkannter URLs entfaellt. */
    OpenURLBase = OpenLibrary((CONST_STRPTR)"openurl.library", 0);
    AslBase = OpenLibrary((CONST_STRPTR)"asl.library", 37);
    GfxBase = (struct GfxBase *)
        OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    return WindowBase && LayoutBase && ButtonBase && ListBrowserBase &&
           ScrollerBase && StringBase && TextEditorBase && AslBase && GfxBase;
}

static void close_classes(void)
{
    if (AslBase) CloseLibrary(AslBase);
    if (OpenURLBase) CloseLibrary(OpenURLBase);
    if (TextEditorBase) CloseLibrary(TextEditorBase);
    if (StringBase) CloseLibrary(StringBase);
    if (ScrollerBase) CloseLibrary(ScrollerBase);
    if (ListBrowserBase) CloseLibrary(ListBrowserBase);
    if (ButtonBase) CloseLibrary(ButtonBase);
    if (LayoutBase) CloseLibrary(LayoutBase);
    if (WindowBase) CloseLibrary(WindowBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    AslBase = NULL;
    OpenURLBase = NULL;
    TextEditorBase = NULL;
    GfxBase = NULL;
    StringBase = NULL;
    ScrollerBase = NULL;
    ListBrowserBase = NULL;
    ButtonBase = NULL;
    LayoutBase = NULL;
    WindowBase = NULL;
}


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
    if (OpenDevice(TIMERNAME, UNIT_VBLANK,
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

static void periodic_timer_restart(AmgGui *gui)
{
    if (!gui || !gui->periodic_timer_request) return;
    periodic_timer_disarm(gui);
    periodic_timer_arm(gui);
}

static void periodic_timer_cleanup(AmgGui *gui)
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


/* Kleine klassische Intuition-Images fuer die Label-Hierarchie.
 * Damit sind die [+]/[-]-Symbole nicht von drawlist.image abhaengig und
 * ListBrowser faellt auf keinem System mehr auf die grossen Standardpfeile
 * zurueck. Die Bitmapdaten liegen wie von imageclass erwartet im CHIP-RAM. */
static const UWORD label_plus_pattern[GUI_TREE_IMAGE_HEIGHT] = {
    0xff80U, 0x8080U, 0x8880U, 0xbe80U,
    0x8880U, 0x8080U, 0xff80U
};

static const UWORD label_minus_pattern[GUI_TREE_IMAGE_HEIGHT] = {
    0xff80U, 0x8080U, 0x8080U, 0xbe80U,
    0x8080U, 0x8080U, 0xff80U
};

static int build_label_tree_image(AmgGui *gui, struct Image *image,
                                  UWORD **storage,
                                  const UWORD *pattern,
                                  ULONG *allocated_bytes)
{
    struct DrawInfo *draw_info;
    ULONG foreground, background, depth_mask;
    ULONG plane_bytes, total_bytes;
    UBYTE screen_depth, bit, plane_count, plane_index;
    UBYTE plane_pick, plane_on_off;
    UWORD visible_mask;
    UWORD *data;
    ULONG y;

    if (!gui || !gui->screen || !image || !storage || !pattern ||
        !allocated_bytes)
        return 0;

    foreground = gui->text_pen >= 0 ? (ULONG)gui->text_pen : 1UL;
    background = 0UL;
    draw_info = GetScreenDrawInfo(gui->screen);
    if (draw_info) {
        background = (ULONG)draw_info->dri_Pens[BACKGROUNDPEN];
        if (foreground == background)
            foreground = (ULONG)draw_info->dri_Pens[SHADOWPEN];
        FreeScreenDrawInfo(gui->screen, draw_info);
    }
    if (foreground == background)
        foreground = background ? 0UL : 1UL;

    screen_depth = gui->screen->RastPort.BitMap ?
        gui->screen->RastPort.BitMap->Depth : 2U;
    if (screen_depth == 0U) screen_depth = 1U;
    if (screen_depth > 8U) screen_depth = 8U;
    depth_mask = (1UL << screen_depth) - 1UL;
    foreground &= depth_mask;
    background &= depth_mask;

    plane_pick = 0U;
    plane_on_off = 0U;
    plane_count = 0U;
    for (bit = 0U; bit < screen_depth; ++bit) {
        ULONG mask = 1UL << bit;
        if ((foreground & mask) != (background & mask)) {
            plane_pick |= (UBYTE)mask;
            ++plane_count;
        } else if (background & mask) {
            plane_on_off |= (UBYTE)mask;
        }
    }
    if (plane_count == 0U) return 0;

    plane_bytes = (ULONG)GUI_TREE_IMAGE_HEIGHT * (ULONG)sizeof(UWORD);
    total_bytes = plane_bytes * (ULONG)plane_count;
    data = (UWORD *)AllocMem(total_bytes, MEMF_CHIP | MEMF_CLEAR);
    if (!data) return 0;

    visible_mask =
        (UWORD)(0xffffU << (16U - (unsigned)GUI_TREE_IMAGE_WIDTH));
    plane_index = 0U;
    for (bit = 0U; bit < screen_depth; ++bit) {
        ULONG mask = 1UL << bit;
        if ((foreground & mask) != (background & mask)) {
            int foreground_bit = (foreground & mask) != 0UL;
            for (y = 0UL; y < (ULONG)GUI_TREE_IMAGE_HEIGHT; ++y) {
                UWORD value = pattern[y] & visible_mask;
                if (!foreground_bit)
                    value = (UWORD)((~value) & visible_mask);
                data[(ULONG)plane_index * GUI_TREE_IMAGE_HEIGHT + y] = value;
            }
            ++plane_index;
        }
    }

    memset(image, 0, sizeof(*image));
    image->LeftEdge = 0;
    image->TopEdge = -1;
    image->Width = GUI_TREE_IMAGE_WIDTH;
    image->Height = GUI_TREE_IMAGE_HEIGHT;
    image->Depth = plane_count;
    image->ImageData = data;
    image->PlanePick = plane_pick;
    image->PlaneOnOff = plane_on_off;
    image->NextImage = NULL;

    *storage = data;
    *allocated_bytes = total_bytes;
    return 1;
}

static int create_label_tree_images(AmgGui *gui)
{
    ULONG show_bytes = 0UL, hide_bytes = 0UL;
    if (!gui) return 0;

    memset(&gui->label_show_image, 0, sizeof(gui->label_show_image));
    memset(&gui->label_hide_image, 0, sizeof(gui->label_hide_image));
    gui->label_show_image_data = NULL;
    gui->label_hide_image_data = NULL;
    gui->label_image_data_bytes = 0UL;

    if (!build_label_tree_image(gui, &gui->label_show_image,
                                &gui->label_show_image_data,
                                label_plus_pattern, &show_bytes))
        return 0;
    if (!build_label_tree_image(gui, &gui->label_hide_image,
                                &gui->label_hide_image_data,
                                label_minus_pattern, &hide_bytes)) {
        FreeMem(gui->label_show_image_data, show_bytes);
        gui->label_show_image_data = NULL;
        memset(&gui->label_show_image, 0, sizeof(gui->label_show_image));
        return 0;
    }

    if (show_bytes != hide_bytes) {
        FreeMem(gui->label_show_image_data, show_bytes);
        FreeMem(gui->label_hide_image_data, hide_bytes);
        gui->label_show_image_data = NULL;
        gui->label_hide_image_data = NULL;
        memset(&gui->label_show_image, 0, sizeof(gui->label_show_image));
        memset(&gui->label_hide_image, 0, sizeof(gui->label_hide_image));
        return 0;
    }
    gui->label_image_data_bytes = show_bytes;

    return 1;
}

static void dispose_label_tree_images(AmgGui *gui)
{
    if (!gui) return;
    if (gui->label_show_image_data && gui->label_image_data_bytes)
        FreeMem(gui->label_show_image_data, gui->label_image_data_bytes);
    if (gui->label_hide_image_data && gui->label_image_data_bytes)
        FreeMem(gui->label_hide_image_data, gui->label_image_data_bytes);
    gui->label_show_image_data = NULL;
    gui->label_hide_image_data = NULL;
    gui->label_image_data_bytes = 0UL;
    memset(&gui->label_show_image, 0, sizeof(gui->label_show_image));
    memset(&gui->label_hide_image, 0, sizeof(gui->label_hide_image));
}

static void draw_tree_dotted_vertical(struct RastPort *rp, LONG x,
                                      LONG y1, LONG y2)
{
    LONG y;
    if (!rp) return;
    if (y2 < y1) {
        LONG swap = y1;
        y1 = y2;
        y2 = swap;
    }
    for (y = y1; y <= y2; y += GUI_TREE_DOT_STEP)
        WritePixel(rp, x, y);
}

static void draw_tree_dotted_horizontal(struct RastPort *rp, LONG x1,
                                        LONG x2, LONG y)
{
    LONG x;
    if (!rp) return;
    if (x2 < x1) {
        LONG swap = x1;
        x1 = x2;
        x2 = swap;
    }
    for (x = x1; x <= x2; x += GUI_TREE_DOT_STEP)
        WritePixel(rp, x, y);
}

static ULONG label_tree_render_subentry(struct Hook *hook,
                                        struct Node *node, APTR message)
{
    AmgGui *gui = hook ? (AmgGui *)hook->h_Data : NULL;
    struct LBDrawMsg *draw = (struct LBDrawMsg *)message;
    struct RastPort *rp;
    ULONG label_index = (ULONG)~0UL;
    const GuiLabel *label;
    LONG row_top, row_bottom, row_center, text_x, text_y;
    LONG current_slot_x, current_glyph_center;
    LONG line_pen, text_pen;
    UBYTE old_fg, old_mode;
    size_t child_index, parent_index;
    unsigned level_up = 1U;

    if (!gui || !node || !draw || draw->lbdm_MethodID != LB_DRAW)
        return LBCB_UNKNOWN;

    GetListBrowserNodeAttrs(
        node, LBNA_UserData, (ULONG)(uintptr_t)&label_index, TAG_DONE);
    if (label_index >= gui->label_count)
        return LBCB_OK;

    label = &gui->labels[label_index];
    rp = draw->lbdm_RastPort;
    if (!rp) return LBCB_OK;

    row_top = draw->lbdm_Bounds.MinY;
    row_bottom = draw->lbdm_Bounds.MaxY;
    row_center = row_top + (row_bottom - row_top) / 2L;

    /* In hierarchical mode ListBrowser places the custom show/hide image
     * immediately before the column renderer. Leaf nodes have no image, so
     * they retain the one-space optical compensation used by the previous
     * native-text implementation. */
    current_slot_x = draw->lbdm_Bounds.MinX;
    if (label->has_children)
        current_slot_x -= GUI_TREE_IMAGE_WIDTH + GUI_TREE_IMAGE_TRAILING_GAP;
    current_glyph_center = current_slot_x +
        (GUI_TREE_IMAGE_WIDTH - 1L) / 2L - GUI_TREE_AXIS_LEFT_SHIFT;

    old_fg = rp->FgPen;
    old_mode = rp->DrawMode;
    SetDrMd(rp, JAM1);

    if (draw->lbdm_DrawInfo) {
        line_pen = draw->lbdm_State == LBR_SELECTED
            ? draw->lbdm_DrawInfo->dri_Pens[FILLTEXTPEN]
            : draw->lbdm_DrawInfo->dri_Pens[SHADOWPEN];
        text_pen = draw->lbdm_State == LBR_SELECTED
            ? draw->lbdm_DrawInfo->dri_Pens[FILLTEXTPEN]
            : draw->lbdm_DrawInfo->dri_Pens[TEXTPEN];
    } else {
        line_pen = gui->text_pen >= 0 ? gui->text_pen : old_fg;
        text_pen = gui->text_pen >= 0 ? gui->text_pen : old_fg;
    }

    SetAPen(rp, (ULONG)line_pen);

    /* Classic tree continuation: for the direct parent the vertical line
     * always reaches the current branch. It continues through the complete
     * row only when another sibling follows. For higher ancestors a line is
     * needed only while the child on the current path has a later sibling. */
    child_index = (size_t)label_index;
    parent_index = label->parent_index;
    while (parent_index != (size_t)-1 && parent_index < gui->label_count) {
        LONG ancestor_x = current_slot_x -
            (LONG)level_up * GUI_TREE_LEVEL_WIDTH +
            GUI_TREE_IMAGE_WIDTH / 2L - GUI_TREE_AXIS_LEFT_SHIFT;
        int continues = gui->labels[child_index].has_next_sibling;

        if (level_up == 1U) {
            draw_tree_dotted_vertical(
                rp, ancestor_x, row_top,
                continues ? row_bottom : row_center);
        } else if (continues) {
            draw_tree_dotted_vertical(rp, ancestor_x, row_top, row_bottom);
        }

        child_index = parent_index;
        parent_index = gui->labels[parent_index].parent_index;
        ++level_up;
    }

    if (label->generation > 1U && label->parent_index != (size_t)-1) {
        LONG parent_x = current_slot_x - GUI_TREE_LEVEL_WIDTH +
                        GUI_TREE_IMAGE_WIDTH / 2L -
                        GUI_TREE_AXIS_LEFT_SHIFT;
        LONG branch_end;
        if (label->has_children) {
            /* current_slot_x entspricht auf den getesteten klassischen
             * ListBrowser-Versionen der horizontalen Boxmitte. Der Ast soll
             * kurz vor der linken Boxkante enden und nicht bis in die Box
             * hinein verlaengert werden. */
            branch_end = current_slot_x -
                GUI_TREE_IMAGE_WIDTH / 2L - 2L;
        } else {
            LONG space_width = TextLength(rp, (CONST_STRPTR)" ", 1);
            branch_end = draw->lbdm_Bounds.MinX + space_width - 2L -
                         GUI_TREE_CHILD_LEFT_SHIFT;
        }
        if (branch_end >= parent_x)
            draw_tree_dotted_horizontal(rp, parent_x, branch_end, row_center);
    }

    /* An expanded branch starts its own vertical continuation immediately
     * below the [-] box. Hidden children therefore never leave stray lines. */
    if (label->has_children && label->expanded) {
        LONG glyph_bottom = row_center + GUI_TREE_IMAGE_HEIGHT / 2L;
        /* Leave a clean one-pixel gap below the [-] box.  On classic
         * ListBrowser versions the first dotted trunk pixel otherwise
         * touches the lower-left edge of the hierarchy image and looks like
         * a stray bitmap pixel. */
        if (glyph_bottom + 2L < row_bottom)
            draw_tree_dotted_vertical(rp, current_glyph_center,
                                      glyph_bottom + 3L, row_bottom);
    }

    SetAPen(rp, (ULONG)text_pen);
    text_x = draw->lbdm_Bounds.MinX;
    if (!label->has_children) {
        text_x += TextLength(rp, (CONST_STRPTR)" ", 1);
        /* Untergeordnete Blatt-Labels ruecken mit dem Ast nach links. So
         * bleibt die Querlinie kurz und der Text steht optisch unter dem
         * darueberliegenden Hauptordner, statt weit nach rechts zu wandern. */
        if (label->generation > 1U)
            text_x -= GUI_TREE_CHILD_LEFT_SHIFT;
    }
    text_y = row_top +
        ((row_bottom - row_top + 1L - (LONG)rp->TxHeight) / 2L) +
        (LONG)rp->TxBaseline + 1L;
    Move(rp, text_x, text_y);
    Text(rp, (CONST_STRPTR)label->display_local,
         (ULONG)strlen(label->display_local));

    SetAPen(rp, old_fg);
    SetDrMd(rp, old_mode);
    return LBCB_OK;
}

static void init_label_tree_render_hook(AmgGui *gui)
{
    if (!gui) return;
    memset(&gui->label_tree_render_hook, 0,
           sizeof(gui->label_tree_render_hook));
    gui->label_tree_render_hook.h_Entry = (HOOKFUNC)HookEntry;
    gui->label_tree_render_hook.h_SubEntry =
        (HOOKFUNC)label_tree_render_subentry;
    gui->label_tree_render_hook.h_Data = gui;
    gui->label_tree_hook_height = gui->screen && gui->screen->RastPort.TxHeight
        ? (UWORD)(gui->screen->RastPort.TxHeight + 2U) : 10U;
}

static ULONG compact_text_render_subentry(struct Hook *hook,
                                          struct Node *node, APTR message)
{
    struct LBDrawMsg *draw = (struct LBDrawMsg *)message;
    struct RastPort *rp;
    ULONG text_value = 0UL, flags = 0UL, fg_pen = 0UL;
    const char *text;
    LONG text_x, text_y, width;
    UBYTE old_fg, old_mode;

    if (!hook || !node || !draw || draw->lbdm_MethodID != LB_DRAW)
        return LBCB_UNKNOWN;
    rp = draw->lbdm_RastPort;
    if (!rp) return LBCB_OK;

    GetListBrowserNodeAttrs(
        node,
        LBNA_Flags, (ULONG)(uintptr_t)&flags,
        LBNA_Column, 0,
        LBNCA_Text, (ULONG)(uintptr_t)&text_value,
        LBNCA_FGPen, (ULONG)(uintptr_t)&fg_pen,
        TAG_DONE);
    text = (const char *)(uintptr_t)text_value;
    if (!text) text = "";

    old_fg = rp->FgPen;
    old_mode = rp->DrawMode;
    SetDrMd(rp, JAM1);
    if (draw->lbdm_DrawInfo) {
        if (draw->lbdm_State == LBR_SELECTED)
            SetAPen(rp, draw->lbdm_DrawInfo->dri_Pens[FILLTEXTPEN]);
        else if (flags & LBFLG_CUSTOMPENS)
            SetAPen(rp, fg_pen);
        else
            SetAPen(rp, draw->lbdm_DrawInfo->dri_Pens[TEXTPEN]);
    }

    text_x = draw->lbdm_Bounds.MinX;
    text_y = draw->lbdm_Bounds.MinY +
        ((draw->lbdm_Bounds.MaxY - draw->lbdm_Bounds.MinY + 1L -
          (LONG)rp->TxHeight) / 2L) + (LONG)rp->TxBaseline + 1L;

    /* h_Data != NULL is used for the narrow flag column, which is centred. */
    if (hook->h_Data) {
        width = TextLength(rp, (CONST_STRPTR)text, (ULONG)strlen(text));
        text_x += ((draw->lbdm_Bounds.MaxX - draw->lbdm_Bounds.MinX + 1L) -
                   width) / 2L;
    }
    Move(rp, text_x, text_y);
    Text(rp, (CONST_STRPTR)text, (ULONG)strlen(text));

    SetAPen(rp, old_fg);
    SetDrMd(rp, old_mode);
    return LBCB_OK;
}

static void init_compact_list_render_hooks(AmgGui *gui)
{
    if (!gui) return;
    gui->list_row_hook_height = gui->screen && gui->screen->RastPort.TxHeight
        ? (UWORD)(gui->screen->RastPort.TxHeight + 2U) : 10U;

    memset(&gui->system_label_render_hook, 0,
           sizeof(gui->system_label_render_hook));
    gui->system_label_render_hook.h_Entry = (HOOKFUNC)HookEntry;
    gui->system_label_render_hook.h_SubEntry =
        (HOOKFUNC)compact_text_render_subentry;
    gui->system_label_render_hook.h_Data = NULL;

    memset(&gui->message_flag_render_hook, 0,
           sizeof(gui->message_flag_render_hook));
    gui->message_flag_render_hook.h_Entry = (HOOKFUNC)HookEntry;
    gui->message_flag_render_hook.h_SubEntry =
        (HOOKFUNC)compact_text_render_subentry;
    gui->message_flag_render_hook.h_Data = gui; /* centre flag column */
}

static struct Node *system_label_node(AmgGui *gui,
                                      const char *text, ULONG user_data)
{
    if (gui && gui->system_label_render_hook.h_Entry &&
        gui->list_row_hook_height) {
        return AllocListBrowserNode(
            1,
            LBNA_UserData, user_data,
            LBNA_Column, 0,
            LBNCA_CopyText, TRUE,
            LBNCA_Text, (ULONG)(uintptr_t)text,
            LBNCA_RenderHook, &gui->system_label_render_hook,
            LBNCA_HookHeight, gui->list_row_hook_height,
            TAG_DONE);
    }
    return one_column_node(text, user_data);
}

static struct Node *one_column_node(const char *text, ULONG user_data)
{
    return AllocListBrowserNode(
        1,
        LBNA_UserData, user_data,
        LBNA_Column, 0,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)(uintptr_t)text,
        LBNCA_VertJustify, LRJ_BOTTOM,
        TAG_DONE);
}

static struct Node *hierarchical_label_node(AmgGui *gui,
                                            const GuiLabel *label,
                                            ULONG user_data)
{
    ULONG flags = label && label->has_children ? LBFLG_HASCHILDREN : 0UL;

    if (gui && gui->label_tree_render_hook.h_Entry &&
        gui->label_tree_hook_height) {
        return AllocListBrowserNode(
            1,
            LBNA_UserData, user_data,
            LBNA_Generation, label ? label->generation : 1U,
            LBNA_Flags, flags,
            LBNA_Column, 0,
            LBNCA_RenderHook, &gui->label_tree_render_hook,
            LBNCA_HookHeight, gui->label_tree_hook_height,
            TAG_DONE);
    }

    /* Fallback vor Initialisierung des Workbench-Screens. Diese Nodes werden
     * in create_window() nach Initialisierung des RenderHooks neu aufgebaut. */
    {
        char display[160];
        const char *text = label ? label->display_local : "";
        if (label && !label->has_children) {
            snprintf(display, sizeof(display), " %s", text);
            text = display;
        }
        return AllocListBrowserNode(
            1,
            LBNA_UserData, user_data,
            LBNA_Generation, label ? label->generation : 1U,
            LBNA_Flags, flags,
            LBNA_Column, 0,
            LBNCA_CopyText, TRUE,
            LBNCA_Text, (ULONG)(uintptr_t)text,
            TAG_DONE);
    }
}

static struct Node *message_node(AmgGui *gui,
                                 const char *from, const char *subject,
                                 const char *date, unsigned long size_bytes,
                                 ULONG uid, int seen, int flagged,
                                 LONG unread_pen, LONG text_pen)
{
    char size[32];
    unsigned long size_kb = size_bytes / 1024UL +
                            (size_bytes % 1024UL ? 1UL : 0UL);
    snprintf(size, sizeof(size), "%lu KB", size_kb);
    return AllocListBrowserNode(
        5,
        LBNA_UserData, uid,
        LBNA_Flags, seen ? 0UL : LBFLG_CUSTOMPENS,
        LBNA_Column, 0,
        LBNCA_CopyText, TRUE,
        LBNCA_Text,
            (ULONG)(uintptr_t)(flagged ? message_flag_marker : ""),
        LBNCA_HorizJustify, LCJ_CENTRE,
        LBNCA_FGPen, (ULONG)text_pen,
        LBNCA_RenderHook,
            gui ? &gui->message_flag_render_hook : NULL,
        LBNCA_HookHeight,
            gui && gui->list_row_hook_height ? gui->list_row_hook_height : 10U,
        LBNA_Column, 1,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)(uintptr_t)from,
        LBNCA_FGPen, (ULONG)unread_pen,
        LBNCA_VertJustify, LRJ_CENTER,
        LBNA_Column, 2,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)(uintptr_t)subject,
        LBNCA_FGPen, (ULONG)unread_pen,
        LBNCA_VertJustify, LRJ_CENTER,
        LBNA_Column, 3,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)(uintptr_t)date,
        LBNCA_FGPen, (ULONG)unread_pen,
        LBNCA_VertJustify, LRJ_CENTER,
        LBNA_Column, 4,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)(uintptr_t)size,
        LBNCA_FGPen, (ULONG)unread_pen,
        LBNCA_VertJustify, LRJ_CENTER,
        TAG_DONE);
}

static void insert_message_node_date_desc(struct List *list,
                                          struct Node *node,
                                          const char *date, ULONG uid)
{
    struct Node *current, *previous = NULL;
    const char *new_date = date ? date : "";
    if (!list || !node) return;

    current = list->lh_Head;
    while (current && current->ln_Succ) {
        STRPTR current_date = NULL;
        ULONG current_uid = 0;
        int comparison;
        GetListBrowserNodeAttrs(
            current,
            LBNA_UserData, (ULONG)(uintptr_t)&current_uid,
            LBNA_Column, 3,
            LBNCA_Text, (ULONG)(uintptr_t)&current_date,
            TAG_DONE);
        comparison = strcmp(new_date, current_date ? (const char *)current_date : "");
        if (comparison > 0 || (comparison == 0 && uid > current_uid))
            break;
        previous = current;
        current = current->ln_Succ;
    }
    Insert(list, node, previous);
}

static struct Node *message_placeholder_node(const char *text)
{
    char wrapped[256];
    const char *source = text ? text : "";
    size_t length = strlen(source);
    size_t i, split = 0;

    if (length >= sizeof(wrapped)) length = sizeof(wrapped) - 1U;
    memcpy(wrapped, source, length);
    wrapped[length] = 0;
    if (!strchr(wrapped, '\n')) {
        size_t middle = length / 2U;
        size_t distance = length + 1U;
        for (i = 0; i < length; ++i) {
            size_t current_distance;
            if (wrapped[i] != ' ') continue;
            current_distance = i > middle ? i - middle : middle - i;
            if (current_distance < distance) {
                split = i;
                distance = current_distance;
            }
        }
        if (split) wrapped[split] = '\n';
    }
    return AllocListBrowserNode(
        5,
        LBNA_UserData, 0,
        LBNA_Column, 0,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)(uintptr_t)"",
        LBNA_Column, 1,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)(uintptr_t)"",
        LBNA_Column, 2,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)(uintptr_t)wrapped,
        LBNCA_Justification, LCJ_CENTRE,
        LBNCA_VertJustify, LRJ_CENTER,
        LBNCA_WordWrap, TRUE,
        LBNA_Column, 3,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)(uintptr_t)"",
        LBNA_Column, 4,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)(uintptr_t)"",
        TAG_DONE);
}

static const char *string_text(struct Gadget *gadget)
{
    ULONG value = 0;
    if (gadget) GetAttr(STRINGA_TextVal, (Object *)gadget, &value);
    return value ? (const char *)(uintptr_t)value : "";
}

static void set_string(struct Gadget *gadget, struct Window *window,
                       const char *text)
{
    if (gadget)
        SetGadgetAttrs(gadget, window, NULL,
                       STRINGA_TextVal, (ULONG)(uintptr_t)(text ? text : ""),
                       TAG_DONE);
}

static void status_local(AmgGui *gui, const char *text)
{
    if (gui && gui->status_gadget)
        set_string(gui->status_gadget, gui->window, text);
}

static void set_utf8_string(struct Gadget *gadget, struct Window *window,
                            const char *utf8)
{
    AmgBuffer local;
    amg_buffer_init(&local);
    if (utf8 && amg_utf8_to_local(utf8, &local) == AMG_OK &&
        amg_buffer_terminate(&local) == AMG_OK)
        set_string(gadget, window, (const char *)local.data);
    else
        set_string(gadget, window, T("Fehler", "Error"));
    amg_buffer_free(&local);
}

static void status_utf8(AmgGui *gui, const char *utf8)
{
    if (gui) set_utf8_string(gui->status_gadget, gui->window, utf8);
}


static int ascii_prefix_ci(const char *text, const char *prefix)
{
    unsigned char a, b;
    if (!text || !prefix) return 0;
    while (*prefix) {
        if (!*text) return 0;
        a = (unsigned char)*text++;
        b = (unsigned char)*prefix++;
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
        if (a != b) return 0;
    }
    return 1;
}

static int url_prefix_length(const char *text)
{
    if (ascii_prefix_ci(text, "https://")) return 8;
    if (ascii_prefix_ci(text, "http://")) return 7;
    if (ascii_prefix_ci(text, "ftp://")) return 6;
    if (ascii_prefix_ci(text, "mailto:")) return 7;
    if (ascii_prefix_ci(text, "www.")) return 4;
    return 0;
}

static int url_boundary_before(const char *text, size_t pos)
{
    unsigned char c;
    if (!text || pos == 0) return 1;
    c = (unsigned char)text[pos - 1U];
    return c <= ' ' || c == '<' || c == '(' || c == '[' || c == '{' ||
           c == '"' || c == '\'';
}

static size_t url_token_end(const char *text, size_t start)
{
    size_t end = start;
    unsigned char c;
    while (text && text[end]) {
        c = (unsigned char)text[end];
        if (c <= ' ' || c == '<' || c == '>' || c == '"' || c == '\'')
            break;
        ++end;
    }
    while (end > start) {
        c = (unsigned char)text[end - 1U];
        if (c == '.' || c == ',' || c == ';' || c == '!' ||
            c == ')' || c == ']' || c == '}')
            --end;
        else
            break;
    }
    return end;
}

static int decorate_preview_links(const char *text, AmgBuffer *styled)
{
    size_t pos = 0, end;
    int result = AMG_OK;
    if (!styled) return AMG_ERR_ARGUMENT;
    if (!text) text = "";
    while (text[pos] && result == AMG_OK) {
        if (url_boundary_before(text, pos) && url_prefix_length(text + pos)) {
            end = url_token_end(text, pos);
            if (end > pos) {
                const unsigned char underline[2] = {0x1bU, 'u'};
                const unsigned char normal[2] = {0x1bU, 'n'};
                result = amg_buffer_append(styled, underline, sizeof(underline));
                if (result == AMG_OK)
                    result = amg_buffer_append(
                        styled, (const unsigned char *)text + pos, end - pos);
                if (result == AMG_OK)
                    result = amg_buffer_append(styled, normal, sizeof(normal));
                pos = end;
                continue;
            }
        }
        result = amg_buffer_append_char(styled, (unsigned char)text[pos++]);
    }
    return result;
}

static int extract_clicked_url(const struct ClickMessage *clickmsg,
                               char output[GUI_URL_MAX])
{
    const char *line;
    size_t length, pos, start, end, used;
    if (!clickmsg || !clickmsg->LineContents || !output) return 0;
    line = (const char *)clickmsg->LineContents;
    length = strlen(line);
    pos = (size_t)clickmsg->ClickPosition;
    if (pos > length) pos = length;
    if (pos == length && pos) --pos;

    start = pos;
    while (start > 0U) {
        unsigned char c = (unsigned char)line[start - 1U];
        if (c <= ' ' || c == '<' || c == '>' || c == '"' || c == '\'')
            break;
        --start;
    }
    while (start < length &&
           (line[start] == '(' || line[start] == '[' || line[start] == '{'))
        ++start;

    end = url_token_end(line, start);
    if (end <= start || !url_prefix_length(line + start)) return 0;

    used = end - start;
    if (ascii_prefix_ci(line + start, "www.")) {
        static const char prefix[] = "http://";
        if (sizeof(prefix) - 1U + used + 1U > GUI_URL_MAX) return 0;
        memcpy(output, prefix, sizeof(prefix) - 1U);
        memcpy(output + sizeof(prefix) - 1U, line + start, used);
        output[sizeof(prefix) - 1U + used] = 0;
    } else {
        if (used + 1U > GUI_URL_MAX) return 0;
        memcpy(output, line + start, used);
        output[used] = 0;
    }
    return 1;
}

static ULONG preview_url_doubleclick_subentry(struct Hook *hook,
                                               Object *object,
                                               APTR message)
{
    AmgGui *gui = hook ? (AmgGui *)hook->h_Data : NULL;
    struct ClickMessage *clickmsg = (struct ClickMessage *)message;
    struct TagItem tags[1];
    char url[GUI_URL_MAX];
    (void)object;

    if (!gui || !extract_clicked_url(clickmsg, url))
        return FALSE;

    /* Der TextEditor ruft diesen Hook explizit fuer die Aktion auf einem
     * doppelt angeklickten Wort auf. URL_OpenA() darf hier direkt laufen:
     * anders als der fruehere DOS/System()-Start blockiert es nicht auf
     * einen weiteren IDCMP-Event. Dadurch oeffnet sich der Browser sofort
     * nach dem Doppelklick und nicht erst beim naechsten Klick im Fenster. */
    if (!OpenURLBase)
        return FALSE;

    tags[0].ti_Tag = TAG_END;
    tags[0].ti_Data = 0;
    (void)AMG_URL_OpenA((STRPTR)url, tags);
    return TRUE;
}

static void init_preview_url_hook(AmgGui *gui)
{
    if (!gui) return;
    memset(&gui->preview_url_hook, 0, sizeof(gui->preview_url_hook));
    gui->preview_url_hook.h_Entry = (HOOKFUNC)HookEntry;
    gui->preview_url_hook.h_SubEntry =
        (HOOKFUNC)preview_url_doubleclick_subentry;
    gui->preview_url_hook.h_Data = gui;
}

static int local_to_utf8(const char *local, char *utf8, size_t capacity)
{
    const unsigned char *source = (const unsigned char *)(local ? local : "");
    size_t used = 0;
    while (*source) {
        if (*source < 0x80U) {
            if (used + 1U >= capacity) return AMG_ERR_LIMIT;
            utf8[used++] = (char)*source;
        } else {
            if (used + 2U >= capacity) return AMG_ERR_LIMIT;
            utf8[used++] = (char)(0xC0U | (*source >> 6));
            utf8[used++] = (char)(0x80U | (*source & 0x3FU));
        }
        ++source;
    }
    utf8[used] = 0;
    return AMG_OK;
}

static void utf8_to_local_copy(const char *utf8, char *local, size_t capacity)
{
    AmgBuffer converted;
    size_t length;
    if (!local || !capacity) return;
    local[0] = 0;
    amg_buffer_init(&converted);
    if (!utf8 || amg_utf8_to_local(utf8, &converted) != AMG_OK ||
        amg_buffer_terminate(&converted) != AMG_OK) {
        amg_buffer_free(&converted);
        return;
    }
    length = converted.length;
    if (length >= capacity) length = capacity - 1U;
    memcpy(local, converted.data, length);
    local[length] = 0;
    amg_buffer_free(&converted);
}

static void header_to_local(const char *header, const char *fallback,
                            char *local, size_t capacity)
{
    AmgBuffer decoded;
    amg_buffer_init(&decoded);
    if (header && *header && amg_rfc2047_decode(header, &decoded) == AMG_OK &&
        amg_buffer_terminate(&decoded) == AMG_OK)
        utf8_to_local_copy((const char *)decoded.data, local, capacity);
    else
        utf8_to_local_copy(fallback ? fallback : "", local, capacity);
    amg_buffer_free(&decoded);
}

static void trim_local_text(char *text)
{
    char *start, *end;
    size_t length;
    if (!text) return;
    start = text;
    while (*start == ' ' || *start == '\t') ++start;
    if (start != text) memmove(text, start, strlen(start) + 1U);
    length = strlen(text);
    end = text + length;
    while (end > text && (end[-1] == ' ' || end[-1] == '\t')) --end;
    *end = 0;
}

static void sender_name_only(char *sender, size_t capacity)
{
    char name[513];
    char *angle, *close_angle, *open_parenthesis;
    size_t length;
    if (!sender || !capacity) return;
    angle = strchr(sender, '<');
    if (angle) {
        length = (size_t)(angle - sender);
        if (length >= sizeof(name)) length = sizeof(name) - 1U;
        memcpy(name, sender, length);
        name[length] = 0;
        trim_local_text(name);
        length = strlen(name);
        if (length >= 2U && name[0] == '"' && name[length - 1U] == '"') {
            memmove(name, name + 1, length - 2U);
            name[length - 2U] = 0;
            trim_local_text(name);
        }
        if (name[0]) {
            strncpy(sender, name, capacity - 1U);
            sender[capacity - 1U] = 0;
            return;
        }
        close_angle = strchr(angle + 1, '>');
        if (close_angle) {
            length = (size_t)(close_angle - (angle + 1));
            if (length >= sizeof(name)) length = sizeof(name) - 1U;
            memcpy(name, angle + 1, length);
            name[length] = 0;
            trim_local_text(name);
            if (name[0]) {
                strncpy(sender, name, capacity - 1U);
                sender[capacity - 1U] = 0;
                return;
            }
        }
    }
    length = strlen(sender);
    open_parenthesis = strrchr(sender, '(');
    if (open_parenthesis && length && sender[length - 1U] == ')' &&
        strchr(sender, '@')) {
        size_t name_length = (size_t)(sender + length - 1U -
                                      (open_parenthesis + 1));
        if (name_length >= sizeof(name)) name_length = sizeof(name) - 1U;
        memcpy(name, open_parenthesis + 1, name_length);
        name[name_length] = 0;
        trim_local_text(name);
        if (name[0]) {
            strncpy(sender, name, capacity - 1U);
            sender[capacity - 1U] = 0;
        }
    }
}

typedef struct SystemLabelDefinition {
    const char *display_de;
    const char *display_en;
    unsigned long special_use;
} SystemLabelDefinition;

static void detach_listbrowser(struct Gadget *gadget, struct Window *window);
static void attach_listbrowser(struct Gadget *gadget, struct Window *window,
                               struct List *list);

static const SystemLabelDefinition system_labels[GUI_SYSTEM_LABEL_COUNT] = {
    {"Posteingang", "Inbox", AMG_LABEL_INBOX},
    {"Markiert", "Starred", AMG_LABEL_FLAGGED},
    {"Gesendet", "Sent", AMG_LABEL_SENT},
    {"Entw\374rfe", "Drafts", AMG_LABEL_DRAFTS},
    {"Alle Nachrichten", "All Mail", AMG_LABEL_ALL},
    {"Spam", "Spam", AMG_LABEL_SPAM},
    {"Papierkorb", "Trash", AMG_LABEL_TRASH}
};

static const char *system_gmail_labels[GUI_SYSTEM_LABEL_COUNT] = {
    "\\Inbox",
    "\\Starred",
    "\\Sent",
    "\\Drafts",
    "\\AllMail",
    "\\Spam",
    "\\Trash"
};

static int ascii_ci_equal(const char *left, const char *right)
{
    unsigned char a, b;
    if (!left || !right) return 0;
    while (*left && *right) {
        a = (unsigned char)*left++;
        b = (unsigned char)*right++;
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
        if (a != b) return 0;
    }
    return !*left && !*right;
}

static unsigned long infer_system_special_use(const char *name_utf8,
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
    if (ascii_ci_equal(leaf, "All Mail") ||
        ascii_ci_equal(leaf, "Alle Nachrichten")) return AMG_LABEL_ALL;
    if (ascii_ci_equal(leaf, "Spam") ||
        ascii_ci_equal(leaf, "Junk")) return AMG_LABEL_SPAM;
    if (ascii_ci_equal(leaf, "Trash") || ascii_ci_equal(leaf, "Bin") ||
        ascii_ci_equal(leaf, "Papierkorb")) return AMG_LABEL_TRASH;
    if (ascii_ci_equal(leaf, "Starred") ||
        ascii_ci_equal(leaf, "Flagged") ||
        ascii_ci_equal(leaf, "Markiert")) return AMG_LABEL_FLAGGED;
    return 0;
}

static void initialize_system_label_map(AmgGui *gui)
{
    size_t i;
    gui->label_count = GUI_SYSTEM_LABEL_COUNT;
    for (i = 0; i < GUI_SYSTEM_LABEL_COUNT; ++i) {
        memset(&gui->labels[i], 0, sizeof(gui->labels[i]));
        {
            const char *display = T(system_labels[i].display_de,
                                    system_labels[i].display_en);
            strncpy(gui->labels[i].display_local, display,
                    sizeof(gui->labels[i].display_local) - 1U);
            strncpy(gui->labels[i].path_local, display,
                    sizeof(gui->labels[i].path_local) - 1U);
        }
        gui->labels[i].delimiter = '/';
        gui->labels[i].generation = 1U;
        gui->labels[i].parent_index = (size_t)-1;
        gui->labels[i].special_use = system_labels[i].special_use;
        strncpy(gui->labels[i].gmail_label_utf8, system_gmail_labels[i],
                sizeof(gui->labels[i].gmail_label_utf8) - 1U);
    }
    strcpy(gui->labels[0].mailbox_utf8, "INBOX");
    gui->labels[0].available = 1;
}

static void rebuild_label_lists(AmgGui *gui)
{
    size_t i;
    detach_listbrowser(gui->system_labels_gadget, gui->window);
    detach_listbrowser(gui->labels_gadget, gui->window);
    FreeListBrowserList(&gui->system_labels_list);
    FreeListBrowserList(&gui->labels_list);
    NewList(&gui->system_labels_list);
    NewList(&gui->labels_list);
    for (i = 0; i < GUI_SYSTEM_LABEL_COUNT; ++i) {
        if (i == GUI_SYSTEM_LABEL_HIDDEN_INDEX) continue;
        struct Node *node = system_label_node(
            gui, gui->labels[i].display_local, (ULONG)i);
        if (node) AddTail(&gui->system_labels_list, node);
    }
    for (i = GUI_SYSTEM_LABEL_COUNT; i < gui->label_count; ++i) {
        struct Node *node = hierarchical_label_node(
            gui, &gui->labels[i], (ULONG)i);
        if (node) AddTail(&gui->labels_list, node);
    }
    HideAllListBrowserChildren(&gui->labels_list);
    apply_label_expansion_state(gui);
    attach_listbrowser(gui->system_labels_gadget, gui->window,
                       &gui->system_labels_list);
    attach_listbrowser(gui->labels_gadget, gui->window, &gui->labels_list);
    if (gui->window) sync_labels_scroller(gui);
}

static void default_labels(AmgGui *gui)
{
    initialize_system_label_map(gui);
    strcpy(gui->current_mailbox_utf8, "INBOX");
    strcpy(gui->current_label_local, T("Posteingang", "Inbox"));
    rebuild_label_lists(gui);
}

static void default_messages(AmgGui *gui)
{
    struct Node *node = message_placeholder_node(
        T("Noch nicht verbunden - 'Abrufen' startet die Gmail-Verbindung.",
          "Not connected yet - 'Fetch' starts the Gmail connection."));
    if (node) AddTail(&gui->messages_list, node);
}

static void detach_listbrowser(struct Gadget *gadget, struct Window *window)
{
    if (gadget)
        SetGadgetAttrs(gadget, window, NULL,
                       LISTBROWSER_Labels, (ULONG)~0UL,
                       TAG_DONE);
}

static void attach_listbrowser(struct Gadget *gadget, struct Window *window,
                               struct List *list)
{
    if (gadget)
        SetGadgetAttrs(gadget, window, NULL,
                       LISTBROWSER_Labels, (ULONG)(uintptr_t)list,
                       LISTBROWSER_Selected, (ULONG)~0UL,
                       LISTBROWSER_Top, 0,
                       TAG_DONE);
}

static void attach_messages_default_date_sort(AmgGui *gui)
{
    if (!gui || !gui->messages_gadget) return;
    if (gui->columns) {
        SetLBColumnInfoAttrs(
            gui->columns,
            LBCIA_Column, 3,
            LBCIA_SortDirection, LBMSORT_REVERSE,
            TAG_DONE);
    }
    SetGadgetAttrs(
        gui->messages_gadget, gui->window, NULL,
        LISTBROWSER_ColumnInfo, (ULONG)(uintptr_t)gui->columns,
        LISTBROWSER_Labels, (ULONG)(uintptr_t)&gui->messages_list,
        LISTBROWSER_Selected, (ULONG)~0UL,
        LISTBROWSER_Top, 0,
        LISTBROWSER_SortColumn, 3,
        TAG_DONE);
}

static void show_message_placeholder(AmgGui *gui, const char *text)
{
    struct Node *node;
    gui->active_message_uid = 0;
    gui->message_click_valid = 0;
    gui->message_click_uid = 0UL;
    detach_listbrowser(gui->messages_gadget, gui->window);
    FreeListBrowserList(&gui->messages_list);
    NewList(&gui->messages_list);
    node = message_placeholder_node(text);
    if (node) AddTail(&gui->messages_list, node);
    attach_listbrowser(gui->messages_gadget, gui->window,
                       &gui->messages_list);
}

static unsigned mail_month_number(const char month[4])
{
    static const char *names[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    unsigned i;
    for (i = 0; i < 12U; ++i) {
        if (((month[0] | 0x20) == (names[i][0] | 0x20)) &&
            ((month[1] | 0x20) == (names[i][1] | 0x20)) &&
            ((month[2] | 0x20) == (names[i][2] | 0x20)))
            return i + 1U;
    }
    return 0;
}

static void format_mail_date(const char *header, char *local, size_t capacity)
{
    const char *date = header;
    const char *comma;
    char month_text[4];
    unsigned long day, year, hour, minute;
    unsigned month;
    if (!local || !capacity) return;
    local[0] = 0;
    if (!date || !*date) return;
    comma = strchr(date, ',');
    if (comma) date = comma + 1;
    while (*date == ' ' || *date == '\t') ++date;
    if (sscanf(date, "%lu %3s %lu %lu:%lu",
               &day, month_text, &year, &hour, &minute) == 5) {
        month = mail_month_number(month_text);
        if (month && day >= 1UL && day <= 31UL &&
            hour <= 23UL && minute <= 59UL) {
            snprintf(local, capacity, "%04lu-%02u-%02lu %02lu:%02lu",
                     year, month, day, hour, minute);
            return;
        }
    }
    utf8_to_local_copy(header, local, capacity);
}

static int label_sort_compare(const GuiLabel *left, const GuiLabel *right)
{
    unsigned char a, b;
    const char *left_text = left->mailbox_utf8;
    const char *right_text = right->mailbox_utf8;
    while (*left_text && *right_text) {
        a = (unsigned char)*left_text++;
        b = (unsigned char)*right_text++;
        if (left->delimiter && a == (unsigned char)left->delimiter) a = 1U;
        if (right->delimiter && b == (unsigned char)right->delimiter) b = 1U;
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
        if (a != b) return a < b ? -1 : 1;
    }
    if (*left_text) return 1;
    if (*right_text) return -1;
    return 0;
}

static void prepare_custom_label_tree(AmgGui *gui)
{
    size_t i, j;
    for (i = GUI_SYSTEM_LABEL_COUNT + 1U; i < gui->label_count; ++i) {
        GuiLabel current = gui->labels[i];
        j = i;
        while (j > GUI_SYSTEM_LABEL_COUNT &&
               label_sort_compare(&gui->labels[j - 1U], &current) > 0) {
            gui->labels[j] = gui->labels[j - 1U];
            --j;
        }
        gui->labels[j] = current;
    }

    for (i = GUI_SYSTEM_LABEL_COUNT; i < gui->label_count; ++i) {
        GuiLabel *label = &gui->labels[i];
        const char *leaf = label->mailbox_utf8;
        const char *cursor;
        unsigned short generation = 1U;
        size_t parent_index = (size_t)-1;
        if (label->delimiter) {
            cursor = label->mailbox_utf8;
            while (*cursor) {
                if (*cursor == label->delimiter) {
                    leaf = cursor + 1;
                }
                ++cursor;
            }
        }
        for (j = GUI_SYSTEM_LABEL_COUNT; j < i; ++j) {
            const GuiLabel *possible_parent = &gui->labels[j];
            size_t prefix_length = strlen(possible_parent->mailbox_utf8);
            if (label->delimiter &&
                possible_parent->delimiter == label->delimiter &&
                !strncmp(label->mailbox_utf8,
                         possible_parent->mailbox_utf8, prefix_length) &&
                label->mailbox_utf8[prefix_length] == label->delimiter &&
                possible_parent->generation + 1U > generation) {
                generation = possible_parent->generation + 1U;
                parent_index = j;
            }
        }
        label->generation = generation;
        label->parent_index = parent_index;
        label->has_next_sibling = 0;
        utf8_to_local_copy(label->mailbox_utf8, label->path_local,
                           sizeof(label->path_local));
        utf8_to_local_copy(leaf, label->display_local,
                           sizeof(label->display_local));
        label->has_children = 0;
        if (i + 1U < gui->label_count && label->delimiter) {
            size_t prefix_length = strlen(label->mailbox_utf8);
            const GuiLabel *next = &gui->labels[i + 1U];
            if (!strncmp(next->mailbox_utf8, label->mailbox_utf8,
                         prefix_length) &&
                next->mailbox_utf8[prefix_length] == label->delimiter)
                label->has_children = 1;
        }
    }

    /* Fuer klassische TreeView-Verbindungslinien muss jede sichtbare Zeile
     * wissen, ob auf derselben Ebene noch ein Geschwister folgt. Die Suche
     * endet sofort beim Verlassen des aktuellen Eltern-Teilbaums. */
    for (i = GUI_SYSTEM_LABEL_COUNT; i < gui->label_count; ++i) {
        GuiLabel *label = &gui->labels[i];
        for (j = i + 1U; j < gui->label_count; ++j) {
            unsigned short next_generation = gui->labels[j].generation;
            if (next_generation < label->generation) break;
            if (next_generation == label->generation) {
                label->has_next_sibling = 1;
                break;
            }
        }
    }
}

static size_t update_labels_from_payload(AmgGui *gui,
                                         const unsigned char *payload,
                                         size_t length)
{
    size_t position = 0, gmail_count = 0;
    initialize_system_label_map(gui);

    while (payload && position < length) {
        size_t line_end = position, first_tab, delimiter_start;
        size_t second_tab, name_start, name_length;
        unsigned long special = 0;
        char delimiter = 0;
        char name_utf8[513], name_local[513];
        size_t system_index = GUI_SYSTEM_LABEL_COUNT;
        size_t i;
        while (line_end < length && payload[line_end] != '\n') ++line_end;
        if (line_end > position && payload[line_end - 1U] == '\r') --line_end;
        first_tab = position;
        while (first_tab < line_end && payload[first_tab] != '\t') {
            if (payload[first_tab] >= '0' && payload[first_tab] <= '9')
                special = special * 10UL +
                          (unsigned long)(payload[first_tab] - '0');
            ++first_tab;
        }
        delimiter_start = first_tab < line_end ? first_tab + 1U : line_end;
        second_tab = delimiter_start;
        while (second_tab < line_end && payload[second_tab] != '\t')
            ++second_tab;
        if (delimiter_start < second_tab &&
            payload[delimiter_start] != ' ')
            delimiter = (char)payload[delimiter_start];
        name_start = second_tab < line_end ? second_tab + 1U : line_end;
        name_length = line_end - name_start;
        if (name_length >= sizeof(name_utf8))
            name_length = sizeof(name_utf8) - 1U;
        if (name_length) memcpy(name_utf8, payload + name_start, name_length);
        name_utf8[name_length] = 0;
        special |= infer_system_special_use(name_utf8, delimiter);
        utf8_to_local_copy(name_utf8, name_local, sizeof(name_local));
        for (i = 0; i < GUI_SYSTEM_LABEL_COUNT; ++i) {
            if (special & system_labels[i].special_use) {
                system_index = i;
                break;
            }
        }
        if (system_index < GUI_SYSTEM_LABEL_COUNT) {
            strncpy(gui->labels[system_index].mailbox_utf8, name_utf8,
                    sizeof(gui->labels[system_index].mailbox_utf8) - 1U);
            gui->labels[system_index].mailbox_utf8[
                sizeof(gui->labels[system_index].mailbox_utf8) - 1U] = 0;
            /* Fuer SELECT, Verschieben und Loeschen immer den vom Server
             * gelieferten Namen verwenden, nicht den lokalen Alias. */
            strncpy(gui->labels[system_index].gmail_label_utf8, name_utf8,
                    sizeof(gui->labels[system_index].gmail_label_utf8) - 1U);
            gui->labels[system_index].gmail_label_utf8[
                sizeof(gui->labels[system_index].gmail_label_utf8) - 1U] = 0;
            gui->labels[system_index].available = 1;
            ++gmail_count;
        } else if (name_local[0] &&
                   strcmp(name_utf8, "[Gmail]") &&
                   !(delimiter && !strncmp(name_utf8, "[Gmail]", 7U) &&
                     name_utf8[7] == delimiter) &&
                   gui->label_count < AMIGMAIL_MAX_LABELS) {
            GuiLabel *label = &gui->labels[gui->label_count++];
            memset(label, 0, sizeof(*label));
            strncpy(label->mailbox_utf8, name_utf8,
                    sizeof(label->mailbox_utf8) - 1U);
            strncpy(label->gmail_label_utf8, name_utf8,
                    sizeof(label->gmail_label_utf8) - 1U);
            label->delimiter = delimiter;
            label->special_use = special;
            label->available = 1;
            ++gmail_count;
        }
        position = line_end;
        while (position < length &&
               (payload[position] == '\r' || payload[position] == '\n'))
            ++position;
    }
    prepare_custom_label_tree(gui);
    load_label_expansion_state(gui);
    rebuild_label_lists(gui);
    return gmail_count;
}

static size_t message_uid_stats(const unsigned char *payload, size_t length,
                                unsigned long baseline,
                                unsigned long *max_uid,
                                int *parse_error)
{
    size_t position = 0U, newer = 0U;
    int result = 0;
    AmgImapFetchRecord record;
    unsigned long maximum = 0UL;
    if (parse_error) *parse_error = 0;
    while (length > 0U &&
           (result = amg_imap_fetch_record_next(
                payload, length, &position, &record)) > 0) {
        if (record.uid > maximum) maximum = record.uid;
        if (baseline && record.uid > baseline) ++newer;
    }
    if (result < 0 && parse_error) *parse_error = result;
    if (max_uid) *max_uid = maximum;
    return newer;
}

static size_t update_messages_from_payload(AmgGui *gui,
                                           const unsigned char *payload,
                                           size_t length, int *parse_error)
{
    size_t position = 0, count = 0;
    int result = 0;
    AmgImapFetchRecord record;
    if (parse_error) *parse_error = 0;
    gui->active_message_uid = 0;
    gui->message_click_valid = 0;
    gui->message_click_uid = 0UL;
    detach_listbrowser(gui->messages_gadget, gui->window);
    FreeListBrowserList(&gui->messages_list);
    NewList(&gui->messages_list);

    /* Ein leerer IMAP-FETCH-Payload ist der normale Zustand eines leeren
     * Ordners. AmgBuffer darf bei length == 0 einen NULL-Datenzeiger liefern;
     * diesen Fall nicht als AMG_ERR_ARGUMENT (-1) an den Parser weiterreichen. */
    while (length > 0U &&
           (result = amg_imap_fetch_record_next(
                payload, length, &position, &record)) > 0) {
        AmgMailHeaders headers;
        const char *from_header, *subject_header, *date_header;
        char from[513], subject[769], date[160];
        struct Node *node;
        amg_mail_headers_init(&headers);
        if (amg_mail_headers_parse((const char *)record.literal,
                                   record.literal_length, &headers,
                                   NULL) != AMG_OK) {
            amg_mail_headers_free(&headers);
            continue;
        }
        from_header = amg_mail_header_get(&headers, "From");
        subject_header = amg_mail_header_get(&headers, "Subject");
        date_header = amg_mail_header_get(&headers, "Date");
        header_to_local(from_header, T("(Unbekannter Absender)", "(Unknown sender)"),
                        from, sizeof(from));
        sender_name_only(from, sizeof(from));
        header_to_local(subject_header, T("(Kein Betreff)", "(No subject)"),
                        subject, sizeof(subject));
        format_mail_date(date_header, date, sizeof(date));
        node = message_node(gui, from, subject, date,
                            record.rfc822_size, record.uid,
                            record.seen, record.flagged,
                            gui->unread_pen, gui->text_pen);
        if (node) {
            /* Die Anzeige soll unabhaengig von der FETCH-Reihenfolge immer
             * mit der neuesten Datumsspalte beginnen. Das ISO-Format aus
             * format_mail_date() ist lexikographisch chronologisch. */
            insert_message_node_date_desc(
                &gui->messages_list, node, date, record.uid);
            ++count;
        }
        amg_mail_headers_free(&headers);
    }
    if (result < 0 && parse_error) *parse_error = result;
    if (!count) {
        const char *message = result < 0
            ? T("Der Ordner konnte nicht ausgewertet werden.",
                "The folder could not be parsed.")
            : T("Dieser Ordner enth\344lt keine Nachrichten.",
                "This folder contains no messages.");
        struct Node *node = message_placeholder_node(message);
        if (node) AddTail(&gui->messages_list, node);
    }
    attach_messages_default_date_sort(gui);
    return count;
}

static int selected_node_user_data(struct Gadget *gadget, ULONG *user_data)
{
    struct Node *node = NULL;
    ULONG value = 0;
    if (!gadget || !user_data) return 0;
    GetAttr(LISTBROWSER_SelectedNode, (Object *)gadget, (ULONG *)&node);
    if (!node) return 0;
    GetListBrowserNodeAttrs(
        node, LBNA_UserData, (ULONG)(uintptr_t)&value, TAG_DONE);
    *user_data = value;
    return 1;
}

static int cursor_node_user_data(struct Gadget *gadget, ULONG *user_data)
{
    struct Node *node = NULL;
    ULONG value = 0;
    if (!gadget || !user_data) return 0;
    GetAttr(LISTBROWSER_CursorNode, (Object *)gadget, (ULONG *)&node);
    if (!node)
        GetAttr(LISTBROWSER_SelectedNode, (Object *)gadget, (ULONG *)&node);
    if (!node) return 0;
    GetListBrowserNodeAttrs(
        node, LBNA_UserData, (ULONG)(uintptr_t)&value, TAG_DONE);
    *user_data = value;
    return 1;
}

static struct Node *find_node_by_user_data(struct List *list,
                                           ULONG user_data)
{
    struct Node *node;
    if (!list) return NULL;
    node = list->lh_Head;
    while (node && node->ln_Succ) {
        ULONG value = 0;
        GetListBrowserNodeAttrs(
            node, LBNA_UserData, (ULONG)(uintptr_t)&value, TAG_DONE);
        if (value == user_data) return node;
        node = node->ln_Succ;
    }
    return NULL;
}


static size_t merge_new_messages_from_payload(AmgGui *gui,
                                              const unsigned char *payload,
                                              size_t length,
                                              int *parse_error)
{
    size_t position = 0U, added = 0U;
    int result = 0;
    ULONG old_top = 0UL;
    ULONG selected_uid;
    AmgImapFetchRecord record;
    struct Node *node, *next;

    if (parse_error) *parse_error = 0;
    if (!gui || !gui->messages_gadget || !gui->window) return 0U;
    selected_uid = gui->active_message_uid;
    GetAttr(LISTBROWSER_Top, (Object *)gui->messages_gadget, &old_top);
    detach_listbrowser(gui->messages_gadget, gui->window);

    /* Platzhalter aus einer zuvor leeren Inbox entfernen, sobald echte
     * Nachrichten eintreffen. */
    node = gui->messages_list.lh_Head;
    while (node && node->ln_Succ) {
        ULONG uid = 0UL;
        next = node->ln_Succ;
        GetListBrowserNodeAttrs(
            node, LBNA_UserData, (ULONG)(uintptr_t)&uid, TAG_DONE);
        if (uid == 0UL) {
            Remove(node);
            FreeListBrowserNode(node);
        }
        node = next;
    }

    while (length > 0U &&
           (result = amg_imap_fetch_record_next(
                payload, length, &position, &record)) > 0) {
        AmgMailHeaders headers;
        const char *from_header, *subject_header, *date_header;
        char from[513], subject[769], date[160];
        if (find_node_by_user_data(&gui->messages_list, record.uid))
            continue;
        amg_mail_headers_init(&headers);
        if (amg_mail_headers_parse((const char *)record.literal,
                                   record.literal_length, &headers,
                                   NULL) != AMG_OK) {
            amg_mail_headers_free(&headers);
            continue;
        }
        from_header = amg_mail_header_get(&headers, "From");
        subject_header = amg_mail_header_get(&headers, "Subject");
        date_header = amg_mail_header_get(&headers, "Date");
        header_to_local(from_header,
                        T("(Unbekannter Absender)", "(Unknown sender)"),
                        from, sizeof(from));
        sender_name_only(from, sizeof(from));
        header_to_local(subject_header,
                        T("(Kein Betreff)", "(No subject)"),
                        subject, sizeof(subject));
        format_mail_date(date_header, date, sizeof(date));
        node = message_node(gui, from, subject, date,
                            record.rfc822_size, record.uid,
                            record.seen, record.flagged,
                            gui->unread_pen, gui->text_pen);
        if (node) {
            insert_message_node_date_desc(
                &gui->messages_list, node, date, record.uid);
            ++added;
        }
        amg_mail_headers_free(&headers);
    }
    if (result < 0 && parse_error) *parse_error = result;

    if (!gui->messages_list.lh_Head->ln_Succ) {
        node = message_placeholder_node(
            T("Dieser Ordner enth\344lt keine Nachrichten.",
              "This folder contains no messages."));
        if (node) AddTail(&gui->messages_list, node);
    }

    if (gui->columns) {
        SetLBColumnInfoAttrs(gui->columns,
                             LBCIA_Column, 3,
                             LBCIA_SortDirection, LBMSORT_REVERSE,
                             TAG_DONE);
    }
    SetGadgetAttrs(gui->messages_gadget, gui->window, NULL,
                   LISTBROWSER_ColumnInfo,
                       (ULONG)(uintptr_t)gui->columns,
                   LISTBROWSER_Labels,
                       (ULONG)(uintptr_t)&gui->messages_list,
                   LISTBROWSER_Selected, (ULONG)~0UL,
                   LISTBROWSER_Top, old_top ? old_top + (ULONG)added : 0UL,
                   LISTBROWSER_SortColumn, 3,
                   TAG_DONE);
    if (selected_uid)
        set_message_selected_visual(gui, selected_uid);
    sync_messages_scroller(gui);
    return added;
}

/* Speichert nur die aufgeklappten benutzerdefinierten Gmail-Labels.
 * Der Mailboxname ist stabiler als die aktuelle Listenposition und bleibt
 * deshalb auch nach einem erneuten FETCH_LABELS erhalten. */
static void load_label_expansion_state(AmgGui *gui)
{
    FILE *file;
    char line[1024];
    size_t i;
    if (!gui) return;
    for (i = GUI_SYSTEM_LABEL_COUNT; i < gui->label_count; ++i)
        gui->labels[i].expanded = 0;

    file = fopen(LABEL_STATE_PATH, "rb");
    if (!file) return;
    while (fgets(line, sizeof(line), file)) {
        size_t length = strlen(line);
        while (length && (line[length - 1U] == '\n' ||
                          line[length - 1U] == '\r'))
            line[--length] = 0;
        if (!length) continue;
        for (i = GUI_SYSTEM_LABEL_COUNT; i < gui->label_count; ++i) {
            if (gui->labels[i].has_children &&
                !strcmp(gui->labels[i].mailbox_utf8, line)) {
                gui->labels[i].expanded = 1;
                break;
            }
        }
    }
    fclose(file);
}

static void save_label_expansion_state(const AmgGui *gui)
{
    FILE *file;
    size_t i;
    if (!gui) return;
    file = fopen(LABEL_STATE_PATH, "wb");
    if (!file) return;
    for (i = GUI_SYSTEM_LABEL_COUNT; i < gui->label_count; ++i) {
        const GuiLabel *label = &gui->labels[i];
        if (label->has_children && label->expanded &&
            label->mailbox_utf8[0])
            fprintf(file, "%s\n", label->mailbox_utf8);
    }
    fclose(file);
}

static void apply_label_expansion_state(AmgGui *gui)
{
    size_t i;
    if (!gui) return;
    /* Eltern stehen nach prepare_custom_label_tree() immer vor ihren
     * Kindern. So werden verschachtelte, ebenfalls gespeicherte Zweige
     * in der richtigen Reihenfolge wieder sichtbar gemacht. */
    for (i = GUI_SYSTEM_LABEL_COUNT; i < gui->label_count; ++i) {
        struct Node *node;
        if (!gui->labels[i].has_children || !gui->labels[i].expanded)
            continue;
        node = find_node_by_user_data(&gui->labels_list, (ULONG)i);
        if (node) ShowListBrowserNodeChildren(node, 1);
    }
}

static size_t selected_message_uids(AmgGui *gui, ULONG *uids,
                                    size_t capacity)
{
    struct Node *node;
    size_t count = 0;
    ULONG fallback = 0;
    if (!gui || !uids || !capacity) return 0;
    node = gui->messages_list.lh_Head;
    while (node && node->ln_Succ) {
        ULONG selected = FALSE;
        ULONG uid = 0;
        GetListBrowserNodeAttrs(
            node,
            LBNA_Selected, (ULONG)(uintptr_t)&selected,
            LBNA_UserData, (ULONG)(uintptr_t)&uid,
            TAG_DONE);
        if (selected && uid && count < capacity) uids[count++] = uid;
        node = node->ln_Succ;
    }
    if (!count && cursor_node_user_data(gui->messages_gadget, &fallback) &&
        fallback)
        uids[count++] = fallback;
    return count;
}

static ULONG *selected_message_uids_alloc(AmgGui *gui, size_t *count)
{
    struct Node *node;
    size_t capacity = 0U;
    ULONG *uids;
    if (count) *count = 0U;
    if (!gui || !count) return NULL;
    node = gui->messages_list.lh_Head;
    while (node && node->ln_Succ) {
        ++capacity;
        node = node->ln_Succ;
    }
    if (capacity < 1U) capacity = 1U;
    uids = (ULONG *)calloc(capacity, sizeof(*uids));
    if (!uids) return NULL;
    *count = selected_message_uids(gui, uids, capacity);
    return uids;
}

static int message_is_seen(AmgGui *gui, ULONG uid)
{
    struct Node *node = find_node_by_user_data(&gui->messages_list, uid);
    ULONG flags = 0;
    if (!node) return 1;
    GetListBrowserNodeAttrs(
        node, LBNA_Flags, (ULONG)(uintptr_t)&flags, TAG_DONE);
    return (flags & LBFLG_CUSTOMPENS) == 0;
}

static int message_is_flagged(AmgGui *gui, ULONG uid)
{
    struct Node *node = find_node_by_user_data(&gui->messages_list, uid);
    ULONG text_value = 0;
    const char *text;
    if (!node) return 0;
    GetListBrowserNodeAttrs(
        node,
        LBNA_Column, 0,
        LBNCA_Text, (ULONG)(uintptr_t)&text_value,
        TAG_DONE);
    text = (const char *)(uintptr_t)text_value;
    return text && text[0];
}

static void set_message_seen_visual(AmgGui *gui, ULONG uid, int seen)
{
    struct Node *node;
    struct TagItem tags[3];
    struct lbEditNode edit;
    if (!gui || !gui->messages_gadget) return;
    node = find_node_by_user_data(&gui->messages_list, uid);
    if (!node) return;
    tags[0].ti_Tag = LBNA_Flags;
    tags[0].ti_Data = seen ? 0UL : LBFLG_CUSTOMPENS;
    if (uid == gui->active_message_uid) {
        /* Der Wechsel der Lesefarbe darf die aktive Zeilenmarkierung nicht
         * verlieren. Beide Attribute werden deshalb atomar aktualisiert. */
        tags[1].ti_Tag = LBNA_Selected;
        tags[1].ti_Data = TRUE;
        tags[2].ti_Tag = TAG_DONE;
        tags[2].ti_Data = 0;
    } else {
        tags[1].ti_Tag = TAG_DONE;
        tags[1].ti_Data = 0;
    }
    edit.MethodID = LBM_EDITNODE;
    edit.lbe_GInfo = NULL;
    edit.lbe_Node = node;
    edit.lbe_NodeAttrs = tags;
    (void)DoGadgetMethodA(gui->messages_gadget, gui->window, NULL,
                          (Msg)&edit);
    if (gui->window)
        RefreshGList(gui->messages_gadget, gui->window, NULL, 1);
}

static void set_message_flagged_visual(AmgGui *gui, ULONG uid, int flagged)
{
    struct Node *node;
    struct TagItem tags[5];
    struct lbEditNode edit;
    size_t tag_count = 0;
    if (!gui || !gui->messages_gadget) return;
    node = find_node_by_user_data(&gui->messages_list, uid);
    if (!node) return;
    tags[tag_count].ti_Tag = LBNA_Column;
    tags[tag_count++].ti_Data = 0;
    tags[tag_count].ti_Tag = LBNCA_CopyText;
    tags[tag_count++].ti_Data = TRUE;
    tags[tag_count].ti_Tag = LBNCA_Text;
    tags[tag_count++].ti_Data =
        (ULONG)(uintptr_t)(flagged ? message_flag_marker : "");
    if (uid == gui->active_message_uid) {
        tags[tag_count].ti_Tag = LBNA_Selected;
        tags[tag_count++].ti_Data = TRUE;
    }
    tags[tag_count].ti_Tag = TAG_DONE;
    tags[tag_count].ti_Data = 0;
    edit.MethodID = LBM_EDITNODE;
    edit.lbe_GInfo = NULL;
    edit.lbe_Node = node;
    edit.lbe_NodeAttrs = tags;
    (void)DoGadgetMethodA(gui->messages_gadget, gui->window, NULL,
                          (Msg)&edit);
    if (gui->window)
        RefreshGList(gui->messages_gadget, gui->window, NULL, 1);
}

static void set_message_selected_visual(AmgGui *gui, ULONG uid)
{
    struct Node *node;
    struct TagItem tags[2];
    struct lbEditNode edit;
    if (!gui || !gui->messages_gadget) return;
    node = find_node_by_user_data(&gui->messages_list, uid);
    if (!node) return;
    gui->active_message_uid = uid;
    tags[0].ti_Tag = LBNA_Selected;
    tags[0].ti_Data = TRUE;
    tags[1].ti_Tag = TAG_DONE;
    tags[1].ti_Data = 0;
    edit.MethodID = LBM_EDITNODE;
    edit.lbe_GInfo = NULL;
    edit.lbe_Node = node;
    edit.lbe_NodeAttrs = tags;
    (void)DoGadgetMethodA(gui->messages_gadget, gui->window, NULL,
                          (Msg)&edit);
    if (gui->window)
        RefreshGList(gui->messages_gadget, gui->window, NULL, 1);
}

static void select_label_index(AmgGui *gui, size_t index)
{
    struct Node *node;
    if (!gui) return;
    if (gui->system_labels_gadget)
        SetGadgetAttrs(gui->system_labels_gadget, gui->window, NULL,
                       LISTBROWSER_Selected, (ULONG)~0UL,
                       TAG_DONE);
    if (gui->labels_gadget)
        SetGadgetAttrs(gui->labels_gadget, gui->window, NULL,
                       LISTBROWSER_Selected, (ULONG)~0UL,
                       TAG_DONE);
    if (index < GUI_SYSTEM_LABEL_COUNT) {
        if (index == GUI_SYSTEM_LABEL_HIDDEN_INDEX) return;
        node = find_node_by_user_data(&gui->system_labels_list,
                                      (ULONG)index);
        if (node && gui->system_labels_gadget)
            SetGadgetAttrs(gui->system_labels_gadget, gui->window, NULL,
                           LISTBROWSER_SelectedNode,
                               (ULONG)(uintptr_t)node,
                           TAG_DONE);
        return;
    }
    node = find_node_by_user_data(&gui->labels_list, (ULONG)index);
    if (node && gui->labels_gadget)
        SetGadgetAttrs(gui->labels_gadget, gui->window, NULL,
                       LISTBROWSER_SelectedNode, (ULONG)(uintptr_t)node,
                       TAG_DONE);
}

static struct Gadget *create_vertical_scroller(ULONG gadget_id)
{
    return (struct Gadget *)NewObject(
        SCROLLER_GetClass(), NULL,
        GA_ID, gadget_id,
        GA_RelVerify, TRUE,
        SCROLLER_Orientation, SORIENT_VERT,
        SCROLLER_Arrows, TRUE,
        SCROLLER_ArrowDelta, 1,
        SCROLLER_Top, 0,
        SCROLLER_Total, 1,
        SCROLLER_Visible, 1,
        TAG_DONE);
}

static ULONG estimate_listbrowser_visible_nodes(struct Window *window,
                                                struct Gadget *listbrowser)
{
    ULONG row_height = 10U;
    ULONG visible;
    LONG height;
    if (!listbrowser) return 1U;
    if (window && window->RPort && window->RPort->TxHeight > 0)
        row_height = (ULONG)window->RPort->TxHeight + 4U;
    height = (LONG)listbrowser->Height - 6L;
    if (height <= 0L) return 1U;
    visible = (ULONG)height / row_height;
    return visible ? visible : 1U;
}

static void set_scroller_full(struct Window *window, struct Gadget *scroller)
{
    if (!window || !scroller) return;
    SetGadgetAttrs(scroller, window, NULL,
                   SCROLLER_Top, 0,
                   SCROLLER_Total, 1,
                   SCROLLER_Visible, 1,
                   TAG_DONE);
    RefreshGList(scroller, window, NULL, 1);
}

static void sync_listbrowser_scroller(struct Window *window,
                                      struct Gadget *listbrowser,
                                      struct Gadget *scroller)
{
    ULONG top = 0, total = 1, visible = 1;
    ULONG total_nodes = 0, estimated_visible;
    if (!window || !listbrowser || !scroller) return;

    estimated_visible = estimate_listbrowser_visible_nodes(window, listbrowser);
    GetAttr(LISTBROWSER_TotalVisibleNodes, (Object *)listbrowser, &total_nodes);

    /* Bei komplett sichtbarem Inhalt muss der Prop-Knopf die gesamte Bahn
     * ausfuellen. Insbesondere aeltere ReAction-Versionen liefern bei einem
     * externen Scroller und LISTBROWSER_VerticalProp=FALSE teils unbrauchbare
     * VProp-Verhaeltnisse, obwohl gar nichts zu scrollen ist. */
    if (total_nodes == 0U || total_nodes <= estimated_visible) {
        SetGadgetAttrs(listbrowser, window, NULL,
                       LISTBROWSER_VPropTop, 0,
                       TAG_DONE);
        set_scroller_full(window, scroller);
        return;
    }

    GetAttr(LISTBROWSER_VPropTop, (Object *)listbrowser, &top);
    GetAttr(LISTBROWSER_VPropTotal, (Object *)listbrowser, &total);
    GetAttr(LISTBROWSER_VPropVisible, (Object *)listbrowser, &visible);
    if (total < 1U) total = total_nodes ? total_nodes : 1U;
    if (visible < 1U) visible = 1U;
    if (visible > total) visible = total;
    if (top > total - visible) top = total - visible;
    SetGadgetAttrs(scroller, window, NULL,
                   SCROLLER_Top, top,
                   SCROLLER_Total, total,
                   SCROLLER_Visible, visible,
                   TAG_DONE);
    RefreshGList(scroller, window, NULL, 1);
}

static void handle_listbrowser_scroller(struct Window *window,
                                        struct Gadget *listbrowser,
                                        struct Gadget *scroller)
{
    ULONG top = 0;
    if (!window || !listbrowser || !scroller) return;
    GetAttr(SCROLLER_Top, (Object *)scroller, &top);
    SetGadgetAttrs(listbrowser, window, NULL,
                   LISTBROWSER_VPropTop, top,
                   TAG_DONE);
    RefreshGList(listbrowser, window, NULL, 1);
    sync_listbrowser_scroller(window, listbrowser, scroller);
}

/* Label-Scrollbar: Hierarchische ListBrowser liefern auf einigen klassischen
 * ReAction-Versionen mit ausgeschaltetem internen Prop-Gadget unzuverlaessige
 * LISTBROWSER_VProp*-Werte. Deshalb verwenden wir dieselbe Einheit wie der
 * ListBrowser selbst: sichtbare Nodes und LISTBROWSER_Top. Ein absichtlich zu
 * grosser Top-Wert wird von ReAction auf den echten letzten Seitenanfang
 * begrenzt. Dadurch skaliert der externe Scroller auch bei auf-/zugeklappten
 * Unterordnern korrekt und erreicht garantiert das Listenende. */
static void label_scroll_geometry(AmgGui *gui, ULONG *top_out,
                                  ULONG *total_out, ULONG *visible_out)
{
    ULONG current = 0, max_top = 0, total = 0, visible = 1;
    if (!gui || !gui->window || !gui->labels_gadget) {
        if (top_out) *top_out = 0;
        if (total_out) *total_out = 1;
        if (visible_out) *visible_out = 1;
        return;
    }

    GetAttr(LISTBROWSER_TotalVisibleNodes,
            (Object *)gui->labels_gadget, &total);
    if (total < 1U) total = 1U;
    GetAttr(LISTBROWSER_Top, (Object *)gui->labels_gadget, &current);

    SetGadgetAttrs(gui->labels_gadget, gui->window, NULL,
                   LISTBROWSER_Top, 0x7fffffffUL,
                   TAG_DONE);
    GetAttr(LISTBROWSER_Top, (Object *)gui->labels_gadget, &max_top);
    if (current > max_top) current = max_top;
    SetGadgetAttrs(gui->labels_gadget, gui->window, NULL,
                   LISTBROWSER_Top, current,
                   TAG_DONE);

    if (max_top == 0U || total <= max_top) {
        visible = total;
        current = 0U;
    } else {
        visible = total - max_top;
        if (visible < 1U) visible = 1U;
        if (visible > total) visible = total;
    }

    if (top_out) *top_out = current;
    if (total_out) *total_out = total;
    if (visible_out) *visible_out = visible;
}

static void sync_labels_scroller(AmgGui *gui)
{
    ULONG top = 0, total = 1, visible = 1;
    if (!gui || !gui->window || !gui->labels_gadget ||
        !gui->labels_scroller)
        return;

    label_scroll_geometry(gui, &top, &total, &visible);
    if (total <= visible) {
        SetGadgetAttrs(gui->labels_gadget, gui->window, NULL,
                       LISTBROWSER_Top, 0,
                       TAG_DONE);
        set_scroller_full(gui->window, gui->labels_scroller);
        return;
    }
    if (top > total - visible) top = total - visible;
    SetGadgetAttrs(gui->labels_scroller, gui->window, NULL,
                   SCROLLER_Top, top,
                   SCROLLER_Total, total,
                   SCROLLER_Visible, visible,
                   TAG_DONE);
    RefreshGList(gui->labels_scroller, gui->window, NULL, 1);
}

static void handle_labels_scroller(AmgGui *gui)
{
    ULONG top = 0, total = 1, visible = 1;
    if (!gui || !gui->window || !gui->labels_gadget ||
        !gui->labels_scroller)
        return;

    label_scroll_geometry(gui, NULL, &total, &visible);
    GetAttr(SCROLLER_Top, (Object *)gui->labels_scroller, &top);
    if (total <= visible) top = 0U;
    else if (top > total - visible) top = total - visible;

    SetGadgetAttrs(gui->labels_gadget, gui->window, NULL,
                   LISTBROWSER_Top, top,
                   TAG_DONE);
    RefreshGList(gui->labels_gadget, gui->window, NULL, 1);
    SetGadgetAttrs(gui->labels_scroller, gui->window, NULL,
                   SCROLLER_Top, top,
                   SCROLLER_Total, total,
                   SCROLLER_Visible, visible,
                   TAG_DONE);
    RefreshGList(gui->labels_scroller, gui->window, NULL, 1);
}

/* Nachrichten-Scrollbar: ReAction bestimmt selbst, welche Zeile die
 * letzte moegliche oberste Zeile ist. Das ist robuster als eine Schaetzung
 * aus Font- und Gadgethoehe, weil ListBrowser intern Titel, Rahmen,
 * Zeilenabstand und WrapText beruecksichtigt. */
static void message_scroll_geometry(AmgGui *gui, ULONG *top_out,
                                    ULONG *total_out, ULONG *visible_out)
{
    ULONG current = 0, max_top = 0, total = 0, visible = 1;
    if (!gui || !gui->messages_gadget) {
        if (top_out) *top_out = 0;
        if (total_out) *total_out = 1;
        if (visible_out) *visible_out = 1;
        return;
    }

    GetAttr(LISTBROWSER_TotalVisibleNodes,
            (Object *)gui->messages_gadget, &total);
    if (total < 1U) total = 1U;
    GetAttr(LISTBROWSER_Top, (Object *)gui->messages_gadget, &current);

    /* Absichtlich einen zu grossen Top-Wert setzen. ListBrowser clamp't
     * ihn auf den echten letzten Seitenanfang. Ohne Refresh ist dieser
     * kurze Probe-Zustand nicht sichtbar. */
    SetGadgetAttrs(gui->messages_gadget, gui->window, NULL,
                   LISTBROWSER_Top, 0x7fffffffUL,
                   TAG_DONE);
    GetAttr(LISTBROWSER_Top, (Object *)gui->messages_gadget, &max_top);
    if (current > max_top) current = max_top;
    SetGadgetAttrs(gui->messages_gadget, gui->window, NULL,
                   LISTBROWSER_Top, current,
                   TAG_DONE);

    if (max_top == 0U || total <= max_top) {
        visible = total;
        current = 0U;
    } else {
        visible = total - max_top;
        if (visible < 1U) visible = 1U;
        if (visible > total) visible = total;
    }
    if (top_out) *top_out = current;
    if (total_out) *total_out = total;
    if (visible_out) *visible_out = visible;
}

static void sync_messages_scroller(AmgGui *gui)
{
    ULONG top = 0, total = 1, visible = 1;
    if (!gui || !gui->window || !gui->messages_gadget ||
        !gui->messages_scroller)
        return;

    message_scroll_geometry(gui, &top, &total, &visible);
    if (total <= visible) {
        SetGadgetAttrs(gui->messages_gadget, gui->window, NULL,
                       LISTBROWSER_Top, 0,
                       TAG_DONE);
        set_scroller_full(gui->window, gui->messages_scroller);
        return;
    }
    if (top > total - visible) top = total - visible;
    SetGadgetAttrs(gui->messages_scroller, gui->window, NULL,
                   SCROLLER_Top, top,
                   SCROLLER_Total, total,
                   SCROLLER_Visible, visible,
                   TAG_DONE);
    RefreshGList(gui->messages_scroller, gui->window, NULL, 1);
}

static void handle_messages_scroller(AmgGui *gui)
{
    ULONG top = 0, total = 1, visible = 1;
    if (!gui || !gui->window || !gui->messages_gadget ||
        !gui->messages_scroller)
        return;

    message_scroll_geometry(gui, NULL, &total, &visible);
    GetAttr(SCROLLER_Top, (Object *)gui->messages_scroller, &top);
    if (total <= visible) top = 0U;
    else if (top > total - visible) top = total - visible;
    SetGadgetAttrs(gui->messages_gadget, gui->window, NULL,
                   LISTBROWSER_Top, top,
                   TAG_DONE);
    RefreshGList(gui->messages_gadget, gui->window, NULL, 1);

    /* Nicht erneut aus einer geschaetzten Geometrie zurueckskalieren.
     * Der gerade gesetzte Top-Wert und die vom ListBrowser ermittelte
     * letzte Seite verwenden dieselbe Einheit. */
    SetGadgetAttrs(gui->messages_scroller, gui->window, NULL,
                   SCROLLER_Top, top,
                   SCROLLER_Total, total,
                   SCROLLER_Visible, visible,
                   TAG_DONE);
    RefreshGList(gui->messages_scroller, gui->window, NULL, 1);
}

static ULONG count_preview_lines(const char *text)
{
    ULONG lines = 1;
    const unsigned char *cursor = (const unsigned char *)(text ? text : "");
    while (*cursor) {
        if (*cursor == '\n')
            ++lines;
        else if (*cursor == '\r' && cursor[1] != '\n')
            ++lines;
        ++cursor;
    }
    return lines ? lines : 1;
}

static ULONG estimate_texteditor_visible_lines(struct Window *window,
                                               struct Gadget *editor)
{
    ULONG line_height = 8;
    ULONG visible;
    LONG height;
    if (!editor) return 1;
    if (window && window->RPort && window->RPort->TxHeight > 0)
        line_height = (ULONG)window->RPort->TxHeight;
    height = (LONG)editor->Height;
    if (height > 6L) height -= 6L;
    if (height <= 0L) return 1;
    visible = (ULONG)height / line_height;
    return visible ? visible : 1;
}

static void sync_texteditor_scroller(struct Window *window,
                                     struct Gadget *editor,
                                     struct Gadget *scroller,
                                     ULONG fallback_entries,
                                     int reset_top)
{
    ULONG first = 0, entries = 1, visible = 1;
    ULONG estimated_visible;
    if (!window || !editor || !scroller) return;
    estimated_visible = estimate_texteditor_visible_lines(window, editor);
    GetAttr(GA_TEXTEDITOR_Prop_First, (Object *)editor, &first);
    GetAttr(GA_TEXTEDITOR_Prop_Entries, (Object *)editor, &entries);
    GetAttr(GA_TEXTEDITOR_Prop_Visible, (Object *)editor, &visible);
    if (fallback_entries > entries) entries = fallback_entries;
    if (entries < 1U) entries = 1U;
    if (estimated_visible < 1U) estimated_visible = 1U;

    /* Ist der komplette Text sichtbar, immer eine volle Scrollbar anzeigen.
     * Damit sehen leere/kurze Textfelder nicht wie sehr lange Dokumente aus. */
    if (entries <= estimated_visible ||
        (fallback_entries > 0U && fallback_entries <= estimated_visible)) {
        if (reset_top || first != 0U)
            SetGadgetAttrs(editor, window, NULL,
                           GA_TEXTEDITOR_Prop_First, 0,
                           TAG_DONE);
        set_scroller_full(window, scroller);
        return;
    }

    if (visible <= 1U && estimated_visible > 1U) visible = estimated_visible;
    if (visible < 1U) visible = 1U;
    if (visible > entries) visible = entries;
    if (reset_top) first = 0U;
    if (first > entries - visible) first = entries - visible;
    SetGadgetAttrs(scroller, window, NULL,
                   SCROLLER_Top, first,
                   SCROLLER_Total, entries,
                   SCROLLER_Visible, visible,
                   TAG_DONE);
    RefreshGList(scroller, window, NULL, 1);
}

static void handle_texteditor_scroller(struct Window *window,
                                       struct Gadget *editor,
                                       struct Gadget *scroller,
                                       ULONG fallback_entries)
{
    ULONG top = 0;
    if (!window || !editor || !scroller) return;
    GetAttr(SCROLLER_Top, (Object *)scroller, &top);
    SetGadgetAttrs(editor, window, NULL,
                   GA_TEXTEDITOR_Prop_First, top,
                   TAG_DONE);
    RefreshGList(editor, window, NULL, 1);
    sync_texteditor_scroller(window, editor, scroller,
                             fallback_entries, 0);
}

static void sync_preview_scroller(AmgGui *gui, int reset_top)
{
    if (!gui || !gui->preview_gadget || !gui->preview_scroller ||
        !gui->window) return;
    sync_texteditor_scroller(gui->window, gui->preview_gadget,
                             gui->preview_scroller,
                             gui->preview_line_count, reset_top);
}

static void handle_preview_scroller(AmgGui *gui)
{
    if (!gui || !gui->preview_gadget || !gui->preview_scroller ||
        !gui->window) return;
    handle_texteditor_scroller(gui->window, gui->preview_gadget,
                               gui->preview_scroller,
                               gui->preview_line_count);
}

static void set_preview_local(AmgGui *gui, const char *local)
{
    AmgBuffer styled;
    const char *contents = local ? local : "";
    if (!gui || !gui->preview_gadget) return;

    gui->preview_line_count = count_preview_lines(contents);
    amg_buffer_init(&styled);
    if (decorate_preview_links(contents, &styled) == AMG_OK &&
        amg_buffer_terminate(&styled) == AMG_OK)
        contents = (const char *)styled.data;

    SetGadgetAttrs(gui->preview_gadget, gui->window, NULL,
                   GA_TEXTEDITOR_Contents, (ULONG)(uintptr_t)contents,
                   GA_TEXTEDITOR_Prop_First, 0,
                   TAG_DONE);
    sync_preview_scroller(gui, 1);
    amg_buffer_free(&styled);
}

static void set_preview_utf8(AmgGui *gui, const unsigned char *utf8,
                             size_t length)
{
    AmgBuffer local;
    amg_buffer_init(&local);
    if (utf8 && amg_utf8_to_local((const char *)utf8, &local) == AMG_OK &&
        amg_buffer_terminate(&local) == AMG_OK)
        set_preview_local(gui, (const char *)local.data);
    else
        set_preview_local(gui, T("Nachricht konnte nicht dargestellt werden.", "Message could not be displayed."));
    amg_buffer_free(&local);
    (void)length;
}

static int append_preview_header(AmgBuffer *preview, const char *name,
                                 const char *value)
{
    AmgBuffer decoded;
    int result;
    amg_buffer_init(&decoded);
    result = amg_buffer_append_cstr(preview, name);
    if (result == AMG_OK) {
        if (value && *value && amg_rfc2047_decode(value, &decoded) == AMG_OK)
            result = amg_buffer_append(preview, decoded.data, decoded.length);
        else
            result = amg_buffer_append_cstr(preview, "-");
    }
    if (result == AMG_OK) result = amg_buffer_append_char(preview, '\n');
    amg_buffer_free(&decoded);
    return result;
}

static int display_message_payload(AmgGui *gui, const unsigned char *payload,
                                   size_t payload_length, AmgError *error)
{
    AmgImapFetchRecord record;
    AmgMailHeaders headers;
    AmgBuffer body, preview, attachments;
    size_t position = 0;
    int result;
    result = amg_imap_fetch_record_next(payload, payload_length,
                                        &position, &record);
    if (result <= 0) {
        amg_error_set(error, result < 0 ? result : AMG_ERR_PARSE,
                      T("Die ausgew\303\244hlte Nachricht enth\303\244lt keinen Mail-Datenblock.", "The selected message contains no mail data block."));
        return result < 0 ? result : AMG_ERR_PARSE;
    }

    amg_mail_headers_init(&headers);
    amg_buffer_init(&body);
    amg_buffer_init(&preview);
    amg_buffer_init(&attachments);
    result = amg_mail_headers_parse((const char *)record.literal,
                                    record.literal_length, &headers, NULL);
    if (result == AMG_OK)
        result = append_preview_header(
            &preview, T("Von: ", "From: "), amg_mail_header_get(&headers, "From"));
    if (result == AMG_OK)
        result = append_preview_header(
            &preview, T("An: ", "To: "), amg_mail_header_get(&headers, "To"));
    if (result == AMG_OK)
        result = append_preview_header(
            &preview, T("Datum: ", "Date: "), amg_mail_header_get(&headers, "Date"));
    if (result == AMG_OK)
        result = append_preview_header(
            &preview, T("Betreff: ", "Subject: "), amg_mail_header_get(&headers, "Subject"));
    if (result == AMG_OK)
        result = amg_buffer_append_char(&preview, '\n');
    if (result == AMG_OK)
        result = amg_mime_extract_text((const char *)record.literal,
                                       record.literal_length, &body, error);
    if (result == AMG_OK)
        result = amg_buffer_append(&preview, body.data, body.length);
    if (result == AMG_OK &&
        amg_mime_attachment_summary((const char *)record.literal,
                                    record.literal_length, &attachments,
                                    NULL) == AMG_OK &&
        attachments.length) {
        result = amg_buffer_append_cstr(&preview, T("\n\nAnh\303\244nge:\n", "\n\nAttachments:\n"));
        if (result == AMG_OK)
            result = amg_buffer_append(&preview, attachments.data,
                                       attachments.length);
    }
    if (result == AMG_OK && amg_buffer_terminate(&preview) == AMG_OK) {
        set_preview_utf8(gui, preview.data, preview.length);
        amg_error_set(error, AMG_OK, "");
    } else if (result != AMG_OK) {
        set_preview_local(gui, T("Nachrichtentext konnte nicht dargestellt werden.", "Message text could not be displayed."));
    }
    amg_mail_headers_free(&headers);
    amg_buffer_free(&body);
    amg_buffer_free(&preview);
    amg_buffer_free(&attachments);
    return result;
}

static void set_attachment_button_enabled(AmgGui *gui, int enabled)
{
    if (!gui || !gui->save_attachments_gadget || !gui->window) return;
    SetGadgetAttrs(gui->save_attachments_gadget, gui->window, NULL,
                   GA_Disabled, enabled ? FALSE : TRUE,
                   TAG_DONE);
    RefreshGList(gui->save_attachments_gadget, gui->window, NULL, 1);
}

static void clear_current_message_payload(AmgGui *gui)
{
    if (!gui) return;
    free(gui->current_message_payload);
    gui->current_message_payload = NULL;
    gui->current_message_payload_length = 0U;
    gui->current_attachment_count = 0U;
    set_attachment_button_enabled(gui, 0);
}

static int copy_first_message_literal(const unsigned char *payload,
                                      size_t payload_length,
                                      unsigned char **message_out,
                                      size_t *message_length_out)
{
    AmgImapFetchRecord record;
    unsigned char *copy;
    size_t position = 0U;
    int result;
    if (!message_out || !message_length_out ||
        (!payload && payload_length != 0U))
        return AMG_ERR_ARGUMENT;
    *message_out = NULL;
    *message_length_out = 0U;
    result = amg_imap_fetch_record_next(payload, payload_length,
                                        &position, &record);
    if (result <= 0) return result < 0 ? result : AMG_ERR_PARSE;
    if (record.literal_length > AMIGMAIL_MAX_MESSAGE)
        return AMG_ERR_LIMIT;
    copy = (unsigned char *)malloc(record.literal_length + 1U);
    if (!copy) return AMG_ERR_MEMORY;
    if (record.literal_length)
        memcpy(copy, record.literal, record.literal_length);
    copy[record.literal_length] = 0;
    *message_out = copy;
    *message_length_out = record.literal_length;
    return AMG_OK;
}

static void retain_current_message_payload(AmgGui *gui,
                                           const AmgNetworkEvent *event)
{
    unsigned char *message = NULL;
    size_t message_length = 0U;
    size_t count = 0U;
    int result;
    if (!gui || !event) return;

    /* Nicht den NetworkEvent-Payload stehlen: Derselbe Event wird beim
     * Antworten unmittelbar danach noch von prepare_reply_payload()
     * ausgewertet. Stattdessen nur den eigentlichen RFC822/MIME-Literalblock
     * separat speichern. Das ist zugleich die korrekte Eingabe fuer die
     * Anhangserkennung und -extraktion. */
    result = copy_first_message_literal(event->payload,
                                        event->payload_length,
                                        &message, &message_length);
    clear_current_message_payload(gui);
    if (result != AMG_OK) return;

    gui->current_message_payload = message;
    gui->current_message_payload_length = message_length;
    if (amg_mime_attachment_count(
            (const char *)gui->current_message_payload,
            gui->current_message_payload_length, &count, NULL) == AMG_OK) {
        gui->current_attachment_count = count;
    }
    set_attachment_button_enabled(gui, count > 0U);
}

static void sanitize_attachment_name(const char *name_utf8,
                                     char *name_local, size_t capacity)
{
    size_t i, used;
    if (!name_local || !capacity) return;
    utf8_to_local_copy(name_utf8 && *name_utf8 ? name_utf8 : T("Anhang.bin", "attachment.bin"),
                       name_local, capacity);
    used = strlen(name_local);
    for (i = 0U; i < used; ++i) {
        unsigned char c = (unsigned char)name_local[i];
        if (c < 32U || c == ':' || c == '/' || c == '\\')
            name_local[i] = '_';
    }
    while (name_local[0] == '.')
        memmove(name_local, name_local + 1, strlen(name_local));
    if (!name_local[0]) strcpy(name_local, T("Anhang.bin", "attachment.bin"));
}

static int build_unique_attachment_path(const char *drawer,
                                        const char *name,
                                        char *path, size_t capacity)
{
    unsigned long suffix = 0UL;
    if (!drawer || !path || capacity < 4U) return AMG_ERR_ARGUMENT;
    for (;;) {
        char candidate[COMPOSE_NAME_MAX + 32U];
        BPTR lock;
        if (suffix == 0UL)
            snprintf(candidate, sizeof(candidate), "%s", name);
        else
            snprintf(candidate, sizeof(candidate), "%s.%lu", name, suffix);
        strncpy(path, drawer, capacity - 1U);
        path[capacity - 1U] = 0;
        if (!AddPart((STRPTR)path, (STRPTR)candidate, (LONG)capacity))
            return AMG_ERR_LIMIT;
        lock = Lock((STRPTR)path, ACCESS_READ);
        if (!lock) return AMG_OK;
        UnLock(lock);
        if (++suffix > 9999UL) return AMG_ERR_LIMIT;
    }
}

static void save_current_attachments(AmgGui *gui)
{
    struct FileRequester *request;
    char drawer[COMPOSE_PATH_MAX];
    size_t i, saved = 0U;
    if (!gui || !gui->current_message_payload ||
        !gui->current_attachment_count) {
        status_local(gui, T("Diese Nachricht enth\344lt keine speicherbaren Anh\344nge.", "This message has no savable attachments."));
        return;
    }
    request = (struct FileRequester *)AllocAslRequestTags(
        ASL_FileRequest,
        ASLFR_TitleText, (ULONG)(uintptr_t)T("Anh\344nge speichern", "Save attachments"),
        ASLFR_Window, (ULONG)(uintptr_t)gui->window,
        ASLFR_SleepWindow, TRUE,
        ASLFR_DrawersOnly, TRUE,
        ASLFR_RejectIcons, TRUE,
        TAG_DONE);
    if (!request) {
        status_local(gui, T("Zielordner konnte nicht ausgew\344hlt werden.", "Destination folder could not be selected."));
        return;
    }
    if (!AslRequest(request, NULL)) {
        FreeAslRequest(request);
        return;
    }
    strncpy(drawer, request->rf_Dir ? (const char *)request->rf_Dir : "",
            sizeof(drawer) - 1U);
    drawer[sizeof(drawer) - 1U] = 0;
    FreeAslRequest(request);
    if (!drawer[0]) {
        status_local(gui, T("Kein Zielordner ausgew\344hlt.", "No destination folder selected."));
        return;
    }

    for (i = 0U; i < gui->current_attachment_count; ++i) {
        AmgBuffer name_utf8, data;
        AmgError attachment_error;
        char name_local[COMPOSE_NAME_MAX];
        char path[COMPOSE_PATH_MAX];
        FILE *file;
        int result;
        amg_buffer_init(&name_utf8);
        amg_buffer_init(&data);
        memset(&attachment_error, 0, sizeof(attachment_error));
        result = amg_mime_extract_attachment(
            (const char *)gui->current_message_payload,
            gui->current_message_payload_length, i,
            &name_utf8, &data, &attachment_error);
        if (result != AMG_OK) {
            amg_buffer_free(&name_utf8);
            amg_buffer_free(&data);
            status_utf8(gui, attachment_error.message[0]
                                 ? attachment_error.message
                                 : T("Anhang konnte nicht gespeichert werden.", "Attachment could not be saved."));
            return;
        }
        sanitize_attachment_name((const char *)name_utf8.data,
                                 name_local, sizeof(name_local));
        result = build_unique_attachment_path(
            drawer, name_local, path, sizeof(path));
        if (result != AMG_OK) {
            amg_buffer_free(&name_utf8);
            amg_buffer_free(&data);
            status_local(gui, T("Dateipfad f\374r Anhang ist zu lang.", "Attachment path is too long."));
            return;
        }
        file = fopen(path, "wb");
        if (!file) {
            amg_buffer_free(&name_utf8);
            amg_buffer_free(&data);
            status_local(gui, T("Anhang konnte nicht auf Disk geschrieben werden.", "Attachment could not be written to disk."));
            return;
        }
        {
            int write_failed = data.length &&
                fwrite(data.data, 1U, data.length, file) != data.length;
            int close_failed = fclose(file) != 0;
            if (write_failed || close_failed) {
                amg_buffer_free(&name_utf8);
                amg_buffer_free(&data);
                status_local(gui, T("Anhang konnte nicht auf Disk geschrieben werden.", "Attachment could not be written to disk."));
                return;
            }
        }
        ++saved;
        amg_buffer_free(&name_utf8);
        amg_buffer_free(&data);
    }
    {
        char message[128];
        amg_tr_snprintf(message, sizeof(message),
                        "%lu Anhang/Anh\344nge gespeichert.",
                        "%lu attachment(s) saved.",
                        (unsigned long)saved);
        status_local(gui, message);
    }
}

static void append_local_limited(char *destination, size_t capacity,
                                 const char *source)
{
    size_t used, available, length;
    if (!destination || !capacity || !source) return;
    used = strlen(destination);
    if (used >= capacity - 1U) return;
    available = capacity - used - 1U;
    length = strlen(source);
    if (length > available) length = available;
    memcpy(destination + used, source, length);
    destination[used + length] = 0;
}

static int prepare_reply_payload(AmgGui *gui, const unsigned char *payload,
                                 size_t payload_length, AmgError *error)
{
    AmgImapFetchRecord record;
    AmgMailHeaders headers;
    AmgBuffer body_utf8, body_local;
    const char *reply_to, *subject, *date, *message_id, *references;
    char from_local[768], subject_local[512], date_local[192];
    const char *cursor;
    size_t position = 0;
    int result;
    if (!gui) return AMG_ERR_ARGUMENT;
    result = amg_imap_fetch_record_next(payload, payload_length,
                                        &position, &record);
    if (result <= 0) {
        amg_error_set(error, result < 0 ? result : AMG_ERR_PARSE,
                      T("Antwortdaten konnten nicht ausgewertet werden.", "Reply data could not be parsed."));
        return result < 0 ? result : AMG_ERR_PARSE;
    }
    amg_mail_headers_init(&headers);
    amg_buffer_init(&body_utf8);
    amg_buffer_init(&body_local);
    result = amg_mail_headers_parse((const char *)record.literal,
                                    record.literal_length, &headers, NULL);
    if (result != AMG_OK) goto done;
    reply_to = amg_mail_header_get(&headers, "Reply-To");
    if (!reply_to || !*reply_to)
        reply_to = amg_mail_header_get(&headers, "From");
    subject = amg_mail_header_get(&headers, "Subject");
    date = amg_mail_header_get(&headers, "Date");
    message_id = amg_mail_header_get(&headers, "Message-ID");
    references = amg_mail_header_get(&headers, "References");
    header_to_local(reply_to, "", gui->reply_to_local,
                    sizeof(gui->reply_to_local));
    header_to_local(amg_mail_header_get(&headers, "From"),
                    T("Absender", "sender"), from_local, sizeof(from_local));
    header_to_local(subject, "", subject_local, sizeof(subject_local));
    header_to_local(date, "", date_local, sizeof(date_local));
    if (!subject_local[0])
        strcpy(gui->reply_subject_local, "Re:");
    else if ((subject_local[0] == 'R' || subject_local[0] == 'r') &&
             (subject_local[1] == 'E' || subject_local[1] == 'e') &&
             subject_local[2] == ':')
        snprintf(gui->reply_subject_local,
                 sizeof(gui->reply_subject_local), "%s", subject_local);
    else
        snprintf(gui->reply_subject_local,
                 sizeof(gui->reply_subject_local), "Re: %.507s", subject_local);

    snprintf(gui->reply_in_reply_to_utf8,
             sizeof(gui->reply_in_reply_to_utf8), "%s",
             message_id ? message_id : "");
    gui->reply_references_utf8[0] = 0;
    if (references && *references) {
        snprintf(gui->reply_references_utf8,
                 sizeof(gui->reply_references_utf8), "%s", references);
    }
    if (message_id && *message_id) {
        if (gui->reply_references_utf8[0])
            append_local_limited(gui->reply_references_utf8,
                                 sizeof(gui->reply_references_utf8), " ");
        append_local_limited(gui->reply_references_utf8,
                             sizeof(gui->reply_references_utf8), message_id);
    }

    result = amg_mime_extract_text((const char *)record.literal,
                                   record.literal_length, &body_utf8, error);
    if (result != AMG_OK) goto done;
    result = amg_buffer_terminate(&body_utf8);
    if (result == AMG_OK)
        result = amg_utf8_to_local((const char *)body_utf8.data, &body_local);
    if (result == AMG_OK) result = amg_buffer_terminate(&body_local);
    if (result != AMG_OK) goto done;

    gui->reply_body_local[0] = 0;
    if (amg_i18n_is_german()) {
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local), "\n\nAm ");
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local),
                             date_local[0] ? date_local : "unbekanntem Datum");
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local), " schrieb ");
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local), from_local);
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local), ":\n");
    } else {
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local), "\n\nOn ");
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local),
                             date_local[0] ? date_local : "an unknown date");
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local), ", ");
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local), from_local);
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local), " wrote:\n");
    }
    cursor = (const char *)body_local.data;
    while (*cursor && strlen(gui->reply_body_local) + 4U <
                         sizeof(gui->reply_body_local)) {
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local), "> ");
        while (*cursor && *cursor != '\r' && *cursor != '\n') {
            char character[2];
            character[0] = *cursor++;
            character[1] = 0;
            append_local_limited(gui->reply_body_local,
                                 sizeof(gui->reply_body_local), character);
        }
        if (*cursor == '\r') ++cursor;
        if (*cursor == '\n') ++cursor;
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local), "\n");
    }
    amg_error_set(error, AMG_OK, "");

done:
    if (result != AMG_OK && error && !error->message[0])
        amg_error_set(error, result,
                      T("Antwort konnte nicht vorbereitet werden.", "Reply could not be prepared."));
    amg_mail_headers_free(&headers);
    amg_buffer_free(&body_utf8);
    amg_buffer_free(&body_local);
    return result;
}

static unsigned short banner_be16(const unsigned char *data)
{
    return (unsigned short)(((unsigned)data[0] << 8) | data[1]);
}

static unsigned long banner_be32(const unsigned char *data)
{
    return ((unsigned long)data[0] << 24) |
           ((unsigned long)data[1] << 16) |
           ((unsigned long)data[2] << 8) | data[3];
}

static const unsigned char *banner_chunk(const char id[4], size_t *length)
{
    size_t position = 12U;
    if (amg_banner_iff_size < position ||
        memcmp(amg_banner_iff, "FORM", 4U) ||
        memcmp(amg_banner_iff + 8U, "ILBM", 4U))
        return NULL;
    while (position + 8U <= amg_banner_iff_size) {
        size_t chunk_length =
            (size_t)banner_be32(amg_banner_iff + position + 4U);
        size_t data_position = position + 8U;
        if (chunk_length > amg_banner_iff_size - data_position) return NULL;
        if (!memcmp(amg_banner_iff + position, id, 4U)) {
            *length = chunk_length;
            return amg_banner_iff + data_position;
        }
        position = data_position + chunk_length + (chunk_length & 1U);
    }
    return NULL;
}

static void prepare_banner_pens(AmgGui *gui)
{
    const unsigned char *palette;
    size_t palette_length;
    size_t i;
    palette = banner_chunk("CMAP", &palette_length);
    for (i = 0; i < BANNER_COLOR_COUNT; ++i) {
        unsigned char red = 0x88U, green = 0x88U, blue = 0x88U;
        LONG pen;
        if (palette && i * 3U + 2U < palette_length) {
            red = palette[i * 3U];
            green = palette[i * 3U + 1U];
            blue = palette[i * 3U + 2U];
        }
        pen = ObtainBestPenA(
            gui->screen->ViewPort.ColorMap,
            (ULONG)red * 0x01010101UL,
            (ULONG)green * 0x01010101UL,
            (ULONG)blue * 0x01010101UL, NULL);
        if (pen >= 0) {
            gui->banner_pens[i] = pen;
            gui->banner_pen_owned[i] = 1U;
        } else {
            pen = FindColor(
                gui->screen->ViewPort.ColorMap,
                (ULONG)red * 0x01010101UL,
                (ULONG)green * 0x01010101UL,
                (ULONG)blue * 0x01010101UL, -1L);
            gui->banner_pens[i] =
                pen >= 0 ? pen : (LONG)gui->screen->BlockPen;
        }
    }
}

static void prepare_unread_pen(AmgGui *gui)
{
    LONG pen;
    if (!gui || !gui->screen) return;
    pen = ObtainBestPenA(
        gui->screen->ViewPort.ColorMap,
        0x00000000UL, 0x33333333UL, 0xffffffffUL, NULL);
    if (pen >= 0) {
        gui->unread_pen = pen;
        gui->unread_pen_owned = 1U;
        return;
    }
    pen = FindColor(gui->screen->ViewPort.ColorMap,
                    0x00000000UL, 0x33333333UL, 0xffffffffUL, -1L);
    gui->unread_pen = pen >= 0 ? pen : (LONG)gui->screen->DetailPen;
}

static void prepare_text_pen(AmgGui *gui)
{
    struct DrawInfo *draw_info;
    if (!gui || !gui->screen) return;
    gui->text_pen = (LONG)gui->screen->DetailPen;
    draw_info = GetScreenDrawInfo(gui->screen);
    if (draw_info) {
        gui->text_pen = (LONG)draw_info->dri_Pens[TEXTPEN];
        FreeScreenDrawInfo(gui->screen, draw_info);
    }
}

static void center_window_on_screen(struct Window *window)
{
    LONG left, top;
    if (!window || !window->WScreen) return;
    left = ((LONG)window->WScreen->Width - (LONG)window->Width) / 2L;
    top = ((LONG)window->WScreen->Height - (LONG)window->Height) / 2L;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    MoveWindow(window, left - window->LeftEdge, top - window->TopEdge);
}

static void draw_embedded_banner_at(AmgGui *gui, struct Window *window,
                                    LONG left, LONG top,
                                    LONG available_width, LONG available_height)
{
    const unsigned char *header, *body;
    size_t header_length, body_length;
    unsigned width, height, planes, row_bytes, draw_width, draw_height;
    unsigned x, y;
    struct RastPort *rastport;

    if (!gui || !window || available_width <= 0L || available_height <= 0L)
        return;
    rastport = window->RPort;
    if (!rastport) return;

    header = banner_chunk("BMHD", &header_length);
    body = banner_chunk("BODY", &body_length);
    if (!header || header_length < 20U || !body) return;
    width = banner_be16(header);
    height = banner_be16(header + 2U);
    planes = header[8U];
    if (!width || !height || planes != 3U || header[10U] != 0U) return;
    row_bytes = ((width + 15U) / 16U) * 2U;
    if ((size_t)row_bytes * planes * height > body_length) return;

    draw_width = width;
    if ((LONG)draw_width > available_width)
        draw_width = (unsigned)available_width;
    draw_height = height;
    if ((LONG)draw_height > available_height)
        draw_height = (unsigned)available_height;

    for (y = 0; y < draw_height; ++y) {
        const unsigned char *row = body + (size_t)y * row_bytes * planes;
        x = 0;
        while (x < draw_width) {
            unsigned run_start = x;
            unsigned color = 0;
            unsigned next_color;
            unsigned plane;
            for (plane = 0; plane < planes; ++plane)
                color |= ((row[plane * row_bytes + (x >> 3)] >>
                           (7U - (x & 7U))) & 1U) << plane;
            do {
                ++x;
                if (x >= draw_width) break;
                next_color = 0;
                for (plane = 0; plane < planes; ++plane)
                    next_color |= ((row[plane * row_bytes + (x >> 3)] >>
                                    (7U - (x & 7U))) & 1U) << plane;
            } while (next_color == color);
            SetAPen(rastport, (ULONG)gui->banner_pens[color]);
            RectFill(rastport, left + (LONG)run_start, top + (LONG)y,
                     left + (LONG)x - 1L, top + (LONG)y);
        }
    }
}

static void draw_banner(AmgGui *gui)
{
    LONG left, logo_left, top, right, bottom;
    struct RastPort *rastport;
    if (!gui || !gui->window) return;
    rastport = gui->window->RPort;
    if (!rastport) return;
    left = gui->window->BorderLeft;
    top = gui->window->BorderTop;
    right = (LONG)gui->window->Width - gui->window->BorderRight - 1L;
    bottom = top + 27L;
    if (right < left ||
        bottom >= (LONG)gui->window->Height - gui->window->BorderBottom)
        return;
    SetAPen(rastport, (ULONG)gui->banner_pens[0]);
    RectFill(rastport, left, top, right, bottom);

    /* Match the logo edge to the first toolbar button after layout. */
    logo_left = left;
    if (gui->new_mail_gadget) {
        LONG gadget_left = (LONG)gui->new_mail_gadget->LeftEdge;
        if (gadget_left >= left && gadget_left <= right)
            logo_left = gadget_left;
    }

    draw_embedded_banner_at(gui, gui->window, logo_left, top,
                            right - logo_left + 1L, 28L);
}

static void draw_message_flag_header(AmgGui *gui)
{
    struct RastPort *rp;
    struct TextFont *old_font;
    LONG gadget_left, gadget_top;
    LONG title_height, text_width, text_x, text_y;
    UBYTE old_fg, old_mode;
    static const char marker[] = "!";

    if (!gui || !gui->window || !gui->messages_gadget || !gui->screen) return;
    rp = gui->window->RPort;
    if (!rp || !gui->screen->RastPort.Font) return;

    gadget_left = (LONG)gui->messages_gadget->LeftEdge;
    gadget_top = (LONG)gui->messages_gadget->TopEdge;

    /* listbrowser.gadget uses the public screen font by default.  Temporarily
     * use that exact font here too, so the exclamation mark keeps precisely
     * the same normal glyph weight as the other native column titles.  Only
     * its x coordinate uses the native visual center of the first column. */
    old_font = rp->Font;
    SetFont(rp, gui->screen->RastPort.Font);

    title_height = (LONG)rp->TxHeight + 2L;
    text_width = TextLength(rp, (CONST_STRPTR)marker, 1UL);
    text_x = gadget_left + 2L +
        ((LONG)GUI_MESSAGE_FLAG_COLUMN_WIDTH - text_width) / 2L;
    text_y = gadget_top + 2L +
        (title_height - (LONG)rp->TxHeight) / 2L + (LONG)rp->TxBaseline;

    old_fg = rp->FgPen;
    old_mode = rp->DrawMode;
    SetDrMd(rp, JAM1);
    SetAPen(rp, (ULONG)(gui->text_pen >= 0 ? gui->text_pen : old_fg));
    Move(rp, text_x, text_y);
    Text(rp, (CONST_STRPTR)marker, 1UL);
    SetAPen(rp, old_fg);
    SetDrMd(rp, old_mode);
    if (old_font) SetFont(rp, old_font);
}

static void draw_window_overlays(AmgGui *gui)
{
    draw_banner(gui);
    sync_labels_scroller(gui);
    sync_messages_scroller(gui);
    sync_preview_scroller(gui, 0);
    draw_message_flag_header(gui);
}

static int create_window(AmgGui *gui, AmgError *error)
{
    Object *banner_row;

    gui->screen = LockPubScreen(NULL);
    if (!gui->screen) {
        amg_error_set(error, AMG_ERR_IO,
                      T("Workbench-Bildschirm konnte nicht gesperrt werden.", "Workbench screen could not be locked."));
        return AMG_ERR_IO;
    }
    prepare_banner_pens(gui);
    prepare_unread_pen(gui);
    prepare_text_pen(gui);
    init_preview_url_hook(gui);
    init_label_tree_render_hook(gui);
    init_compact_list_render_hooks(gui);
    /* default_labels() wird vor dem LockPubScreen() aufgebaut. Jetzt sind
     * Font und RenderHook bekannt, daher erzeugen wir nur die Label-Nodes
     * erneut; die Hierarchie- und Persistenzdaten selbst bleiben erhalten. */
    rebuild_label_lists(gui);
    if (!create_label_tree_images(gui)) {
        amg_error_set(error, AMG_ERR_MEMORY,
                      T("Label-Symbole konnten nicht angelegt werden.", "Label symbols could not be created."));
        return AMG_ERR_MEMORY;
    }

    banner_row = HGroupObject,
        LAYOUT_SpaceOuter, FALSE,
        LAYOUT_SpaceInner, FALSE,
    EndObject;
    if (!banner_row) {
        amg_error_set(error, AMG_ERR_MEMORY,
                      T("Grafikzeile konnte nicht angelegt werden.", "Banner row could not be created."));
        return AMG_ERR_MEMORY;
    }

    gui->columns = AllocLBColumnInfo(
        5,
        LBCIA_Column, 0,
        LBCIA_Title, (ULONG)(uintptr_t)"",
        LBCIA_Width, GUI_MESSAGE_FLAG_COLUMN_WIDTH,
        LBCIA_Flags, CIF_FIXED | CIF_CENTER,
        LBCIA_Column, 1,
        LBCIA_Title, (ULONG)(uintptr_t)T("Absender", "Sender"),
        LBCIA_Weight, 27,
        LBCIA_AutoSort, TRUE,
        LBCIA_SortArrow, TRUE,
        LBCIA_DraggableSeparator, TRUE,
        LBCIA_Column, 2,
        LBCIA_Title, (ULONG)(uintptr_t)T("Betreff", "Subject"),
        LBCIA_Weight, 37,
        LBCIA_AutoSort, TRUE,
        LBCIA_SortArrow, TRUE,
        LBCIA_DraggableSeparator, TRUE,
        LBCIA_Column, 3,
        LBCIA_Title, (ULONG)(uintptr_t)T("Datum", "Date"),
        LBCIA_Weight, 23,
        LBCIA_AutoSort, TRUE,
        LBCIA_SortArrow, TRUE,
        LBCIA_SortDirection, LBMSORT_REVERSE,
        LBCIA_DraggableSeparator, TRUE,
        LBCIA_Column, 4,
        LBCIA_Title, (ULONG)(uintptr_t)T("Gr\366\337e", "Size"),
        LBCIA_Weight, 13,
        LBCIA_AutoSort, TRUE,
        LBCIA_SortArrow, TRUE,
        TAG_DONE);
    if (!gui->columns) {
        amg_error_set(error, AMG_ERR_MEMORY,
                      T("Spalten konnten nicht angelegt werden.", "Columns could not be created."));
        return AMG_ERR_MEMORY;
    }

    gui->labels_scroller = create_vertical_scroller(GID_LABELS_SCROLL);
    gui->messages_scroller = create_vertical_scroller(GID_MESSAGES_SCROLL);
    gui->preview_scroller = create_vertical_scroller(GID_PREVIEW_SCROLL);
    if (!gui->labels_scroller || !gui->messages_scroller ||
        !gui->preview_scroller) {
        if (gui->preview_scroller) DisposeObject((Object *)gui->preview_scroller);
        if (gui->messages_scroller) DisposeObject((Object *)gui->messages_scroller);
        if (gui->labels_scroller) DisposeObject((Object *)gui->labels_scroller);
        gui->preview_scroller = NULL;
        gui->messages_scroller = NULL;
        gui->labels_scroller = NULL;
        FreeLBColumnInfo(gui->columns);
        gui->columns = NULL;
        amg_error_set(error, AMG_ERR_MEMORY,
                      T("Scrollbar konnte nicht angelegt werden.", "Scrollbar could not be created."));
        return AMG_ERR_MEMORY;
    }

    gui->window_object = WindowObject,
        WA_Title, "AmiGmail",
        WA_Flags, WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                      WFLG_SIZEGADGET | WFLG_ACTIVATE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_MENUPICK |
                      IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW,
        WA_Width, 720,
        WA_Height, 480,
        WA_MinWidth, 620,
        WA_MinHeight, 320,
        WA_PubScreen, gui->screen,
        WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_NewMenu, amg_i18n_is_german() ? menus_de : menus_en,
        WINDOW_ParentGroup, VGroupObject,
            LAYOUT_SpaceOuter, FALSE,
            LAYOUT_SpaceInner, FALSE,
            LAYOUT_FillPen, gui->banner_pens[0],
            LAYOUT_FillPattern, (ULONG)(uintptr_t)solid_fill_pattern,

            LAYOUT_AddChild, banner_row,
            CHILD_MinHeight, 28,
            CHILD_MaxHeight, 28,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, VGroupObject,
                LAYOUT_SpaceOuter, TRUE,
                LAYOUT_SpaceInner, TRUE,

                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_EvenSize, TRUE,
                    LAYOUT_AddChild,
                        gui->new_mail_gadget = (struct Gadget *)ButtonObject,
                        GA_ID, GID_NEW_MAIL,
                        GA_RelVerify, TRUE,
                        GA_Text, T("_Neue Mail", "_New mail"),
                    EndObject,
                    CHILD_MinWidth, 92,
                    LAYOUT_AddChild, ButtonObject,
                        GA_ID, GID_FETCH,
                        GA_RelVerify, TRUE,
                        GA_Text, T("_Abrufen", "_Fetch"),
                    EndObject,
                    CHILD_MinWidth, 92,
                    LAYOUT_AddChild, ButtonObject,
                        GA_ID, GID_REPLY,
                        GA_RelVerify, TRUE,
                        GA_Text, T("A_ntworten", "_Reply"),
                    EndObject,
                    CHILD_MinWidth, 92,
                    LAYOUT_AddChild, ButtonObject,
                        GA_ID, GID_DELETE,
                        GA_RelVerify, TRUE,
                        GA_Text, T("_L\366schen", "_Delete"),
                    EndObject,
                    CHILD_MinWidth, 92,
                    LAYOUT_AddChild, ButtonObject,
                        GA_ID, GID_MOVE,
                        GA_RelVerify, TRUE,
                        GA_Text, T("_Verschieben", "_Move"),
                    EndObject,
                    CHILD_MinWidth, 92,
                    LAYOUT_AddChild, ButtonObject,
                        GA_ID, GID_SEEN,
                        GA_RelVerify, TRUE,
                        GA_Text, T("_Un/Gelesen", "_Read/Unread"),
                    EndObject,
                    CHILD_MinWidth, 92,
                EndObject,
                CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_AddChild,
                    VGroupObject,
                        LAYOUT_SpaceOuter, FALSE,
                        LAYOUT_SpaceInner, FALSE,

                        LAYOUT_AddChild, HGroupObject,
                            LAYOUT_SpaceOuter, FALSE,
                            LAYOUT_SpaceInner, FALSE,
                            LAYOUT_AddChild,
                                gui->system_labels_gadget =
                                    (struct Gadget *)ListBrowserObject,
                                    GA_ID, GID_SYSTEM_LABELS,
                                    GA_RelVerify, TRUE,
                                    LISTBROWSER_Labels,
                                        &gui->system_labels_list,
                                    LISTBROWSER_ShowSelected, TRUE,
                                    LISTBROWSER_VerticalProp, FALSE,
                                    LISTBROWSER_Spacing, 1,
                                    LISTBROWSER_MinVisible,
                                        GUI_SYSTEM_LABEL_VISIBLE_COUNT,
                                EndObject,
                            LAYOUT_AddChild, HGroupObject,
                                LAYOUT_SpaceOuter, FALSE,
                                LAYOUT_SpaceInner, FALSE,
                            EndObject,
                            CHILD_MinWidth, GUI_SCROLLBAR_WIDTH,
                            CHILD_MaxWidth, GUI_SCROLLBAR_WIDTH,
                            CHILD_WeightedWidth, 0,
                        EndObject,
                        CHILD_WeightedHeight, 0,

                        LAYOUT_AddChild, HGroupObject,
                            LAYOUT_SpaceOuter, FALSE,
                            LAYOUT_SpaceInner, FALSE,
                        EndObject,
                        CHILD_MinHeight, 3,
                        CHILD_MaxHeight, 3,
                        CHILD_WeightedHeight, 0,

                        LAYOUT_AddChild, HGroupObject,
                            LAYOUT_SpaceOuter, FALSE,
                            LAYOUT_SpaceInner, FALSE,
                            LAYOUT_AddChild,
                                gui->labels_gadget =
                                    (struct Gadget *)ListBrowserObject,
                                    GA_ID, GID_LABELS,
                                    GA_RelVerify, TRUE,
                                    LISTBROWSER_Labels, &gui->labels_list,
                                    LISTBROWSER_ShowSelected, TRUE,
                                    LISTBROWSER_Hierarchical, TRUE,
                                    /* Kleines [+] = geschlossen /
                                     * aufklappen, kleines [-] = offen /
                                     * zuklappen. Eigene klassische Images
                                     * verhindern die grossen nativen Pfeile. */
                                    LISTBROWSER_ShowImage,
                                        &gui->label_show_image,
                                    LISTBROWSER_HideImage,
                                        &gui->label_hide_image,
                                    LISTBROWSER_VerticalProp, FALSE,
                                    LISTBROWSER_Spacing, 1,
                                EndObject,
                            LAYOUT_AddChild, gui->labels_scroller,
                            CHILD_MinWidth, GUI_SCROLLBAR_WIDTH,
                            CHILD_MaxWidth, GUI_SCROLLBAR_WIDTH,
                            CHILD_WeightedWidth, 0,
                        EndObject,
                        CHILD_WeightedHeight, 100,
                    EndObject,
                CHILD_WeightedWidth, 25,
                CHILD_MinWidth, 120,

                LAYOUT_AddChild, VGroupObject,
                    LAYOUT_AddChild, HGroupObject,
                        LAYOUT_SpaceOuter, FALSE,
                        LAYOUT_SpaceInner, FALSE,
                        LAYOUT_AddChild,
                            gui->messages_gadget =
                                (struct Gadget *)ListBrowserObject,
                                GA_ID, GID_MESSAGES,
                                GA_RelVerify, TRUE,
                                LISTBROWSER_Labels, &gui->messages_list,
                                LISTBROWSER_ColumnInfo, gui->columns,
                                LISTBROWSER_ColumnTitles, TRUE,
                                LISTBROWSER_TitleClickable, TRUE,
                                LISTBROWSER_SortColumn, 3,
                                LISTBROWSER_MultiSelect, TRUE,
                                LISTBROWSER_ShowSelected, TRUE,
                                LISTBROWSER_VerticalProp, FALSE,
                                LISTBROWSER_WrapText, TRUE,
                                LISTBROWSER_Spacing, 1,
                                /* Kein ScrollRaster-Optimierungsweg: auf
                                 * einigen klassischen Intuition/Layer-Setups
                                 * bleiben sonst beim Scrollen einzelne
                                 * Pixelzeilen des vorherigen Inhalts stehen. */
                                LISTBROWSER_ScrollRaster, FALSE,
                            EndObject,
                        LAYOUT_AddChild, gui->messages_scroller,
                        CHILD_MinWidth, GUI_SCROLLBAR_WIDTH,
                        CHILD_MaxWidth, GUI_SCROLLBAR_WIDTH,
                        CHILD_WeightedWidth, 0,
                    EndObject,
                    CHILD_WeightedHeight, 55,
                    CHILD_MinWidth, 250,

                    LAYOUT_AddChild, VGroupObject,
                        LAYOUT_SpaceOuter, FALSE,
                        LAYOUT_SpaceInner, TRUE,
                        LAYOUT_AddChild, HGroupObject,
                            LAYOUT_SpaceOuter, FALSE,
                            LAYOUT_SpaceInner, FALSE,
                            LAYOUT_AddChild,
                                gui->preview_gadget =
                                    (struct Gadget *)TextEditorObject,
                                    GA_ID, GID_PREVIEW,
                                    GA_ReadOnly, TRUE,
                                    GA_TEXTEDITOR_ReadOnly, TRUE,
                                    GA_TEXTEDITOR_DoubleClickHook,
                                        &gui->preview_url_hook,
                                    GA_TEXTEDITOR_Contents,
                                        T("W\344hlen Sie nach dem Abruf eine Nachricht aus.", "Select a message after fetching."),
                                EndObject,
                            LAYOUT_AddChild, gui->preview_scroller,
                            CHILD_MinWidth, GUI_SCROLLBAR_WIDTH,
                            CHILD_MaxWidth, GUI_SCROLLBAR_WIDTH,
                            CHILD_WeightedWidth, 0,
                        EndObject,
                        CHILD_WeightedHeight, 100,
                        LAYOUT_AddChild, HGroupObject,
                            LAYOUT_SpaceOuter, FALSE,
                            LAYOUT_AddChild, HGroupObject,
                                LAYOUT_SpaceOuter, FALSE,
                                LAYOUT_SpaceInner, FALSE,
                            EndObject,
                            CHILD_WeightedWidth, 100,
                            LAYOUT_AddChild,
                                gui->save_attachments_gadget =
                                    (struct Gadget *)ButtonObject,
                                    GA_ID, GID_SAVE_ATTACHMENTS,
                                    GA_RelVerify, TRUE,
                                    GA_Disabled, TRUE,
                                    GA_Text, T("_Anh\344nge speichern...", "Save _attachments..."),
                                EndObject,
                            CHILD_WeightedWidth, 0,
                        EndObject,
                        CHILD_WeightedHeight, 0,
                    EndObject,
                    CHILD_WeightedHeight, 45,
                    CHILD_MinWidth, 250,
                EndObject,
                CHILD_WeightedWidth, 75,
                CHILD_MinWidth, 250,
            EndObject,

                LAYOUT_AddChild,
                    gui->status_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_STATUS,
                        GA_ReadOnly, TRUE,
                        STRINGA_TextVal, T("Bereit", "Ready"),
                    EndObject,
                CHILD_WeightedHeight, 0,
            EndObject,
        EndObject,
    EndWindow;

    if (!gui->window_object) {
        FreeLBColumnInfo(gui->columns);
        gui->columns = NULL;
        gui->labels_scroller = NULL;
        gui->messages_scroller = NULL;
        gui->preview_scroller = NULL;
        amg_error_set(error, AMG_ERR_MEMORY,
                      "ReAction-Fenster konnte nicht erzeugt werden.");
        return AMG_ERR_MEMORY;
    }
    return AMG_OK;
}

AmgGui *amg_gui_create(AmgAccount *account, AmgError *error)
{
    AmgGui *gui;
    if (!account) return NULL;
    if (!open_classes()) {
        close_classes();
        amg_error_set(
            error, AMG_ERR_UNSUPPORTED,
            T("Erforderliche ReAction-Klassen fehlen. AmiGmail ben\303\266tigt AmigaOS 3.2.", "Required ReAction classes are missing. AmiGmail requires AmigaOS 3.2."));
        return NULL;
    }
    gui = (AmgGui *)calloc(1, sizeof(*gui));
    if (!gui) {
        close_classes();
        return NULL;
    }
    gui->account = account;
    NewList(&gui->system_labels_list);
    NewList(&gui->labels_list);
    NewList(&gui->messages_list);
    default_labels(gui);
    default_messages(gui);
    gui->network = amg_network_create();
    if (!gui->network || create_window(gui, error) != AMG_OK) {
        amg_gui_destroy(gui);
        return NULL;
    }
    return gui;
}

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

static int account_is_locked(const AmgAccount *account)
{
    if (!account || !account->email[0]) return 1;
    if (account->auth_mode == AMG_AUTH_OAUTH2)
        return !account->refresh_token;
    return !account->app_password;
}

static void replace_account(AmgGui *gui, AmgAccount *replacement)
{
    amg_network_stop(gui->network);
    amg_account_clear(gui->account);
    *gui->account = *replacement;
    replacement->app_password = NULL;
    replacement->refresh_token = NULL;
}

static int account_dialog(AmgGui *gui, AmgError *error)
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
    int restart_network = amg_network_is_running(gui->network);

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
                switch (result & WMHI_CLASSMASK) {
                    case WMHI_CLOSEWINDOW:
                        done = 1;
                        break;

                    case WMHI_RAWKEY:
                        if ((result & WMHI_KEYMASK) == 0x45UL) done = 1;
                        break;

                    case WMHI_GADGETUP:
                        switch (result & WMHI_GADGETMASK) {
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
                        break;
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
        if (result == AMG_OK)
            result = amg_network_request(gui->network, AMG_NET_CONNECT, 0,
                                         NULL, NULL, error);
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

static void about_dialog(AmgGui *gui)
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
                        if ((result & WMHI_KEYMASK) == 0x45UL) done = 1;
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

static int selected_file_size(const char *path, unsigned long *size)
{
    FILE *file = fopen(path, "rb");
    long length;
    if (!file) return AMG_ERR_IO;
    if (fseek(file, 0L, SEEK_END) != 0 || (length = ftell(file)) < 0) {
        fclose(file);
        return AMG_ERR_IO;
    }
    fclose(file);
    *size = (unsigned long)length;
    return AMG_OK;
}

static unsigned long attachment_total(const ComposeAttachment *attachments,
                                      size_t count)
{
    size_t i;
    unsigned long total = 0;
    for (i = 0; i < count; ++i) total += attachments[i].size;
    return total;
}

static void rebuild_attachment_list(struct Gadget *list_gadget,
                                    struct Window *window, struct List *list,
                                    const ComposeAttachment *attachments,
                                    size_t count)
{
    size_t i;
    SetGadgetAttrs(list_gadget, window, NULL,
                   LISTBROWSER_Labels, (ULONG)~0UL,
                   TAG_DONE);
    FreeListBrowserList(list);
    NewList(list);
    for (i = 0; i < count; ++i) {
        char line[384];
        struct Node *node;
        snprintf(line, sizeof(line), "%s (%lu KB)",
                 attachments[i].name_local,
                 (attachments[i].size + 1023UL) / 1024UL);
        node = one_column_node(line, (ULONG)i);
        if (node) AddTail(list, node);
    }
    SetGadgetAttrs(list_gadget, window, NULL,
                   LISTBROWSER_Labels, (ULONG)(uintptr_t)list,
                   TAG_DONE);
}

static void update_compose_status(struct Gadget *status_gadget,
                                  struct Window *window,
                                  const ComposeAttachment *attachments,
                                  size_t count)
{
    char text[160];
    unsigned long total = attachment_total(attachments, count);
    amg_tr_snprintf(text, sizeof(text),
                    "%lu Anlage(n), %lu KB von 10240 KB",
                    "%lu attachment(s), %lu KB of 10240 KB",
                    (unsigned long)count, (total + 1023UL) / 1024UL);
    set_string(status_gadget, window, text);
}

static int add_attachment(struct Window *window, struct Gadget *list_gadget,
                          struct Gadget *status_gadget, struct List *list,
                          ComposeAttachment *attachments, size_t *count)
{
    struct FileRequester *request;
    char path[COMPOSE_PATH_MAX];
    unsigned long size, total;
    size_t i;
    if (*count >= AMG_MAIL_MAX_ATTACHMENTS) {
        set_string(status_gadget, window,
                   T("H\366chstens 8 Anlagen sind m\366glich.", "A maximum of 8 attachments is allowed."));
        return 0;
    }
    request = (struct FileRequester *)AllocAslRequestTags(
        ASL_FileRequest,
        ASLFR_TitleText, (ULONG)(uintptr_t)T("Dateianlage ausw\344hlen", "Select attachment"),
        ASLFR_Window, (ULONG)(uintptr_t)window,
        ASLFR_SleepWindow, TRUE,
        ASLFR_RejectIcons, TRUE,
        TAG_DONE);
    if (!request) {
        set_string(status_gadget, window,
                   T("Dateiauswahl konnte nicht ge\366ffnet werden.", "File requester could not be opened."));
        return 0;
    }
    if (!AslRequest(request, NULL)) {
        FreeAslRequest(request);
        return 0;
    }
    strncpy(path, request->rf_Dir ? (const char *)request->rf_Dir : "",
            sizeof(path) - 1U);
    path[sizeof(path) - 1U] = 0;
    if (!AddPart((STRPTR)path, request->rf_File,
                 (LONG)sizeof(path))) {
        FreeAslRequest(request);
        set_string(status_gadget, window, T("Dateipfad ist zu lang.", "File path is too long."));
        return 0;
    }
    for (i = 0; i < *count; ++i) {
        if (!strcmp(attachments[i].path, path)) {
            FreeAslRequest(request);
            set_string(status_gadget, window,
                       T("Diese Datei ist bereits angeh\344ngt.", "This file is already attached."));
            return 0;
        }
    }
    if (selected_file_size(path, &size) != AMG_OK) {
        FreeAslRequest(request);
        set_string(status_gadget, window,
                   T("Datei konnte nicht gelesen werden.", "File could not be read."));
        return 0;
    }
    total = attachment_total(attachments, *count);
    if (size > AMG_MAIL_MAX_ATTACHMENT_TOTAL - total) {
        FreeAslRequest(request);
        set_string(status_gadget, window,
                   T("Anlagen d\374rfen zusammen h\366chstens 10 MB gro\337 sein.", "Attachments may total no more than 10 MB."));
        return 0;
    }
    strncpy(attachments[*count].path, path,
            sizeof(attachments[*count].path) - 1U);
    attachments[*count].path[sizeof(attachments[*count].path) - 1U] = 0;
    strncpy(attachments[*count].name_local,
            request->rf_File ? (const char *)request->rf_File : "attachment.bin",
            sizeof(attachments[*count].name_local) - 1U);
    attachments[*count].name_local[
        sizeof(attachments[*count].name_local) - 1U] = 0;
    if (local_to_utf8(attachments[*count].name_local,
                      attachments[*count].name_utf8,
                      sizeof(attachments[*count].name_utf8)) != AMG_OK)
        strcpy(attachments[*count].name_utf8, "attachment.bin");
    attachments[*count].size = size;
    ++*count;
    FreeAslRequest(request);
    rebuild_attachment_list(list_gadget, window, list, attachments, *count);
    update_compose_status(status_gadget, window, attachments, *count);
    return 1;
}

static void remove_attachment(struct Window *window, struct Gadget *list_gadget,
                              struct Gadget *status_gadget, struct List *list,
                              ComposeAttachment *attachments, size_t *count)
{
    ULONG selected = (ULONG)~0UL;
    size_t i;
    GetAttr(LISTBROWSER_Selected, (Object *)list_gadget, &selected);
    if (selected == (ULONG)~0UL || selected >= *count) {
        set_string(status_gadget, window,
                   T("Bitte zuerst eine Anlage ausw\344hlen.", "Please select an attachment first."));
        return;
    }
    for (i = (size_t)selected; i + 1U < *count; ++i)
        attachments[i] = attachments[i + 1U];
    --*count;
    rebuild_attachment_list(list_gadget, window, list, attachments, *count);
    update_compose_status(status_gadget, window, attachments, *count);
}

static int is_leap_year(unsigned long year)
{
    return (year % 4UL == 0UL && year % 100UL != 0UL) ||
           year % 400UL == 0UL;
}

static unsigned long days_in_year(unsigned long year)
{
    return is_leap_year(year) ? 366UL : 365UL;
}

static int make_date_and_message_id(char date[96], char message_id[256])
{
    static const char *weekdays[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char *months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    static const unsigned char month_lengths[] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };
    static unsigned long sequence = 0UL;
    struct DateStamp stamp;
    unsigned long remaining_days, year, month, day;
    unsigned long hour, minute, second, weekday;
    int length;

    DateStamp(&stamp);
    if (stamp.ds_Days < 0 || stamp.ds_Minute < 0 || stamp.ds_Tick < 0)
        return AMG_ERR_IO;

    remaining_days = (unsigned long)stamp.ds_Days;
    weekday = remaining_days % 7UL; /* 1 January 1978 was a Sunday. */
    year = 1978UL;
    while (remaining_days >= days_in_year(year)) {
        remaining_days -= days_in_year(year);
        ++year;
    }
    month = 0UL;
    while (month < 11UL) {
        unsigned long length_of_month = month_lengths[month];
        if (month == 1UL && is_leap_year(year)) ++length_of_month;
        if (remaining_days < length_of_month) break;
        remaining_days -= length_of_month;
        ++month;
    }
    day = remaining_days + 1UL;
    hour = (unsigned long)stamp.ds_Minute / 60UL;
    minute = (unsigned long)stamp.ds_Minute % 60UL;
    second = (unsigned long)stamp.ds_Tick / 50UL;
    if (second > 59UL) second = 59UL;

    /* DateStamp contains local time. -0000 denotes an unknown local offset. */
    length = snprintf(date, 96U,
                      "%s, %02lu %s %04lu %02lu:%02lu:%02lu -0000",
                      weekdays[weekday], day, months[month], year,
                      hour, minute, second);
    if (length < 0 || length >= 96) return AMG_ERR_IO;
    ++sequence;
    length = snprintf(message_id, 256U, "<%08lx.%04lx.%04lx.%04lx@amigmail.local>",
                      (unsigned long)stamp.ds_Days,
                      (unsigned long)stamp.ds_Minute,
                      (unsigned long)stamp.ds_Tick, sequence);
    if (length < 0 || length >= 256) return AMG_ERR_IO;
    return AMG_OK;
}

static int queue_composed_mail(AmgGui *gui, struct Window *window,
                               struct Gadget *to_gadget,
                               struct Gadget *cc_gadget,
                               struct Gadget *bcc_gadget,
                               struct Gadget *subject_gadget,
                               struct Gadget *body_gadget,
                               const ComposeAttachment *attachments,
                               size_t attachment_count, int reply_mode,
                               int save_as_draft, AmgError *error)
{
    char to_utf8[1536], cc_utf8[1536], bcc_utf8[1536];
    char subject_utf8[1024];
    char date[96], message_id[256];
    AmgBuffer body_utf8;
    AmgAttachmentInput inputs[AMG_MAIL_MAX_ATTACHMENTS];
    AmgMailDraft draft;
    const unsigned char *p;
    STRPTR body_local = NULL;
    size_t i;
    int result = AMG_OK;
    if (!save_as_draft && !*string_text(to_gadget) &&
        !*string_text(cc_gadget) && !*string_text(bcc_gadget)) {
        amg_error_set(error, AMG_ERR_ARGUMENT,
                      T("Mindestens ein Empf\303\244nger fehlt.", "At least one recipient is required."));
        return AMG_ERR_ARGUMENT;
    }
    if (local_to_utf8(string_text(to_gadget), to_utf8,
                      sizeof(to_utf8)) != AMG_OK ||
        local_to_utf8(string_text(cc_gadget), cc_utf8,
                      sizeof(cc_utf8)) != AMG_OK ||
        local_to_utf8(string_text(bcc_gadget), bcc_utf8,
                      sizeof(bcc_utf8)) != AMG_OK ||
        local_to_utf8(string_text(subject_gadget), subject_utf8,
                      sizeof(subject_utf8)) != AMG_OK) {
        amg_error_set(error, AMG_ERR_LIMIT, T("Eine Kopfzeile ist zu lang.", "A header line is too long."));
        return AMG_ERR_LIMIT;
    }
    body_local = (STRPTR)(uintptr_t)DoGadgetMethod(
        body_gadget, window, NULL, GM_TEXTEDITOR_ExportText, 0UL);
    if (!body_local) {
        amg_error_set(error, AMG_ERR_MEMORY,
                      T("Nachrichtentext konnte nicht gelesen werden.", "Message text could not be read."));
        return AMG_ERR_MEMORY;
    }
    amg_buffer_init(&body_utf8);
    p = (const unsigned char *)body_local;
    while (*p && result == AMG_OK) {
        unsigned char bytes[2];
        if (*p < 0x80U) {
            result = amg_buffer_append_char(&body_utf8, *p);
        } else {
            bytes[0] = (unsigned char)(0xC0U | (*p >> 6));
            bytes[1] = (unsigned char)(0x80U | (*p & 0x3FU));
            result = amg_buffer_append(&body_utf8, bytes, 2U);
        }
        ++p;
    }
    if (result == AMG_OK) result = amg_buffer_terminate(&body_utf8);
    if (result != AMG_OK) {
        amg_buffer_free(&body_utf8);
        FreeVec(body_local);
        amg_error_set(error, result, T("Mailtext ist zu gro\303\237.", "Mail text is too large."));
        return result;
    }
    for (i = 0; i < attachment_count; ++i) {
        inputs[i].path = attachments[i].path;
        inputs[i].name_utf8 = attachments[i].name_utf8;
        inputs[i].size = attachments[i].size;
    }
    if (make_date_and_message_id(date, message_id) != AMG_OK) {
        amg_buffer_free(&body_utf8);
        FreeVec(body_local);
        amg_error_set(error, AMG_ERR_IO,
                      T("Datum der Nachricht konnte nicht erzeugt werden.", "Message date could not be generated."));
        return AMG_ERR_IO;
    }
    draft.from = gui->account->email;
    draft.to = to_utf8;
    draft.cc = cc_utf8;
    draft.bcc = bcc_utf8;
    draft.subject = subject_utf8;
    draft.body_utf8 = (const char *)body_utf8.data;
    draft.date_rfc2822 = date;
    draft.message_id = message_id;
    draft.in_reply_to = reply_mode
        ? gui->reply_in_reply_to_utf8 : NULL;
    draft.references = reply_mode
        ? gui->reply_references_utf8 : NULL;
    draft.attachments = inputs;
    draft.attachment_count = attachment_count;

    if (!amg_network_is_running(gui->network)) {
        result = amg_network_start(gui->network, gui->account, error);
        if (result == AMG_OK)
            result = amg_network_request(gui->network, AMG_NET_CONNECT, 0,
                                         NULL, NULL, error);
    } else if (save_as_draft &&
               !amg_network_is_connected(gui->network)) {
        result = amg_network_request(gui->network, AMG_NET_CONNECT, 0,
                                     NULL, NULL, error);
    }
    if (result == AMG_OK) {
        if (save_as_draft) {
            const char *draft_mailbox = gui->labels[3U].available
                ? gui->labels[3U].gmail_label_utf8 : "\\Drafts";
            result = amg_network_request_draft(
                gui->network, &draft, draft_mailbox, error);
        } else {
            result = amg_network_request_mail(gui->network, &draft, error);
        }
    }
    amg_buffer_free(&body_utf8);
    FreeVec(body_local);
    return result;
}

static int compose_dialog(AmgGui *gui, int reply_mode, AmgError *error)
{
    Object *dialog;
    struct Window *window;
    struct Gadget *to_gadget, *cc_gadget, *bcc_gadget, *subject_gadget;
    struct Gadget *body_gadget, *body_scroller;
    struct Gadget *attachments_gadget, *attachments_scroller;
    struct Gadget *compose_status;
    struct List attachment_list;
    ComposeAttachment attachments[AMG_MAIL_MAX_ATTACHMENTS];
    size_t attachment_count = 0;
    ULONG signal_mask, compose_width = 600UL, compose_height = 400UL;
    ULONG compose_left = 0UL, compose_top = 0UL;
    int done = 0, queued = 0;

    memset(attachments, 0, sizeof(attachments));
    NewList(&attachment_list);
    to_gadget = cc_gadget = bcc_gadget = subject_gadget = NULL;
    body_gadget = body_scroller = NULL;
    attachments_gadget = attachments_scroller = compose_status = NULL;
    if (gui->screen && (ULONG)gui->screen->Width > 40UL) {
        ULONG available_width = (ULONG)gui->screen->Width - 20UL;
        if (compose_width > available_width) compose_width = available_width;
    }
    if (compose_width < 440UL) compose_width = 440UL;
    if (gui->screen && (ULONG)gui->screen->Height > 40UL) {
        ULONG available_height = (ULONG)gui->screen->Height - 20UL;
        if (compose_height > available_height) compose_height = available_height;
    }
    if (compose_height < 300UL) compose_height = 300UL;
    if (gui->screen) {
        if ((ULONG)gui->screen->Width > compose_width)
            compose_left = ((ULONG)gui->screen->Width - compose_width) / 2UL;
        if ((ULONG)gui->screen->Height > compose_height)
            compose_top = ((ULONG)gui->screen->Height - compose_height) / 2UL;
    }

    body_scroller = create_vertical_scroller(GID_COMPOSE_BODY_SCROLL);
    attachments_scroller =
        create_vertical_scroller(GID_COMPOSE_ATTACHMENTS_SCROLL);
    if (!body_scroller || !attachments_scroller) {
        if (attachments_scroller)
            DisposeObject((Object *)attachments_scroller);
        if (body_scroller) DisposeObject((Object *)body_scroller);
        amg_error_set(error, AMG_ERR_MEMORY,
                      T("Scrollbar konnte nicht angelegt werden.", "Scrollbar could not be created."));
        return 0;
    }

    dialog = WindowObject,
        WA_Title, reply_mode ? T("AmiGmail - Antworten", "AmiGmail - Reply") :
                               T("AmiGmail - Neue Mail", "AmiGmail - New mail"),
        WA_Flags, WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                      WFLG_SIZEGADGET | WFLG_ACTIVATE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_RAWKEY,
        WA_PubScreen, gui->screen,
        WA_Left, compose_left,
        WA_Top, compose_top,
        WA_Width, compose_width,
        WA_Height, compose_height,
        WA_MinWidth, 440,
        WA_MinHeight, 300,
        WINDOW_ParentGroup, VGroupObject,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_SpaceInner, TRUE,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T("An:", "To:")),
                CHILD_MinWidth, 70,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    to_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_COMPOSE_TO,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 767,
                        STRINGA_TextVal,
                            (ULONG)(uintptr_t)(reply_mode
                                ? gui->reply_to_local : ""),
                    EndObject,
                EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label("CC:"),
                CHILD_MinWidth, 70,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    cc_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_COMPOSE_CC,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 767,
                    EndObject,
                EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label("BCC:"),
                CHILD_MinWidth, 70,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    bcc_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_COMPOSE_BCC,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 767,
                    EndObject,
                EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T("Betreff:", "Subject:")),
                CHILD_MinWidth, 70,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    subject_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_COMPOSE_SUBJECT,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 511,
                        STRINGA_TextVal,
                            (ULONG)(uintptr_t)(reply_mode
                                ? gui->reply_subject_local : ""),
                    EndObject,
                EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T("Nachricht:", "Message:")),
                CHILD_MinWidth, 70,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                    LAYOUT_AddChild,
                        body_gadget = (struct Gadget *)TextEditorObject,
                            GA_ID, GID_COMPOSE_BODY,
                            GA_TabCycle, TRUE,
                            GA_TEXTEDITOR_Contents,
                                (ULONG)(uintptr_t)(reply_mode
                                    ? gui->reply_body_local : ""),
                            GA_TEXTEDITOR_TabSize, 4,
                            GA_TEXTEDITOR_IndentWidth, 4,
                            GA_TEXTEDITOR_TabKeyPolicy,
                                GV_TEXTEDITOR_TabKey_IndentsAfter,
                        EndObject,
                    LAYOUT_AddChild, body_scroller,
                    CHILD_MinWidth, GUI_SCROLLBAR_WIDTH,
                    CHILD_MaxWidth, GUI_SCROLLBAR_WIDTH,
                    CHILD_WeightedWidth, 0,
                EndObject,
                CHILD_MinHeight, 100,
                CHILD_WeightedHeight, 100,
            EndObject,
            CHILD_WeightedHeight, 65,
            CHILD_MinHeight, 100,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T("Anlagen:", "Attachments:")),
                CHILD_MinWidth, 70,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_AddChild, HGroupObject,
                        LAYOUT_SpaceOuter, FALSE,
                        LAYOUT_SpaceInner, FALSE,
                        LAYOUT_AddChild,
                            attachments_gadget =
                                (struct Gadget *)ListBrowserObject,
                                GA_ID, GID_COMPOSE_ATTACHMENTS,
                                GA_RelVerify, TRUE,
                                LISTBROWSER_Labels, &attachment_list,
                                LISTBROWSER_ShowSelected, TRUE,
                                LISTBROWSER_VerticalProp, FALSE,
                            EndObject,
                        LAYOUT_AddChild, attachments_scroller,
                        CHILD_MinWidth, GUI_SCROLLBAR_WIDTH,
                        CHILD_MaxWidth, GUI_SCROLLBAR_WIDTH,
                        CHILD_WeightedWidth, 0,
                    EndObject,
                    CHILD_WeightedWidth, 75,
                    CHILD_MinHeight, 45,

                    LAYOUT_AddChild, VGroupObject,
                        LAYOUT_AddChild, ButtonObject,
                            GA_ID, GID_COMPOSE_ADD_ATTACHMENT,
                            GA_RelVerify, TRUE,
                            GA_Text, T("_Anlage...", "_Attachment..."),
                        EndObject,
                        LAYOUT_AddChild, ButtonObject,
                            GA_ID, GID_COMPOSE_REMOVE_ATTACHMENT,
                            GA_RelVerify, TRUE,
                            GA_Text, T("Ent_fernen", "_Remove"),
                        EndObject,
                    EndObject,
                    CHILD_WeightedWidth, 25,
                EndObject,
            EndObject,
            CHILD_WeightedHeight, 25,

            LAYOUT_AddChild,
                compose_status = (struct Gadget *)StringObject,
                    GA_ID, GID_COMPOSE_STATUS,
                    GA_ReadOnly, TRUE,
                    STRINGA_TextVal, T("0 Anlagen, 0 KB von 10240 KB", "0 attachments, 0 KB of 10240 KB"),
                EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_COMPOSE_SEND,
                    GA_RelVerify, TRUE,
                    GA_Text, T("_Senden", "_Send"),
                EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_COMPOSE_CANCEL,
                    GA_RelVerify, TRUE,
                    GA_Text, T("Ab_brechen", "_Cancel"),
                EndObject,
            EndObject,
            CHILD_WeightedHeight, 0,
        EndObject,
    EndWindow;

    if (!dialog) {
        amg_error_set(error, AMG_ERR_MEMORY,
                      T("Fenster f\303\274r neue Mail konnte nicht erzeugt werden.", "New mail window could not be created."));
        return 0;
    }
    window = RA_OpenWindow(dialog);
    if (!window) {
        DisposeObject(dialog);
        amg_error_set(error, AMG_ERR_IO,
                      T("Fenster f\303\274r neue Mail konnte nicht ge\303\266ffnet werden.", "New mail window could not be opened."));
        return 0;
    }
    GetAttr(WINDOW_SigMask, dialog, &signal_mask);
    sync_texteditor_scroller(window, body_gadget, body_scroller, 0, 0);
    sync_listbrowser_scroller(window, attachments_gadget,
                              attachments_scroller);

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
                        if ((result & WMHI_KEYMASK) == 0x45UL) done = 1;
                        break;

                    case WMHI_GADGETUP:
                        switch (result & WMHI_GADGETMASK) {
                            case GID_COMPOSE_CANCEL:
                                if (confirm_question_dialog_for_window(
                                        gui, window,
                                        T("Wollen Sie den Entwurf speichern?",
                                          "Do you want to save the draft?"),
                                        NULL, 310L)) {
                                    if (queue_composed_mail(
                                            gui, window, to_gadget, cc_gadget,
                                            bcc_gadget, subject_gadget,
                                            body_gadget, attachments,
                                            attachment_count, reply_mode, 1,
                                            error) == AMG_OK) {
                                        done = 1;
                                    } else {
                                        set_utf8_string(compose_status, window,
                                                        error->message);
                                    }
                                } else {
                                    done = 1;
                                }
                                break;

                            case GID_COMPOSE_BODY_SCROLL:
                                handle_texteditor_scroller(
                                    window, body_gadget, body_scroller, 0);
                                break;

                            case GID_COMPOSE_ATTACHMENTS_SCROLL:
                                handle_listbrowser_scroller(
                                    window, attachments_gadget,
                                    attachments_scroller);
                                break;

                            case GID_COMPOSE_ADD_ATTACHMENT:
                                add_attachment(
                                    window, attachments_gadget, compose_status,
                                    &attachment_list, attachments,
                                    &attachment_count);
                                break;

                            case GID_COMPOSE_REMOVE_ATTACHMENT:
                                remove_attachment(
                                    window, attachments_gadget, compose_status,
                                    &attachment_list, attachments,
                                    &attachment_count);
                                break;

                            case GID_COMPOSE_SEND:
                                if (queue_composed_mail(
                                        gui, window, to_gadget, cc_gadget,
                                        bcc_gadget,
                                        subject_gadget, body_gadget,
                                        attachments, attachment_count,
                                        reply_mode, 0,
                                        error) == AMG_OK) {
                                    queued = 1;
                                    done = 1;
                                } else {
                                    set_utf8_string(compose_status, window,
                                                    error->message);
                                }
                                break;
                        }
                        break;
                }
            }
            if (!done) {
                sync_texteditor_scroller(
                    window, body_gadget, body_scroller, 0, 0);
                sync_listbrowser_scroller(
                    window, attachments_gadget, attachments_scroller);
            }
        }
    }
    DisposeObject(dialog);
    FreeListBrowserList(&attachment_list);
    if (queued) status_local(gui, T("Mail wird gesendet...", "Sending mail..."));
    return queued;
}

static int ensure_account(AmgGui *gui, AmgError *error)
{
    if (amg_account_validate(gui->account, error) == AMG_OK) return 1;
    if (!account_dialog(gui, error)) {
        if (error->message[0]) status_utf8(gui, error->message);
        return 0;
    }
    if (amg_account_validate(gui->account, error) != AMG_OK) {
        status_utf8(gui, error->message);
        return 0;
    }
    return 1;
}

static size_t label_index_for_mailbox(const AmgGui *gui,
                                      const char *mailbox_utf8)
{
    size_t i;
    if (!gui || !mailbox_utf8) return gui ? gui->label_count : 0U;
    for (i = 0; i < gui->label_count; ++i) {
        if (gui->labels[i].available &&
            (!strcmp(gui->labels[i].mailbox_utf8, mailbox_utf8) ||
             !strcmp(gui->labels[i].gmail_label_utf8, mailbox_utf8)))
            return i;
    }
    return gui->label_count;
}

static size_t label_index_for_gmail_label(const AmgGui *gui,
                                          const char *gmail_label_utf8)
{
    size_t i;
    if (!gui || !gmail_label_utf8) return gui ? gui->label_count : 0U;
    for (i = 0; i < gui->label_count; ++i) {
        if (gui->labels[i].available &&
            !strcmp(gui->labels[i].gmail_label_utf8, gmail_label_utf8))
            return i;
    }
    return gui->label_count;
}

static int request_label_index(AmgGui *gui, size_t index, AmgError *error)
{
    char message[192];
    int result;
    if (!gui || index >= gui->label_count) return AMG_ERR_ARGUMENT;
    if (!gui->labels[index].available ||
        !gui->labels[index].mailbox_utf8[0]) {
        status_local(gui,
                     T("Dieser Gmail-Ordner ist f\374r das Konto nicht verf\374gbar.", "This Gmail folder is not available for the account."));
        return AMG_ERR_ARGUMENT;
    }
    if (!amg_network_is_connected(gui->network)) {
        status_local(gui, T("Bitte zuerst 'Abrufen' anklicken.", "Please click 'Fetch' first."));
        return AMG_ERR_IO;
    }
    result = amg_network_request(gui->network, AMG_NET_FETCH_INBOX, 0,
                                 gui->labels[index].mailbox_utf8,
                                 NULL, error);
    if (result != AMG_OK) {
        if (error && error->message[0]) status_utf8(gui, error->message);
        return result;
    }
    strncpy(gui->current_mailbox_utf8,
            gui->labels[index].mailbox_utf8,
            sizeof(gui->current_mailbox_utf8) - 1U);
    gui->current_mailbox_utf8[
        sizeof(gui->current_mailbox_utf8) - 1U] = 0;
    strncpy(gui->current_label_local,
            gui->labels[index].path_local[0]
                ? gui->labels[index].path_local
                : gui->labels[index].display_local,
            sizeof(gui->current_label_local) - 1U);
    gui->current_label_local[sizeof(gui->current_label_local) - 1U] = 0;
    select_label_index(gui, index);
    clear_current_message_payload(gui);
    show_message_placeholder(gui, T("Ordner wird geladen...", "Loading folder..."));
    set_preview_local(gui,
                      T("W\344hlen Sie nach dem Abruf eine Nachricht aus.", "Select a message after fetching."));
    amg_tr_snprintf(message, sizeof(message),
                    "%s wird geladen...", "Loading %s...",
                    gui->current_label_local);
    status_local(gui, message);
    return AMG_OK;
}

static void begin_move(AmgGui *gui, AmgError *error)
{
    ULONG uid = 0;
    size_t source_index;
    if (!cursor_node_user_data(gui->messages_gadget, &uid) || !uid) {
        status_local(gui, T("Bitte zuerst eine Nachricht ausw\344hlen.", "Please select a message first."));
        return;
    }
    if (!amg_network_is_connected(gui->network)) {
        status_local(gui, T("Bitte zuerst 'Abrufen' anklicken.", "Please click 'Fetch' first."));
        return;
    }
    if (!gui->current_mailbox_utf8[0]) {
        amg_error_set(error, AMG_ERR_ARGUMENT,
                      T("Der Quellordner ist nicht bekannt.", "The source folder is unknown."));
        status_utf8(gui, error->message);
        return;
    }
    gui->move_uid = uid;
    source_index = label_index_for_mailbox(
        gui, gui->current_mailbox_utf8);
    strncpy(gui->move_source_mailbox_utf8,
            source_index < gui->label_count
                ? gui->labels[source_index].gmail_label_utf8
                : gui->current_mailbox_utf8,
            sizeof(gui->move_source_mailbox_utf8) - 1U);
    gui->move_source_mailbox_utf8[
        sizeof(gui->move_source_mailbox_utf8) - 1U] = 0;
    gui->move_pending = 1;
    status_local(
        gui,
        T("Bitte seitlich einen Zielordner zum Verschieben ausw\344hlen.", "Please select a destination folder on the left."));
}

static int queue_move_to_label(AmgGui *gui, size_t index, AmgError *error)
{
    char message[640];
    const char *target_name;
    size_t current_index;
    int result;
    if (!gui || index >= gui->label_count || !gui->move_pending)
        return AMG_ERR_ARGUMENT;
    if (!gui->labels[index].available ||
        !gui->labels[index].mailbox_utf8[0]) {
        status_local(gui,
                     T("Dieser Gmail-Ordner ist f\374r das Konto nicht verf\374gbar.", "This Gmail folder is not available for the account."));
        return AMG_ERR_ARGUMENT;
    }
    if (!strcmp(gui->move_source_mailbox_utf8,
                gui->labels[index].gmail_label_utf8)) {
        status_local(gui, T("Quell- und Zielordner sind identisch.", "Source and destination folders are identical."));
        return AMG_ERR_ARGUMENT;
    }
    result = amg_network_request(
        gui->network, AMG_NET_MOVE, gui->move_uid,
        gui->move_source_mailbox_utf8,
        gui->labels[index].gmail_label_utf8,
        error);
    if (result != AMG_OK) {
        if (error && error->message[0]) status_utf8(gui, error->message);
        return result;
    }
    gui->move_pending = 0;
    current_index = label_index_for_mailbox(gui, gui->current_mailbox_utf8);
    if (current_index < gui->label_count)
        select_label_index(gui, current_index);
    target_name = gui->labels[index].path_local[0]
        ? gui->labels[index].path_local
        : gui->labels[index].display_local;
    amg_tr_snprintf(message, sizeof(message),
                    "Nachricht wird nach %s verschoben...",
                    "Moving message to %s...", target_name);
    status_local(gui, message);
    return AMG_OK;
}

static void remove_message_uid(AmgGui *gui, ULONG uid)
{
    struct Node *node, *next;
    if (!gui || !uid) return;
    detach_listbrowser(gui->messages_gadget, gui->window);
    node = gui->messages_list.lh_Head;
    while (node && node->ln_Succ) {
        ULONG value = 0;
        next = node->ln_Succ;
        GetListBrowserNodeAttrs(
            node, LBNA_UserData, (ULONG)(uintptr_t)&value, TAG_DONE);
        if (value == uid) {
            Remove(node);
            FreeListBrowserNode(node);
            if (gui->active_message_uid == uid)
                gui->active_message_uid = 0;
            break;
        }
        node = next;
    }
    if (!gui->messages_list.lh_Head->ln_Succ) {
        node = message_placeholder_node(
            T("Dieser Ordner enth\344lt keine Nachrichten.", "This folder contains no messages."));
        if (node) AddTail(&gui->messages_list, node);
    }
    attach_listbrowser(gui->messages_gadget, gui->window,
                       &gui->messages_list);
    clear_current_message_payload(gui);
    set_preview_local(gui,
                      T("W\344hlen Sie eine Nachricht zur Anzeige aus.", "Select a message to display."));
}

static int handle_label_tree_event(AmgGui *gui)
{
    ULONG release_event = LBRE_NORMAL;
    ULONG top = 0;
    struct Node *node = NULL;
    if (!gui || !gui->labels_gadget) return 0;
    GetAttr(LISTBROWSER_RelEvent, (Object *)gui->labels_gadget,
            &release_event);
    if (release_event != LBRE_SHOWCHILDREN &&
        release_event != LBRE_HIDECHILDREN)
        return 0;
    GetAttr(LISTBROWSER_CursorNode, (Object *)gui->labels_gadget,
            (ULONG *)&node);
    if (!node)
        GetAttr(LISTBROWSER_SelectedNode, (Object *)gui->labels_gadget,
                (ULONG *)&node);
    if (!node) return 1;
    {
        ULONG label_index = (ULONG)~0UL;
        GetListBrowserNodeAttrs(
            node, LBNA_UserData, (ULONG)(uintptr_t)&label_index, TAG_DONE);
        if (label_index < gui->label_count &&
            gui->labels[label_index].has_children)
            gui->labels[label_index].expanded =
                release_event == LBRE_SHOWCHILDREN ? 1 : 0;
    }
    GetAttr(LISTBROWSER_Top, (Object *)gui->labels_gadget, &top);
    detach_listbrowser(gui->labels_gadget, gui->window);
    if (release_event == LBRE_SHOWCHILDREN)
        ShowListBrowserNodeChildren(node, 1);
    else
        HideListBrowserNodeChildren(node);
    save_label_expansion_state(gui);
    SetGadgetAttrs(gui->labels_gadget, gui->window, NULL,
                   LISTBROWSER_Labels,
                       (ULONG)(uintptr_t)&gui->labels_list,
                   LISTBROWSER_SelectedNode, (ULONG)(uintptr_t)node,
                   LISTBROWSER_Top, top,
                   TAG_DONE);
    sync_labels_scroller(gui);
    return 1;
}

static void handle_label_gadget(AmgGui *gui, struct Gadget *gadget,
                                int hierarchical, AmgError *error)
{
    ULONG index = 0;
    if (hierarchical && handle_label_tree_event(gui)) return;
    if (!selected_node_user_data(gadget, &index)) return;
    if (gui->move_pending)
        queue_move_to_label(gui, (size_t)index, error);
    else
        request_label_index(gui, (size_t)index, error);
}

static int message_doubleclick_detected(AmgGui *gui, ULONG uid,
                                        ULONG release_event)
{
    ULONG seconds = 0UL, micros = 0UL;
    int is_doubleclick = 0;

    if (!gui || !uid) return 0;

    /* Prefer listbrowser.gadget's own result when it survives unchanged. */
    if (release_event == LBRE_DOUBLECLICK) {
        gui->message_click_valid = 0;
        gui->message_click_uid = 0UL;
        return 1;
    }

    /* AmiGmail redraws the selected row after a normal click. On classic
     * ReAction this can reset ListBrowser's internal double-click state before
     * the second click arrives. Keep a second, OS-native detector keyed to the
     * UID so a redraw cannot break the gesture. DoubleClick() uses the user's
     * Intuition double-click preference rather than a hard-coded timeout. */
    CurrentTime(&seconds, &micros);
    if (gui->message_click_valid && gui->message_click_uid == uid &&
        DoubleClick(gui->message_click_seconds, gui->message_click_micros,
                    seconds, micros)) {
        is_doubleclick = 1;
        gui->message_click_valid = 0;
        gui->message_click_uid = 0UL;
    } else {
        gui->message_click_seconds = seconds;
        gui->message_click_micros = micros;
        gui->message_click_uid = uid;
        gui->message_click_valid = 1;
    }
    return is_doubleclick;
}

static void toggle_message_flagged(AmgGui *gui, ULONG uid, AmgError *error)
{
    int flagged;
    int result;
    if (!gui || !uid) return;
    if (!amg_network_is_connected(gui->network)) {
        status_local(gui, T("Bitte zuerst 'Abrufen' anklicken.", "Please click 'Fetch' first."));
        return;
    }
    flagged = !message_is_flagged(gui, uid);
    result = amg_network_request(
        gui->network, AMG_NET_SET_FLAGGED, uid,
        flagged ? "1" : "0", "doubleclick", error);
    if (result == AMG_OK) {
        set_message_flagged_visual(gui, uid, flagged);
        status_local(gui, flagged
            ? T("Sternmarkierung wird gesetzt...", "Setting star...")
            : T("Sternmarkierung wird entfernt...", "Removing star..."));
    } else if (error && error->message[0]) {
        status_utf8(gui, error->message);
    }
}

static void request_message(AmgGui *gui, int reply, AmgError *error)
{
    ULONG uid = 0;
    ULONG selected[2];
    size_t selected_count;
    ULONG release_event = LBRE_NORMAL;
    int is_doubleclick = 0;
    int result;
    if (!gui || !gui->messages_gadget) return;
    if (!reply) {
        GetAttr(LISTBROWSER_RelEvent, (Object *)gui->messages_gadget,
                &release_event);
        if (release_event == LBRE_TITLECLICK) return;
        if (!cursor_node_user_data(gui->messages_gadget, &uid) || !uid) {
            status_local(gui, T("Bitte zuerst eine Nachricht ausw\344hlen.", "Please select a message first."));
            return;
        }
        is_doubleclick = message_doubleclick_detected(gui, uid, release_event);
    } else {
        selected_count = selected_message_uids(gui, selected, 2U);
        if (!selected_count) {
            status_local(gui, T("Bitte zuerst eine Nachricht ausw\344hlen.", "Please select a message first."));
            return;
        }
        if (selected_count > 1U) {
            status_local(gui, T("Bitte zum Antworten nur eine Nachricht ausw\344hlen.", "Please select only one message to reply."));
            return;
        }
        uid = selected[0];
    }
    if (!uid) {
        status_local(gui, T("Bitte zuerst eine Nachricht ausw\344hlen.", "Please select a message first."));
        return;
    }
    set_message_selected_visual(gui, uid);
    if (!reply && is_doubleclick) {
        toggle_message_flagged(gui, uid, error);
        return;
    }
    if (!amg_network_is_connected(gui->network)) {
        status_local(gui, T("Bitte zuerst 'Abrufen' anklicken.", "Please click 'Fetch' first."));
        return;
    }
    result = amg_network_request(gui->network, AMG_NET_FETCH_MESSAGE,
                                 uid, reply ? "reply" : "preview",
                                 NULL, error);
    if (result == AMG_OK) {
        clear_current_message_payload(gui);
        if (reply)
            status_local(gui, T("Antwort wird vorbereitet...", "Preparing reply..."));
        else {
            set_preview_local(gui, T("Nachricht wird geladen...", "Loading message..."));
            status_local(gui, T("Nachricht wird geladen...", "Loading message..."));
        }
    } else if (error && error->message[0]) {
        status_utf8(gui, error->message);
    }
}

static void toggle_selected_seen(AmgGui *gui, AmgError *error)
{
    ULONG *uids;
    size_t count, i, queued = 0;
    if (!gui) return;
    uids = selected_message_uids_alloc(gui, &count);
    if (!uids) {
        status_local(gui, T("Nicht genug Speicher.", "Not enough memory."));
        return;
    }
    if (!count) {
        free(uids);
        status_local(gui, T("Bitte zuerst eine Nachricht ausw\344hlen.", "Please select a message first."));
        return;
    }
    if (!amg_network_is_connected(gui->network)) {
        free(uids);
        status_local(gui, T("Bitte zuerst 'Abrufen' anklicken.", "Please click 'Fetch' first."));
        return;
    }
    for (i = 0; i < count; ++i) {
        int seen = !message_is_seen(gui, uids[i]);
        int result = amg_network_request(
            gui->network, AMG_NET_SET_SEEN, uids[i],
            seen ? "1" : "0", "button", error);
        if (result != AMG_OK) break;
        set_message_seen_visual(gui, uids[i], seen);
        ++queued;
    }
    free(uids);
    if (queued == count) {
        char message[128];
        amg_tr_snprintf(message, sizeof(message),
                        "%lu Nachricht(en): Lesestatus wird ge\344ndert.",
                        "%lu message(s): changing read status.",
                        (unsigned long)queued);
        status_local(gui, message);
    } else if (error && error->message[0]) {
        status_utf8(gui, error->message);
    }
}

static int confirm_question_dialog_for_window(AmgGui *gui,
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
        WINDOW_RefWindow, ref_window,
        WINDOW_Position, WPOS_CENTERWINDOW,
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
                        if ((result & WMHI_KEYMASK) == 0x45UL) done = 1;
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

static int confirm_delete_dialog(AmgGui *gui)
{
    return confirm_question_dialog(
        gui, T("Mail wirklich l\366schen?", "Really delete mail?"),
        T("Dieser Vorgang kann nicht widerrufen werden.",
          "This action cannot be undone."), 310L);
}

static int confirm_empty_trash_dialog(AmgGui *gui)
{
    return confirm_question_dialog(
        gui, T("Papierkorb wirklich leeren?", "Really empty Trash?"),
        NULL, 280L);
}

static int confirm_empty_spam_dialog(AmgGui *gui)
{
    return confirm_question_dialog(
        gui, T("Spam wirklich leeren?", "Really empty Spam?"),
        NULL, 280L);
}

static void delete_selected_messages(AmgGui *gui, AmgError *error)
{
    ULONG *uids;
    size_t count, i, source_index;
    const char *source_label, *trash_label;
    char status_text[128];
    if (!gui) return;
    uids = selected_message_uids_alloc(gui, &count);
    if (!uids) {
        status_local(gui, T("Nicht genug Speicher.", "Not enough memory."));
        return;
    }
    if (!count) {
        free(uids);
        status_local(gui, T("Bitte zuerst eine Nachricht ausw\344hlen.", "Please select a message first."));
        return;
    }
    if (!amg_network_is_connected(gui->network)) {
        free(uids);
        status_local(gui, T("Bitte zuerst 'Abrufen' anklicken.", "Please click 'Fetch' first."));
        return;
    }
    if (!confirm_delete_dialog(gui)) {
        free(uids);
        return;
    }
    source_index = label_index_for_mailbox(gui, gui->current_mailbox_utf8);
    source_label = source_index < gui->label_count
        ? gui->labels[source_index].gmail_label_utf8
        : gui->current_mailbox_utf8;
    trash_label = gui->labels[6U].available
        ? gui->labels[6U].gmail_label_utf8 : "\\Trash";
    if (!source_label || !*source_label) {
        free(uids);
        status_local(gui, T("Der aktuelle Gmail-Ordner ist nicht bekannt.", "The current Gmail folder is unknown."));
        return;
    }
    if (!strcmp(source_label, trash_label)) {
        free(uids);
        status_local(gui, T("Die Nachricht liegt bereits im Papierkorb.", "The message is already in Trash."));
        return;
    }
    for (i = 0; i < count; ++i) {
        int result = amg_network_request(
            gui->network, AMG_NET_DELETE, uids[i],
            trash_label, source_label, error);
        if (result != AMG_OK) {
            free(uids);
            if (error && error->message[0]) status_utf8(gui, error->message);
            return;
        }
    }
    free(uids);
    amg_tr_snprintf(status_text, sizeof(status_text),
                    "%lu Nachricht(en) werden gel\366scht.",
                    "Deleting %lu message(s).",
                    (unsigned long)count);
    status_local(gui, status_text);
}

static void empty_trash(AmgGui *gui, AmgError *error)
{
    const char *trash_label;
    int result;
    if (!gui) return;
    if (!amg_network_is_connected(gui->network)) {
        status_local(gui, T("Bitte zuerst 'Abrufen' anklicken.", "Please click 'Fetch' first."));
        return;
    }
    if (!confirm_empty_trash_dialog(gui)) return;
    trash_label = gui->labels[6U].available
        ? gui->labels[6U].gmail_label_utf8 : "\\Trash";
    if (!trash_label || !*trash_label) {
        status_local(gui, T("Der Gmail-Papierkorb ist nicht bekannt.", "The Gmail Trash folder is unknown."));
        return;
    }
    result = amg_network_request(gui->network, AMG_NET_EMPTY_TRASH, 0,
                                 trash_label, NULL, error);
    if (result == AMG_OK)
        status_local(gui, T("Papierkorb wird geleert...", "Emptying Trash..."));
    else if (error && error->message[0])
        status_utf8(gui, error->message);
}

static void empty_spam(AmgGui *gui, AmgError *error)
{
    const char *spam_label;
    int result;
    if (!gui) return;
    if (!amg_network_is_connected(gui->network)) {
        status_local(gui, T("Bitte zuerst 'Abrufen' anklicken.",
                            "Please click 'Fetch' first."));
        return;
    }
    if (!confirm_empty_spam_dialog(gui)) return;
    spam_label = gui->labels[5U].available
        ? gui->labels[5U].gmail_label_utf8 : NULL;
    if (!spam_label || !*spam_label) {
        status_local(gui, T("Der Gmail-Spamordner ist nicht bekannt.",
                            "The Gmail Spam folder is unknown."));
        return;
    }
    result = amg_network_request(gui->network, AMG_NET_EMPTY_SPAM, 0,
                                 spam_label, NULL, error);
    if (result == AMG_OK)
        status_local(gui, T("Spam wird geleert...", "Emptying Spam..."));
    else if (error && error->message[0])
        status_utf8(gui, error->message);
}

static void handle_network(AmgGui *gui)
{
    AmgNetworkEvent event;
    while (amg_network_poll(gui->network, &event) > 0) {
        if (event.type == AMG_NET_CHECK_INBOX)
            gui->periodic_check_pending = 0;
        if (event.result != AMG_OK) {
            if (event.type == AMG_NET_SET_SEEN)
                set_message_seen_visual(
                    gui, event.uid, atoi(event.argument1) == 0);
            else if (event.type == AMG_NET_SET_FLAGGED)
                set_message_flagged_visual(
                    gui, event.uid, atoi(event.argument1) == 0);
            status_utf8(gui,
                        event.message[0] ? event.message : T("Netzwerkfehler", "Network error"));
        } else {
            switch (event.type) {
                case AMG_NET_CONNECT:
                    status_local(gui, T("Mit Gmail verbunden.", "Connected to Gmail."));
                    amg_network_request(gui->network, AMG_NET_FETCH_LABELS,
                                        0, NULL, NULL, NULL);
                    break;
                case AMG_NET_FETCH_LABELS:
                {
                    AmgError request_error;
                    char message[96];
                    size_t count = update_labels_from_payload(
                        gui, event.payload, event.payload_length);
                    amg_tr_snprintf(message, sizeof(message),
                                    "%lu Gmail-Labels wurden geladen.",
                                    "%lu Gmail labels loaded.",
                                    (unsigned long)count);
                    status_local(gui, message);
                    memset(&request_error, 0, sizeof(request_error));
                    request_label_index(gui, 0U, &request_error);
                    break;
                }
                case AMG_NET_FETCH_INBOX:
                {
                    char message[256];
                    int parse_error = 0;
                    int uid_parse_error = 0;
                    unsigned long max_uid = 0UL;
                    size_t index, count;
                    index = label_index_for_mailbox(
                        gui, event.argument1[0] ? event.argument1 : "INBOX");
                    if (index == 0U) {
                        (void)message_uid_stats(
                            event.payload, event.payload_length, 0UL,
                            &max_uid, &uid_parse_error);
                        if (uid_parse_error >= 0) {
                            gui->inbox_latest_uid = max_uid;
                            gui->inbox_baseline_ready = 1;
                        }
                    }
                    count = update_messages_from_payload(
                        gui, event.payload, event.payload_length, &parse_error);
                    if (index < gui->label_count) {
                        strncpy(gui->current_mailbox_utf8,
                                gui->labels[index].mailbox_utf8,
                                sizeof(gui->current_mailbox_utf8) - 1U);
                        gui->current_mailbox_utf8[
                            sizeof(gui->current_mailbox_utf8) - 1U] = 0;
                        strncpy(gui->current_label_local,
                                gui->labels[index].path_local[0]
                                    ? gui->labels[index].path_local
                                    : gui->labels[index].display_local,
                                sizeof(gui->current_label_local) - 1U);
                        gui->current_label_local[
                            sizeof(gui->current_label_local) - 1U] = 0;
                        select_label_index(gui, index);
                    }
                    set_preview_local(
                        gui, T("W\344hlen Sie eine Nachricht zur Anzeige aus.", "Select a message to display."));
                    if (parse_error < 0) {
                        amg_tr_snprintf(message, sizeof(message),
                                        "IMAP-Nachrichten konnten nicht ausgewertet werden (Code %d).",
                                        "IMAP messages could not be parsed (code %d).",
                                        parse_error);
                    } else {
                        amg_tr_snprintf(message, sizeof(message),
                                        "%lu Nachrichten in %s geladen.",
                                        "%lu messages loaded in %s.",
                                        (unsigned long)count,
                                        gui->current_label_local[0]
                                            ? gui->current_label_local
                                            : T("Ordner", "folder"));
                    }
                    status_local(gui, message);
                    break;
                }
                case AMG_NET_CHECK_INBOX:
                {
                    unsigned long max_uid = 0UL;
                    unsigned long previous_uid = gui->inbox_latest_uid;
                    int parse_error = 0;
                    size_t new_count = message_uid_stats(
                        event.payload, event.payload_length,
                        gui->inbox_baseline_ready ? previous_uid : 0UL,
                        &max_uid, &parse_error);
                    if (parse_error < 0) {
                        char message[160];
                        amg_tr_snprintf(message, sizeof(message),
                                        "Periodischer Abruf konnte nicht ausgewertet werden (Code %d).",
                                        "Periodic fetch could not be parsed (code %d).",
                                        parse_error);
                        status_local(gui, message);
                        break;
                    }
                    if (!gui->inbox_baseline_ready) {
                        new_count = 0U;
                        gui->inbox_latest_uid = max_uid;
                    } else if (max_uid > gui->inbox_latest_uid) {
                        gui->inbox_latest_uid = max_uid;
                    }
                    gui->inbox_baseline_ready = 1;

                    /* Ist die Inbox gerade sichtbar, neue Header ohne
                     * kompletten Listen-Neuaufbau einfuegen. Auswahl, Preview
                     * und Scrollposition bleiben dadurch erhalten. Andere
                     * Labels werden vom periodischen Abruf nie veraendert. */
                    if (new_count > 0U &&
                        !strcmp(gui->current_mailbox_utf8, "INBOX")) {
                        int merge_error = 0;
                        (void)merge_new_messages_from_payload(
                            gui, event.payload, event.payload_length,
                            &merge_error);
                    }
                    if (new_count > 0U) {
                        char message[128];
                        amg_tr_snprintf(message, sizeof(message),
                                        "Periodischer Abruf: %lu neue Mail(s) im Posteingang.",
                                        "Periodic fetch: %lu new mail(s) in Inbox.",
                                        (unsigned long)new_count);
                        status_local(gui, message);
                    } else {
                        status_local(gui,
                            T("Periodischer Abruf: keine neuen Mails.",
                              "Periodic fetch: no new mail."));
                    }
                    break;
                }
                case AMG_NET_FETCH_MESSAGE:
                {
                    AmgError preview_error;
                    int prepare_reply = !strcmp(event.argument1, "reply");
                    int unread_before = !message_is_seen(gui, event.uid);
                    memset(&preview_error, 0, sizeof(preview_error));
                    if (display_message_payload(
                            gui, event.payload, event.payload_length,
                            &preview_error) == AMG_OK) {
                        retain_current_message_payload(gui, &event);
                        set_message_selected_visual(gui, event.uid);
                        if (unread_before) {
                            set_message_seen_visual(gui, event.uid, 1);
                            (void)amg_network_request(
                                gui->network, AMG_NET_SET_SEEN, event.uid,
                                "1", "preview", NULL);
                        }
                        if (prepare_reply) {
                            if (prepare_reply_payload(
                                    gui, event.payload, event.payload_length,
                                    &preview_error) == AMG_OK) {
                                if (!compose_dialog(gui, 1, &preview_error))
                                    status_local(gui,
                                                 T("Antwort wurde nicht gesendet.", "Reply was not sent."));
                            } else {
                                status_utf8(gui,
                                    preview_error.message[0]
                                        ? preview_error.message
                                        : T("Antwort konnte nicht vorbereitet werden.", "Reply could not be prepared."));
                            }
                        } else {
                            status_local(gui, T("Nachricht geladen.", "Message loaded."));
                        }
                    } else {
                        status_utf8(
                            gui, preview_error.message[0]
                                     ? preview_error.message
                                     : T("Nachricht konnte nicht dargestellt werden.", "Message could not be displayed."));
                    }
                    break;
                }
                case AMG_NET_SET_SEEN:
                    set_message_seen_visual(
                        gui, event.uid, atoi(event.argument1) != 0);
                    if (strcmp(event.argument2, "preview"))
                        status_local(
                            gui, atoi(event.argument1)
                                ? T("Nachricht als gelesen markiert.", "Message marked as read.")
                                : T("Nachricht als ungelesen markiert.", "Message marked as unread."));
                    break;
                case AMG_NET_SET_FLAGGED:
                    set_message_flagged_visual(
                        gui, event.uid, atoi(event.argument1) != 0);
                    status_local(
                        gui, atoi(event.argument1)
                            ? T("Sternmarkierung wurde gesetzt.", "Star was set.")
                            : T("Sternmarkierung wurde entfernt.", "Star was removed."));
                    break;
                case AMG_NET_DELETE:
                    remove_message_uid(gui, event.uid);
                    status_local(gui,
                                 T("Nachricht wurde in den Papierkorb verschoben.", "Message moved to Trash."));
                    break;
                case AMG_NET_EMPTY_TRASH:
                    if (label_index_for_mailbox(
                            gui, gui->current_mailbox_utf8) == 6U) {
                        clear_current_message_payload(gui);
                        show_message_placeholder(
                            gui, T("Dieser Ordner enth\344lt keine Nachrichten.", "This folder contains no messages."));
                        set_preview_local(
                            gui, T("W\344hlen Sie eine Nachricht zur Anzeige aus.", "Select a message to display."));
                    }
                    status_local(gui, T("Papierkorb wurde geleert.", "Trash was emptied."));
                    break;
                case AMG_NET_EMPTY_SPAM:
                    if (label_index_for_mailbox(
                            gui, gui->current_mailbox_utf8) == 5U) {
                        clear_current_message_payload(gui);
                        show_message_placeholder(
                            gui, T("Dieser Ordner enth\344lt keine Nachrichten.", "This folder contains no messages."));
                        set_preview_local(
                            gui, T("W\344hlen Sie eine Nachricht zur Anzeige aus.", "Select a message to display."));
                    }
                    status_local(gui, T("Spam wurde geleert.", "Spam was emptied."));
                    break;
                case AMG_NET_SAVE_DRAFT:
                    status_local(gui, T("Entwurf wurde gespeichert.",
                                        "Draft was saved."));
                    break;
                case AMG_NET_SEND_REPLY:
                    status_local(gui, T("Antwort wurde versendet.", "Reply sent."));
                    break;
                case AMG_NET_SEND_MAIL:
                    status_local(gui, T("Mail wurde versendet.", "Mail sent."));
                    break;
                case AMG_NET_MOVE:
                {
                    char message[640];
                    size_t target = label_index_for_gmail_label(
                        gui, event.argument2);
                    const char *target_name = event.argument2;
                    remove_message_uid(gui, event.uid);
                    if (target < gui->label_count)
                        target_name = gui->labels[target].path_local[0]
                            ? gui->labels[target].path_local
                            : gui->labels[target].display_local;
                    amg_tr_snprintf(message, sizeof(message),
                                    "Nachricht wurde nach %s verschoben.",
                                    "Message moved to %s.",
                                    target_name && *target_name
                                        ? target_name
                                        : T("Zielordner", "destination folder"));
                    status_local(gui, message);
                    break;
                }
                default:
                    status_local(gui, T("Aktion abgeschlossen.", "Action completed."));
                    break;
            }
        }
        amg_network_event_clear(&event);
    }
}

static void periodic_fetch_mail(AmgGui *gui, AmgError *error)
{
    int result = AMG_OK;
    if (!gui || !gui->account || !gui->account->periodic_fetch ||
        account_is_locked(gui->account) || gui->periodic_check_pending)
        return;
    if (!amg_network_is_running(gui->network))
        result = amg_network_start(gui->network, gui->account, error);
    if (result != AMG_OK) {
        if (error && error->message[0]) status_utf8(gui, error->message);
        return;
    }
    if (!amg_network_is_connected(gui->network)) {
        status_local(gui, T("Periodischer Abruf: Verbinde mit Gmail...",
                            "Periodic fetch: connecting to Gmail..."));
        result = amg_network_request(gui->network, AMG_NET_CONNECT, 0,
                                     NULL, "periodic", error);
    } else {
        result = amg_network_request(
            gui->network, AMG_NET_CHECK_INBOX,
            gui->inbox_baseline_ready ? gui->inbox_latest_uid : 0UL,
            "INBOX", "periodic", error);
        if (result == AMG_OK) gui->periodic_check_pending = 1;
    }
    if (result != AMG_OK && error && error->message[0])
        status_utf8(gui, error->message);
}

static void fetch_mail(AmgGui *gui, AmgError *error)
{
    int result = AMG_OK;
    if (!ensure_account(gui, error)) return;
    if (!amg_network_is_running(gui->network)) {
        result = amg_network_start(gui->network, gui->account, error);
    }
    if (result != AMG_OK) {
        status_utf8(gui, error->message);
    } else if (!amg_network_is_connected(gui->network)) {
        status_local(gui, T("Verbinde mit Gmail...", "Connecting to Gmail..."));
        result = amg_network_request(gui->network, AMG_NET_CONNECT, 0,
                                     NULL, NULL, error);
        if (result != AMG_OK) status_utf8(gui, error->message);
    } else {
        result = amg_network_request(gui->network, AMG_NET_FETCH_INBOX, 0,
                                     gui->current_mailbox_utf8[0]
                                         ? gui->current_mailbox_utf8 : "INBOX",
                                     NULL, error);
        if (result != AMG_OK) status_utf8(gui, error->message);
    }
}

static void handle_main_gadget(AmgGui *gui, ULONG gadget_id,
                               AmgError *error)
{
    switch (gadget_id) {
        case GID_NEW_MAIL:
            if (ensure_account(gui, error)) compose_dialog(gui, 0, error);
            break;
        case GID_FETCH:
            fetch_mail(gui, error);
            break;
        case GID_SYSTEM_LABELS:
            handle_label_gadget(gui, gui->system_labels_gadget, 0, error);
            break;
        case GID_LABELS:
            handle_label_gadget(gui, gui->labels_gadget, 1, error);
            break;
        case GID_LABELS_SCROLL:
            handle_labels_scroller(gui);
            break;
        case GID_MESSAGES:
            request_message(gui, 0, error);
            break;
        case GID_MESSAGES_SCROLL:
            handle_messages_scroller(gui);
            break;
        case GID_PREVIEW_SCROLL:
            handle_preview_scroller(gui);
            break;
        case GID_SAVE_ATTACHMENTS:
            save_current_attachments(gui);
            break;
        case GID_REPLY:
            request_message(gui, 1, error);
            break;
        case GID_DELETE:
            delete_selected_messages(gui, error);
            break;
        case GID_SEEN:
            toggle_selected_seen(gui, error);
            break;
        case GID_MOVE:
            begin_move(gui, error);
            break;
    }
}

static void handle_menu(AmgGui *gui, ULONG menu_code, AmgError *error)
{
    switch (menu_code) {
        case MENU_ACCOUNT:
            account_dialog(gui, error);
            break;
        case MENU_ABOUT:
            about_dialog(gui);
            break;
        case MENU_QUIT:
            gui->running = 0;
            break;
        case MENU_EMPTY_TRASH:
            empty_trash(gui, error);
            break;
        case MENU_EMPTY_SPAM:
            empty_spam(gui, error);
            break;
    }
}

int amg_gui_run(AmgGui *gui, AmgError *error)
{
    ULONG window_signal;
    if (!gui) return AMG_ERR_ARGUMENT;
    gui->window = RA_OpenWindow(gui->window_object);
    if (!gui->window) {
        amg_error_set(error, AMG_ERR_IO,
                      T("Workbench-Fenster konnte nicht ge\303\266ffnet werden.", "Workbench window could not be opened."));
        return AMG_ERR_IO;
    }
    center_window_on_screen(gui->window);
    draw_window_overlays(gui);
    GetAttr(WINDOW_SigMask, gui->window_object, &window_signal);
    gui->running = 1;

    if (account_is_locked(gui->account)) {
        account_dialog(gui, error);
        draw_window_overlays(gui);
    }
    if (!periodic_timer_init(gui) && gui->account->periodic_fetch)
        status_local(gui,
            T("Periodischer Abruf ist nicht verf\374gbar (timer.device).",
              "Periodic fetch is unavailable (timer.device)."));

    if (!account_is_locked(gui->account) && gui->account->fetch_on_start)
        fetch_mail(gui, error);

    while (gui->running) {
        ULONG network_signal = amg_network_signal_mask(gui->network);
        ULONG timer_signal = periodic_timer_signal_mask(gui);
        ULONG signals = Wait(window_signal | network_signal | timer_signal |
                             SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C) gui->running = 0;
        if (network_signal && (signals & network_signal)) {
            handle_network(gui);
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
                }
            }
            draw_window_overlays(gui);
        }
    }
    periodic_timer_cleanup(gui);
    amg_network_stop(gui->network);
    gui->window = NULL;
    return AMG_OK;
}

void amg_gui_destroy(AmgGui *gui)
{
    size_t i;
    if (!gui) return;
    periodic_timer_cleanup(gui);
    free(gui->current_message_payload);
    gui->current_message_payload = NULL;
    if (gui->window_object) DisposeObject(gui->window_object);
    gui->window_object = NULL;
    dispose_label_tree_images(gui);
    if (gui->screen) {
        if (gui->unread_pen_owned && gui->unread_pen >= 0)
            ReleasePen(gui->screen->ViewPort.ColorMap, gui->unread_pen);
        for (i = 0; i < BANNER_COLOR_COUNT; ++i) {
            if (gui->banner_pen_owned[i] && gui->banner_pens[i] >= 0)
                ReleasePen(gui->screen->ViewPort.ColorMap,
                           gui->banner_pens[i]);
        }
        UnlockPubScreen(NULL, gui->screen);
        gui->screen = NULL;
    }
    if (gui->columns) FreeLBColumnInfo(gui->columns);
    FreeListBrowserList(&gui->system_labels_list);
    FreeListBrowserList(&gui->labels_list);
    FreeListBrowserList(&gui->messages_list);
    amg_network_destroy(gui->network);
    free(gui);
    close_classes();
}

#else

struct AmgGui {
    int unavailable;
};

AmgGui *amg_gui_create(AmgAccount *account, AmgError *error)
{
    (void)account;
    amg_error_set(error, AMG_ERR_UNSUPPORTED,
                  "ReAction ist nur im AmigaOS-Build verf\303\274gbar.");
    return NULL;
}

int amg_gui_run(AmgGui *gui, AmgError *error)
{
    (void)gui;
    amg_error_set(error, AMG_ERR_UNSUPPORTED,
                  "ReAction ist nur im AmigaOS-Build verf\303\274gbar.");
    return AMG_ERR_UNSUPPORTED;
}

void amg_gui_destroy(AmgGui *gui)
{
    free(gui);
}

#endif
