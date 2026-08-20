#include "storage.h"
#include "buffer.h"
#include "crypto.h"
#include "tls.h"
#include "i18n.h"

#define T(de, en) amg_tr((de), (en))

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if AMIGMAIL_AMIGA
#include <proto/dos.h>
#include <proto/amissl.h>
#include <openssl/evp.h>
#endif

/*
 * AmiGmail 1.8 wrote 100000 PBKDF2-HMAC-SHA256 iterations and also hardcoded
 * the same value while reading, despite already storing an iterations= field.
 * On slow/emulated classic 68k systems that synchronous KDF can make the
 * ReAction account requester look completely frozen.
 *
 * New files use a classic-68k-friendly value.  The loader honours the value
 * stored in each file and falls back to the original 100000 iterations when
 * reading legacy files that do not contain the field.  Existing 1.8 account
 * files therefore remain compatible.
 */
#define STORAGE_LEGACY_ITERATIONS 100000UL
#define STORAGE_WRITE_ITERATIONS 5000UL
#define STORAGE_MIN_ITERATIONS 1000UL
#define STORAGE_MAX_ITERATIONS 1000000UL
#define STORAGE_HEADER "AMIGMAIL-ACCOUNT-1\n"

static const char hex_digits[]="0123456789abcdef";

static int hex_encode(const unsigned char *data,size_t length,AmgBuffer *output)
{
    size_t i;for(i=0;i<length;++i){char pair[2]={hex_digits[data[i]>>4],hex_digits[data[i]&15U]};if(amg_buffer_append(output,pair,2U)!=AMG_OK)return AMG_ERR_MEMORY;}return AMG_OK;
}

static int hex_digit(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int hex_decode(const char *text,AmgBuffer *output)
{
    size_t i,length=strlen(text);if(length&1U)return AMG_ERR_PARSE;for(i=0;i<length;i+=2U){int h=hex_digit(text[i]),l=hex_digit(text[i+1U]);unsigned char value;if(h<0||l<0)return AMG_ERR_PARSE;value=(unsigned char)((h<<4)|l);if(amg_buffer_append_char(output,value)!=AMG_OK)return AMG_ERR_MEMORY;}return AMG_OK;
}

static int write_hex_line(FILE *file,const char *name,const unsigned char *data,size_t length)
{
    AmgBuffer encoded;int result;amg_buffer_init(&encoded);result=hex_encode(data,length,&encoded);if(result==AMG_OK){amg_buffer_terminate(&encoded);if(fprintf(file,"%s=%s\n",name,(char*)encoded.data)<0)result=AMG_ERR_IO;}amg_buffer_free(&encoded);return result;
}

static int replace_file(const char *temporary,const char *path)
{
#if AMIGMAIL_AMIGA
    DeleteFile((CONST_STRPTR)path);
    return Rename((CONST_STRPTR)temporary,(CONST_STRPTR)path)?AMG_OK:AMG_ERR_IO;
#else
    remove(path);
    return rename(temporary,path)==0?AMG_OK:AMG_ERR_IO;
#endif
}

static void discard_file(const char *path)
{
#if AMIGMAIL_AMIGA
    DeleteFile((CONST_STRPTR)path);
#else
    remove(path);
#endif
}

#if AMIGMAIL_AMIGA
static int encrypt_secrets(const char *master, const unsigned char *plain,
                           size_t plain_length, unsigned long iterations,
                           unsigned char salt[16], unsigned char iv[12],
                           unsigned char tag[16], AmgBuffer *cipher,
                           AmgError *error)
{
    unsigned char key[32];
    EVP_CIPHER_CTX *ctx = NULL;
    int out = 0, total = 0, result;

    memset(key, 0, sizeof(key));
    amg_error_set(error, AMG_OK, "");
    result = amg_tls_global_init(error);
    if (result != AMG_OK) return result;

    result = AMG_ERR_TLS;
    if (amg_random_bytes(salt, 16U) != AMG_OK ||
        amg_random_bytes(iv, 12U) != AMG_OK)
        goto done;
    if (PKCS5_PBKDF2_HMAC(master, (int)strlen(master), salt, 16,
                          (int)iterations, EVP_sha256(), 32, key) != 1)
        goto done;
    ctx = EVP_CIPHER_CTX_new();
    if (!ctx ||
        EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1)
        goto done;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL) != 1 ||
        EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) != 1)
        goto done;
    if (amg_buffer_reserve(cipher, plain_length + 32U) != AMG_OK) {
        result = AMG_ERR_MEMORY;
        goto done;
    }
    if (EVP_EncryptUpdate(ctx, cipher->data, &out, plain,
                          (int)plain_length) != 1)
        goto done;
    total = out;
    if (EVP_EncryptFinal_ex(ctx, cipher->data + total, &out) != 1)
        goto done;
    total += out;
    cipher->length = (size_t)total;
    cipher->data[cipher->length] = 0;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) != 1)
        goto done;
    result = AMG_OK;

