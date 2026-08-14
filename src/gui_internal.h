#ifndef AMIGMAIL_GUI_INTERNAL_H
#define AMIGMAIL_GUI_INTERNAL_H

#include "gui.h"
#include "network_task.h"

#if AMIGMAIL_AMIGA

#include <devices/timer.h>
#include <exec/lists.h>
#include <exec/ports.h>
#include <exec/tasks.h>
#include <gadgets/listbrowser.h>
#include <intuition/classes.h>
#include <intuition/intuition.h>
#include <utility/hooks.h>

#define BANNER_COLOR_COUNT 8U
#define GUI_REPLY_BODY_MAX 32768U
#define GUI_SCROLLBAR_WIDTH 16
#define GUI_URL_MAX 1024U
#define GUI_SYSTEM_LABEL_VISIBLE_COUNT 6U
#define COMPOSE_PATH_MAX 512U
#define COMPOSE_NAME_MAX 256U
#define GUI_MAIN_DEFAULT_WIDTH 720L
#define GUI_MAIN_DEFAULT_HEIGHT 480L
#define GUI_MAIN_MIN_WIDTH 620L
#define GUI_MAIN_MIN_HEIGHT 320L

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
    GID_STATUS,
    GID_UPDATE
};


typedef struct ComposeAttachment {
    char path[COMPOSE_PATH_MAX];
    char name_local[COMPOSE_NAME_MAX];
    char name_utf8[COMPOSE_NAME_MAX * 2U];
    unsigned long size;
    int temporary;
} ComposeAttachment;

typedef enum ComposeMode {
    COMPOSE_MODE_NEW = 0,
    COMPOSE_MODE_REPLY,
    COMPOSE_MODE_EDIT_DRAFT
} ComposeMode;

typedef struct ComposeDraftSeed {
    char to_local[768];
    char cc_local[768];
    char bcc_local[768];
    char subject_local[512];
    char body_local[GUI_REPLY_BODY_MAX];
    char in_reply_to_utf8[512];
    char references_utf8[1024];
    char message_id_utf8[256];
    char mailbox_utf8[512];
    unsigned long uid;
    ComposeAttachment attachments[AMG_MAIL_MAX_ATTACHMENTS];
    size_t attachment_count;
} ComposeDraftSeed;

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

typedef struct PendingTempCleanup PendingTempCleanup;

struct AmgGui {
    AmgAccount *account;
    AmgNetwork *network;
    Object *window_object;
    struct Window *window;
    struct Gadget *new_mail_gadget;
    struct Gadget *reply_gadget;
    struct Gadget *system_labels_gadget;
    struct Gadget *labels_gadget;
    struct Gadget *labels_scroller;
    struct Gadget *messages_gadget;
    struct Gadget *messages_scroller;
    struct Gadget *preview_gadget;
    struct Gadget *preview_scroller;
    struct Gadget *save_attachments_gadget;
    struct Gadget *update_gadget;
    struct Image label_show_image;
    struct Image label_hide_image;
    UWORD *label_show_image_data;
    UWORD *label_hide_image_data;
    ULONG label_image_data_bytes;
    struct Hook preview_url_hook;
    char pending_preview_url[GUI_URL_MAX];
    int pending_preview_url_ready;
    LONG preview_url_signal_bit;
    ULONG preview_url_signal_mask;
    struct Task *preview_url_signal_task;
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
    LONG update_pen;
    unsigned char update_pen_owned;
    struct MsgPort *periodic_timer_port;
    struct timerequest *periodic_timer_request;
    int periodic_timer_device_open;
    int periodic_timer_pending;
    int periodic_check_pending;
    unsigned long inbox_latest_uid;
    int inbox_baseline_ready;
    unsigned long inbox_unseen_count;
    int inbox_unseen_known;
    ULONG message_click_seconds;
    ULONG message_click_micros;
    ULONG message_click_uid;
    int message_click_valid;
    PendingTempCleanup *pending_temp_cleanups;
    int mail_network_started;
    int update_check_started;
    int update_check_deferred;
    int update_check_pending;
    int update_download_pending;
    int update_available;
    char update_tag[32];
    char update_download_url[768];
    int window_state_valid;
    LONG saved_window_left;
    LONG saved_window_top;
    LONG saved_window_width;
    LONG saved_window_height;
    int running;
};

