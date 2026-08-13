#ifndef AMIGMAIL_GUI_H
#define AMIGMAIL_GUI_H

#include "account.h"

typedef struct AmgGui AmgGui;

AmgGui *amg_gui_create(AmgAccount *account, AmgError *error);
int amg_gui_run(AmgGui *gui, AmgError *error);
void amg_gui_destroy(AmgGui *gui);

#endif