done:
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    amg_secure_clear(key, sizeof(key));
    amg_tls_global_cleanup();
    if (result == AMG_ERR_MEMORY)
        amg_error_set(error, result, T("Nicht genug Speicher.", "Not enough memory."));
    else if (result != AMG_OK)
        amg_error_set(error, result,
                      T("AmiSSL konnte die Kontodaten nicht verschl\303\274sseln.", "AmiSSL could not encrypt the account data."));
    return result;
}

static int decrypt_secrets(const char *master, const unsigned char *cipher,
                           size_t cipher_length, unsigned long iterations,
                           const unsigned char salt[16],
                           const unsigned char iv[12],
                           const unsigned char tag[16], AmgBuffer *plain,
                           AmgError *error)
{
    unsigned char key[32];
    EVP_CIPHER_CTX *ctx = NULL;
    int out = 0, total = 0, result;

    memset(key, 0, sizeof(key));
    amg_error_set(error, AMG_OK, "");
    result = amg_tls_global_init(error);
    if (result != AMG_OK) return result;

    result = AMG_ERR_AUTH;
    if (PKCS5_PBKDF2_HMAC(master, (int)strlen(master), salt, 16,
                          (int)iterations, EVP_sha256(), 32, key) != 1)
        goto done;
    ctx = EVP_CIPHER_CTX_new();
    if (!ctx ||
        EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1)
        goto done;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL) != 1 ||
        EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv) != 1)
        goto done;
    if (amg_buffer_reserve(plain, cipher_length + 1U) != AMG_OK) {
        result = AMG_ERR_MEMORY;
        goto done;
    }
    if (EVP_DecryptUpdate(ctx, plain->data, &out, cipher,
                          (int)cipher_length) != 1)
        goto done;
    total = out;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void *)tag) != 1 ||
        EVP_DecryptFinal_ex(ctx, plain->data + total, &out) != 1)
        goto done;
    total += out;
    plain->length = (size_t)total;
    plain->data[plain->length] = 0;
    result = AMG_OK;

done:
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    amg_secure_clear(key, sizeof(key));
    amg_tls_global_cleanup();
    if (result == AMG_ERR_MEMORY)
        amg_error_set(error, result, T("Nicht genug Speicher.", "Not enough memory."));
    return result;
}
#endif