/* Shared only inside the private GUI implementation. */
int rawkey_is_accept(ULONG result);
int rawkey_is_cancel(ULONG result);
const char *string_text(struct Gadget *gadget);
void set_string(struct Gadget *gadget, struct Window *window,
                const char *text);
void set_utf8_string(struct Gadget *gadget, struct Window *window,
                     const char *utf8);
void status_local(AmgGui *gui, const char *text);
void status_utf8(AmgGui *gui, const char *utf8);
void periodic_timer_restart(AmgGui *gui);
void periodic_timer_cleanup(AmgGui *gui);
Object *static_text_label(const char *text);
void draw_embedded_banner_at(AmgGui *gui, struct Window *window,
                             LONG left, LONG top,
                             LONG available_width, LONG available_height);

/* Shared private GUI helpers used by more than one GUI translation unit. */
void header_to_local(const char *header, const char *fallback,
                     char *local, size_t capacity);
struct Node *one_column_node(const char *text, ULONG user_data);
struct Gadget *create_vertical_scroller(ULONG gadget_id);
void sync_listbrowser_scroller(struct Window *window,
                               struct Gadget *listbrowser,
                               struct Gadget *scroller);
void handle_listbrowser_scroller(struct Window *window,
                                 struct Gadget *listbrowser,
                                 struct Gadget *scroller);
void sync_texteditor_scroller(struct Window *window,
                              struct Gadget *editor,
                              struct Gadget *scroller,
                              ULONG fallback_entries, int reset_top);
void handle_texteditor_scroller(struct Window *window,
                                struct Gadget *editor,
                                struct Gadget *scroller,
                                ULONG fallback_entries);
void utf8_to_local_copy(const char *utf8, char *local, size_t capacity);
void detach_listbrowser(struct Gadget *gadget, struct Window *window);
void attach_listbrowser(struct Gadget *gadget, struct Window *window,
                        struct List *list);
struct Node *find_node_by_user_data(struct List *list, ULONG user_data);
void set_scroller_full(struct Window *window, struct Gadget *scroller);



/* Window persistence and ENV status. */
void gui_state_prepare_window(AmgGui *gui);
void gui_state_save_window(const AmgGui *gui);
void gui_state_set_mail_status_active(void);
void gui_state_set_mail_status_inactive(void);
void gui_state_set_inbox_unseen(AmgGui *gui, unsigned long count);
void gui_state_adjust_inbox_unseen(AmgGui *gui, long delta);

/* GitHub release check/download GUI integration. */
void gui_update_refresh_gadget(AmgGui *gui);
void gui_update_request_check(AmgGui *gui);
void gui_update_handle_check(AmgGui *gui, const AmgNetworkEvent *event);
void gui_update_start_download(AmgGui *gui, AmgError *error);
void gui_update_handle_download(AmgGui *gui, const AmgNetworkEvent *event);

/* mailto: integration module entry points. Private to src/gui_*.c. */
int open_mailto_compose(AmgGui *gui, const char *url, AmgError *error);
void handle_mailto_requests(AmgGui *gui, AmgMailtoServer *server,
                            AmgError *error);

/* GUI controller/action module entry points. Private to src/gui_*.c. */
void handle_network(AmgGui *gui);
void periodic_fetch_mail(AmgGui *gui, AmgError *error);
void fetch_mail(AmgGui *gui, AmgError *error);
void cancel_pending_move(AmgGui *gui);
void handle_main_gadget(AmgGui *gui, ULONG gadget_id, AmgError *error);
void handle_menu(AmgGui *gui, ULONG menu_code, AmgError *error);

/* Main-window/layout/rendering module entry points. Private to src/gui_*.c. */
int create_window(AmgGui *gui, AmgError *error);
void center_window_on_screen(struct Window *window);
void draw_window_overlays(AmgGui *gui);

/* Preview/received-attachment module entry points. Private to src/gui_*.c. */
void init_preview_url_hook(AmgGui *gui);
void open_pending_preview_url(AmgGui *gui);
void sync_preview_scroller(AmgGui *gui, int reset_top);
void handle_preview_scroller(AmgGui *gui);
void set_preview_local(AmgGui *gui, const char *local);
int display_message_payload(AmgGui *gui, const unsigned char *payload,
                            size_t payload_length, AmgError *error);
