#ifndef AMIGMAIL_H
#define AMIGMAIL_H

#include <stddef.h>
#include <stdint.h>

#define AMIGMAIL_NAME "AmiGmail"
#define AMIGMAIL_VERSION "1.7"
#define AMIGMAIL_PAGE_SIZE 50U
#define AMIGMAIL_MAX_LINE (256UL * 1024UL)
#define AMIGMAIL_MAX_MESSAGE (8UL * 1024UL * 1024UL)
#define AMIGMAIL_MAX_LABELS 256U
#define AMIGMAIL_MAX_HEADERS 2048U

#if defined(__amigaos__) || defined(__AMIGA__)
#define AMIGMAIL_AMIGA 1
#else
#define AMIGMAIL_AMIGA 0
#endif

typedef enum AmgResult {
    AMG_OK = 0,
    AMG_ERR_ARGUMENT = -1,
    AMG_ERR_MEMORY = -2,
    AMG_ERR_IO = -3,
    AMG_ERR_PROTOCOL = -4,
    AMG_ERR_TLS = -5,
    AMG_ERR_AUTH = -6,
    AMG_ERR_PARSE = -7,
    AMG_ERR_LIMIT = -8,
    AMG_ERR_UNSUPPORTED = -9,
    AMG_ERR_CANCELLED = -10
} AmgResult;

typedef enum AmgAuthMode {
    AMG_AUTH_OAUTH2 = 0,
    AMG_AUTH_APP_PASSWORD = 1
} AmgAuthMode;

typedef struct AmgError {
    int code;
    char message[256];
} AmgError;

void amg_error_set(AmgError *error, int code, const char *message);
void amg_secure_clear(void *data, size_t size);

#endif