int amg_storage_save_account(const char *path,const AmgAccount *account,const char *master_password,AmgError *error)
{
    char temporary[512];FILE *file;int result=AMG_OK;AmgBuffer plain,cipher;
#if AMIGMAIL_AMIGA
    unsigned char salt[16],iv[12],tag[16];
#endif
    amg_error_set(error,AMG_OK,"");
    if (!path || !account) return AMG_ERR_ARGUMENT;
    if (strlen(path) + 5U >= sizeof(temporary)) return AMG_ERR_LIMIT;
    snprintf(temporary,sizeof(temporary),"%s.new",path);
    file=fopen(temporary,"wb");if(!file){amg_error_set(error,AMG_ERR_IO,T("Kontodatei konnte nicht geschrieben werden.", "Account file could not be written."));return AMG_ERR_IO;}
    amg_buffer_init(&plain);amg_buffer_init(&cipher);fprintf(file,"%s",STORAGE_HEADER);
    if(write_hex_line(file,"display_name",(const unsigned char*)account->display_name,strlen(account->display_name))!=AMG_OK||
       write_hex_line(file,"email",(const unsigned char*)account->email,strlen(account->email))!=AMG_OK||
       fprintf(file,"auth_mode=%d\nimap_host=%s\nimap_port=%u\nsmtp_host=%s\nsmtp_port=%u\nsmtp_starttls=%d\nfetch_on_start=%d\nperiodic_fetch=%d\nfetch_days=%u\nnotification_sound=%d\n",
       (int)account->auth_mode,account->imap_host,(unsigned)account->imap_port,account->smtp_host,(unsigned)account->smtp_port,account->smtp_starttls,account->fetch_on_start?1:0,account->periodic_fetch?1:0,account->fetch_days?account->fetch_days:180U,account->notification_sound?1:0)<0)result=AMG_ERR_IO;
    if(result==AMG_OK)result=write_hex_line(file,"notification_sound_path",
        (const unsigned char*)account->notification_sound_path,
        strlen(account->notification_sound_path));
    if(result==AMG_OK&&master_password&&*master_password){
        result=write_hex_line(file,"remembered_master",
                              (const unsigned char*)master_password,
                              strlen(master_password));
        if(result==AMG_OK){amg_buffer_append_cstr(&plain,"app_password=");if(account->app_password)hex_encode((unsigned char*)account->app_password,strlen(account->app_password),&plain);
            amg_buffer_append_cstr(&plain,"\nrefresh_token=");if(account->refresh_token)hex_encode((unsigned char*)account->refresh_token,strlen(account->refresh_token),&plain);amg_buffer_append_char(&plain,'\n');}
#if AMIGMAIL_AMIGA
        if(result==AMG_OK)result=encrypt_secrets(master_password,plain.data,plain.length,STORAGE_WRITE_ITERATIONS,salt,iv,tag,&cipher,error);
        if(result==AMG_OK){fprintf(file,"secrets=aes-256-gcm\niterations=%lu\n",STORAGE_WRITE_ITERATIONS);result=write_hex_line(file,"salt",salt,16U);}
        if(result==AMG_OK)result=write_hex_line(file,"iv",iv,12U);
        if(result==AMG_OK)result=write_hex_line(file,"tag",tag,16U);
        if(result==AMG_OK)result=write_hex_line(file,"ciphertext",cipher.data,cipher.length);
#else
        result=AMG_ERR_UNSUPPORTED;
#endif
    }else if(result==AMG_OK)fprintf(file,"secrets=session-only\n");
    amg_secure_clear(plain.data,plain.capacity);amg_secure_clear(cipher.data,cipher.capacity);amg_buffer_free(&plain);amg_buffer_free(&cipher);
    if (fclose(file) != 0 && result == AMG_OK) result = AMG_ERR_IO;
    if(result==AMG_OK)result=replace_file(temporary,path);
    if(result!=AMG_OK)discard_file(temporary);
    if(result==AMG_OK)amg_error_set(error,AMG_OK,"");
    else if(!error||error->code==AMG_OK)
        amg_error_set(error,result,T("Kontodatei konnte nicht sicher gespeichert werden.", "Account file could not be saved securely."));
    return result;
}

static char *read_all(const char *path,size_t *length)
{
    FILE *file=fopen(path,"rb");long size;char *data;if(!file)return NULL;if(fseek(file,0,SEEK_END)||((size=ftell(file))<0)||fseek(file,0,SEEK_SET)){fclose(file);return NULL;}
    data=(char*)malloc((size_t)size+1U);if(!data){fclose(file);return NULL;}if(fread(data,1U,(size_t)size,file)!=(size_t)size){free(data);fclose(file);return NULL;}fclose(file);data[size]=0;*length=(size_t)size;return data;
}

static const char *field(const char *data,const char *name,char *value,size_t size)
{
    size_t n=strlen(name);const char *p=data;while((p=strstr(p,name))!=NULL){if((p==data||p[-1]=='\n')&&p[n]=='='){const char *start=p+n+1U,*end=strchr(start,'\n');size_t len;if(!end)end=start+strlen(start);len=(size_t)(end-start);if(len&&start[len-1U]=='\r')--len;if(len>=size)len=size-1U;memcpy(value,start,len);value[len]=0;return value;}p+=n;}return NULL;
}