void clear_current_message_payload(AmgGui *gui);
void retain_current_message_payload(AmgGui *gui,
                                    const AmgNetworkEvent *event);
void save_current_attachments(AmgGui *gui);

/* Message-list module entry points. Private to src/gui_*.c. */
struct Node *message_placeholder_node(const char *text);
void default_messages(AmgGui *gui);
void show_message_placeholder(AmgGui *gui, const char *text);
size_t message_uid_stats(const unsigned char *payload, size_t length,
                         unsigned long baseline, unsigned long *max_uid,
                         int *parse_error);
size_t message_unseen_count_from_payload(const unsigned char *payload,
                                         size_t length, int *parse_error);
int message_list_uses_recipient(const AmgGui *gui, size_t label_index);
void set_message_party_column_mode(AmgGui *gui, int recipient);
size_t update_messages_from_payload(AmgGui *gui,
                                    const unsigned char *payload,
                                    size_t length, int recipient,
                                    int *parse_error);
int selected_node_user_data(struct Gadget *gadget, ULONG *user_data);
int cursor_node_user_data(struct Gadget *gadget, ULONG *user_data);
size_t merge_new_messages_from_payload(AmgGui *gui,
                                       const unsigned char *payload,
                                       size_t length, int *parse_error);
size_t selected_message_uids(AmgGui *gui, ULONG *uids, size_t capacity);
ULONG *selected_message_uids_alloc(AmgGui *gui, size_t *count);
int message_is_seen(AmgGui *gui, ULONG uid);
int message_is_flagged(AmgGui *gui, ULONG uid);
void set_message_seen_visual(AmgGui *gui, ULONG uid, int seen);
void set_message_flagged_visual(AmgGui *gui, ULONG uid, int flagged);
void set_message_selected_visual(AmgGui *gui, ULONG uid);
void sync_messages_scroller(AmgGui *gui);
void handle_messages_scroller(AmgGui *gui);

/* Folder/label module entry points. Private to src/gui_*.c. */
int create_label_tree_images(AmgGui *gui);
void dispose_label_tree_images(AmgGui *gui);
void init_label_tree_render_hook(AmgGui *gui);
void rebuild_label_lists(AmgGui *gui);
void default_labels(AmgGui *gui);
size_t update_labels_from_payload(AmgGui *gui,
                                  const unsigned char *payload,
                                  size_t length);
void select_label_index(AmgGui *gui, size_t index);
void sync_labels_scroller(AmgGui *gui);
void handle_labels_scroller(AmgGui *gui);
size_t label_index_for_mailbox(const AmgGui *gui,
                               const char *mailbox_utf8);
int current_folder_is_drafts(const AmgGui *gui);
size_t label_index_for_gmail_label(const AmgGui *gui,
                                   const char *gmail_label_utf8);
int handle_label_tree_event(AmgGui *gui);

/* Compose/reply/draft module entry points.  Private to src/gui_*.c. */
void finish_pending_temp_cleanup(AmgGui *gui, AmgNetCommandType type,
                                 unsigned long uid);
void cleanup_all_pending_temp_files(AmgGui *gui);
void cleanup_draft_seed(ComposeDraftSeed *seed);
int prepare_draft_payload(AmgGui *gui, const unsigned char *payload,
                          size_t payload_length, unsigned long uid,
                          const char *mailbox_utf8, ComposeDraftSeed *seed,
                          AmgError *error);
int prepare_reply_payload(AmgGui *gui, const unsigned char *payload,
                          size_t payload_length, AmgError *error);
int compose_dialog(AmgGui *gui, ComposeMode mode,
                   ComposeDraftSeed *draft_seed, AmgError *error);

/* Dialog module entry points.  These are intentionally not public API. */
int account_is_locked(const AmgAccount *account);
int account_dialog(AmgGui *gui, AmgError *error);
void about_dialog(AmgGui *gui);
int confirm_question_dialog_for_window(AmgGui *gui,
                                       struct Window *ref_window,
                                       const char *question,
                                       const char *note, LONG width);
int confirm_delete_dialog(AmgGui *gui);
int confirm_empty_trash_dialog(AmgGui *gui);
int confirm_empty_spam_dialog(AmgGui *gui);

#endif /* AMIGMAIL_AMIGA */

#endif /* AMIGMAIL_GUI_INTERNAL_H */