int amg_storage_load_remembered_master(const char *path, char *output,
                                       size_t capacity)
{
    size_t length;
    char *data;
    char value[512];
    AmgBuffer decoded;
    int result = AMG_ERR_IO;
    if (!path || !output || capacity < 2U) return AMG_ERR_ARGUMENT;
    output[0] = 0;
    data = read_all(path, &length);
    (void)length;
    if (!data) return AMG_ERR_IO;
    amg_buffer_init(&decoded);
    if (!strncmp(data, STORAGE_HEADER, sizeof(STORAGE_HEADER) - 1U) &&
        field(data, "remembered_master", value, sizeof(value)) &&
        hex_decode(value, &decoded) == AMG_OK &&
        decoded.length < capacity) {
        memcpy(output, decoded.data, decoded.length);
        output[decoded.length] = 0;
        result = AMG_OK;
    }
    amg_secure_clear(decoded.data, decoded.capacity);
    amg_buffer_free(&decoded);
    amg_secure_clear(data, strlen(data));
    free(data);
    return result;
}

int amg_storage_load_account(const char *path,const char *master_password,AmgAccount *account,AmgError *error)
{
    size_t length;char *data,*line;char value[2048];AmgBuffer decoded;int result=AMG_OK;
    char remembered_master[128];
    const char *effective_master=master_password;
    amg_error_set(error,AMG_OK,"");
    if (!path || !account) return AMG_ERR_ARGUMENT;
    data=read_all(path,&length);(void)length;
    if(!data){amg_error_set(error,AMG_ERR_IO,T("Es wurde noch keine Kontodatei gespeichert. Bitte zuerst Speichern verwenden.", "No account file has been saved yet. Please use Save first."));return AMG_ERR_IO;}
    if(strncmp(data,STORAGE_HEADER,sizeof(STORAGE_HEADER)-1U)){
        free(data);
        amg_error_set(error,AMG_ERR_PARSE,T("Kontodatei ist ung\303\274ltig.", "Account file is invalid."));
        return AMG_ERR_PARSE;
    }
    amg_account_init(account);amg_buffer_init(&decoded);
    if(field(data,"display_name",value,sizeof(value))&&hex_decode(value,&decoded)==AMG_OK){amg_buffer_terminate(&decoded);snprintf(account->display_name,sizeof(account->display_name),"%s",(char*)decoded.data);}decoded.length=0;
    if(field(data,"email",value,sizeof(value))&&hex_decode(value,&decoded)==AMG_OK){amg_buffer_terminate(&decoded);snprintf(account->email,sizeof(account->email),"%s",(char*)decoded.data);}decoded.length=0;
    if (field(data,"auth_mode",value,sizeof(value))) account->auth_mode=(AmgAuthMode)atoi(value);
    if (field(data,"imap_host",value,sizeof(value))) {
        strncpy(account->imap_host,value,sizeof(account->imap_host)-1U);
        account->imap_host[sizeof(account->imap_host)-1U]=0;
    }
    if (field(data,"imap_port",value,sizeof(value))) account->imap_port=(unsigned short)atoi(value);
    if (field(data,"smtp_host",value,sizeof(value))) {
        strncpy(account->smtp_host,value,sizeof(account->smtp_host)-1U);
        account->smtp_host[sizeof(account->smtp_host)-1U]=0;
    }
    if (field(data,"smtp_port",value,sizeof(value))) account->smtp_port=(unsigned short)atoi(value);
    if (field(data,"smtp_starttls",value,sizeof(value))) account->smtp_starttls=atoi(value);
    if (field(data,"fetch_on_start",value,sizeof(value))) account->fetch_on_start=atoi(value)?1:0;
    if (field(data,"periodic_fetch",value,sizeof(value))) account->periodic_fetch=atoi(value)?1:0;
    if (field(data,"fetch_days",value,sizeof(value))) {
        unsigned long days=strtoul(value,NULL,10);
        if(days>=1UL&&days<=3650UL)account->fetch_days=(unsigned int)days;
    }
    if (field(data,"notification_sound",value,sizeof(value)))
        account->notification_sound=atoi(value)?1:0;
    if(field(data,"notification_sound_path",value,sizeof(value))){
        decoded.length=0;
        if(hex_decode(value,&decoded)==AMG_OK&&
           amg_buffer_terminate(&decoded)==AMG_OK){
            strncpy(account->notification_sound_path,(char*)decoded.data,
                    sizeof(account->notification_sound_path)-1U);
            account->notification_sound_path[
                sizeof(account->notification_sound_path)-1U]=0;
        }
    }
    remembered_master[0]=0;
    if ((!effective_master||!*effective_master) &&
        field(data,"remembered_master",value,sizeof(value))) {
        decoded.length=0;
        if(hex_decode(value,&decoded)==AMG_OK&&decoded.length<sizeof(remembered_master)){
            memcpy(remembered_master,decoded.data,decoded.length);
            remembered_master[decoded.length]=0;
            effective_master=remembered_master;
        }
    }
    if(field(data,"secrets",value,sizeof(value))&&!strcmp(value,"aes-256-gcm")){
        AmgBuffer salt,iv,tag,cipher,plain;
        unsigned long iterations=STORAGE_LEGACY_ITERATIONS;
        char *iteration_end=NULL;
        amg_buffer_init(&salt);amg_buffer_init(&iv);amg_buffer_init(&tag);amg_buffer_init(&cipher);amg_buffer_init(&plain);
        if(field(data,"iterations",value,sizeof(value))){
            unsigned long parsed=strtoul(value,&iteration_end,10);
            if(!value[0]||!iteration_end||*iteration_end||
               parsed<STORAGE_MIN_ITERATIONS||parsed>STORAGE_MAX_ITERATIONS)
                result=AMG_ERR_PARSE;
            else
                iterations=parsed;
        }
        if(result==AMG_OK&&(!effective_master||!*effective_master))result=AMG_ERR_AUTH;
        else if(result==AMG_OK&&(!field(data,"salt",value,sizeof(value))||hex_decode(value,&salt)!=AMG_OK||!field(data,"iv",value,sizeof(value))||hex_decode(value,&iv)!=AMG_OK||
                !field(data,"tag",value,sizeof(value))||hex_decode(value,&tag)!=AMG_OK||!field(data,"ciphertext",value,sizeof(value))||hex_decode(value,&cipher)!=AMG_OK||salt.length!=16U||iv.length!=12U||tag.length!=16U))result=AMG_ERR_PARSE;
#if AMIGMAIL_AMIGA
        else if(result==AMG_OK)result=decrypt_secrets(effective_master,cipher.data,cipher.length,iterations,salt.data,iv.data,tag.data,&plain,error);
#else
        else if(result==AMG_OK)result=AMG_ERR_UNSUPPORTED;
#endif
        if(result==AMG_OK){amg_buffer_terminate(&plain);line=(char*)plain.data;if(field(line,"app_password",value,sizeof(value))){decoded.length=0;if(hex_decode(value,&decoded)==AMG_OK){amg_buffer_terminate(&decoded);amg_account_set_secret(&account->app_password,(char*)decoded.data);}}
            if(field(line,"refresh_token",value,sizeof(value))){decoded.length=0;if(hex_decode(value,&decoded)==AMG_OK){amg_buffer_terminate(&decoded);amg_account_set_secret(&account->refresh_token,(char*)decoded.data);}}}
        amg_secure_clear(plain.data,plain.capacity);amg_buffer_free(&salt);amg_buffer_free(&iv);amg_buffer_free(&tag);amg_buffer_free(&cipher);amg_buffer_free(&plain);
    }
    amg_secure_clear(remembered_master,sizeof(remembered_master));
    amg_secure_clear(decoded.data,decoded.capacity);amg_buffer_free(&decoded);amg_secure_clear(data,strlen(data));free(data);
    if(result==AMG_OK)amg_error_set(error,AMG_OK,"");
    else if(result==AMG_ERR_AUTH)amg_error_set(error,result,T("Master-Passwort fehlt oder ist falsch.", "Master password is missing or incorrect."));
    else if(!error||error->code==AMG_OK)amg_error_set(error,result,T("Kontodatei ist ung\303\274ltig.", "Account file is invalid."));
    return result;
}
