#include "account.h"
#include "codec.h"
#include "crypto.h"
#include "imap_parser.h"
#include "i18n.h"
#include "mime.h"
#include "oauth.h"
#include "smtp.h"
#include "storage.h"
#include "tls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned tests_run;
static unsigned tests_failed;

#define CHECK(expression) do { ++tests_run; if (!(expression)) { \
    ++tests_failed; fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#expression); } } while(0)

static const char *text(AmgBuffer *buffer){amg_buffer_terminate(buffer);return (const char*)buffer->data;}

static void test_base64(void)
{
    AmgBuffer encoded,decoded;amg_buffer_init(&encoded);amg_buffer_init(&decoded);
    CHECK(amg_base64_encode((const unsigned char*)"hello",5U,&encoded)==AMG_OK);CHECK(!strcmp(text(&encoded),"aGVsbG8="));
    CHECK(amg_base64_decode((const char*)encoded.data,encoded.length,&decoded)==AMG_OK);CHECK(decoded.length==5U&&!memcmp(decoded.data,"hello",5U));
    CHECK(amg_base64_decode("@@",2U,&decoded)==AMG_ERR_PARSE);amg_buffer_free(&encoded);amg_buffer_free(&decoded);
}

static void test_quoted_printable(void)
{
    AmgBuffer output;amg_buffer_init(&output);CHECK(amg_quoted_printable_decode("Gr=C3=BC=C3=9Fe=\r\n!",21U,&output)==AMG_OK);
    CHECK(!strcmp(text(&output),"Grüße!"));amg_buffer_free(&output);
}

static void test_utf7(void)
{
    static const char *samples[]={"INBOX","Reisen & Urlaub","Österreich/Grüße","日本語"};size_t i;
    for(i=0;i<sizeof(samples)/sizeof(samples[0]);++i){AmgBuffer wire,back;amg_buffer_init(&wire);amg_buffer_init(&back);
        CHECK(amg_modified_utf7_encode(samples[i],&wire)==AMG_OK);CHECK(amg_modified_utf7_decode(text(&wire),&back)==AMG_OK);CHECK(!strcmp(text(&back),samples[i]));
        amg_buffer_free(&wire);amg_buffer_free(&back);}
    {AmgBuffer output;amg_buffer_init(&output);CHECK(amg_modified_utf7_encode("&",&output)==AMG_OK);CHECK(!strcmp(text(&output),"&-"));amg_buffer_free(&output);}
}

static void test_imap_parser(void)
{
    AmgImapParser parser;AmgImapEvent event;const char *chunks[]={"* 1 FETCH (BODY[] {","5}\r\nhe","llo)\r\nA1 OK done\r\n"};int result;
    amg_imap_parser_init(&parser);CHECK(amg_imap_parser_feed(&parser,chunks[0],strlen(chunks[0]))==AMG_OK);CHECK(amg_imap_parser_next(&parser,&event)==0);
    CHECK(amg_imap_parser_feed(&parser,chunks[1],strlen(chunks[1]))==AMG_OK);result=amg_imap_parser_next(&parser,&event);CHECK(result==1&&event.type==AMG_IMAP_EVENT_LINE);
    CHECK(amg_imap_parser_next(&parser,&event)==0);CHECK(amg_imap_parser_feed(&parser,chunks[2],strlen(chunks[2]))==AMG_OK);
    result=amg_imap_parser_next(&parser,&event);CHECK(result==1&&event.type==AMG_IMAP_EVENT_LITERAL&&event.length==5U&&!memcmp(event.data,"hello",5U));
    result=amg_imap_parser_next(&parser,&event);CHECK(result==1&&event.type==AMG_IMAP_EVENT_LINE);
    result=amg_imap_parser_next(&parser,&event);CHECK(result==1&&event.type==AMG_IMAP_EVENT_LINE);amg_imap_parser_free(&parser);
    {size_t literal=0;CHECK(amg_imap_parse_literal_length((const unsigned char*)"x {123+}\r\n",10U,&literal)==1&&literal==123U);}
    {size_t literal=175536460U;CHECK(amg_imap_parse_literal_length((const unsigned char*)"A1 OK done\r\n",12U,&literal)==0&&literal==0U);}
    CHECK(amg_imap_greeting_is_success(
        (const unsigned char *)"* OK Gimap ready for requests\r\n",31U));
    CHECK(amg_imap_greeting_is_success(
        (const unsigned char *)"  * PREAUTH ready\r\n",19U));
    CHECK(!amg_imap_greeting_is_success(
        (const unsigned char *)"* BYE unavailable\r\n",19U));
    CHECK(amg_imap_greeting_status(
        (const unsigned char *)"* OK Gimap",10U)==1);
    CHECK(amg_imap_greeting_status(
        (const unsigned char *)"\xef\xbb\xbf\t* PREAUTH ready\n",20U)==1);
    CHECK(amg_imap_greeting_status(
        (const unsigned char *)"* BYE unavailable\n",18U)==-1);
    CHECK(amg_imap_greeting_status(
        (const unsigned char *)"* O",3U)==0);
    {
        const char *select_response =
            "* FLAGS (\\Seen)\r\n* 12345 EXISTS\r\nA000001 OK selected\r\n";
        unsigned long exists = 0;
        CHECK(amg_imap_parse_exists(
            (const unsigned char *)select_response,
            strlen(select_response), &exists) == 1);
        CHECK(exists == 12345UL);
    }
    {
        const char *fetch_response =
            "* 9876 FETCH (UID 456789 FLAGS (\\Seen))\r\n"
            "A000002 OK fetched\r\n";
        unsigned long sequence = 0;
        CHECK(amg_imap_parse_fetch_sequence(
            (const unsigned char *)fetch_response,
            strlen(fetch_response), 456789UL, &sequence) == 1);
        CHECK(sequence == 9876UL);
        CHECK(!amg_imap_parse_fetch_sequence(
            (const unsigned char *)fetch_response,
            strlen(fetch_response), 111UL, &sequence));
    }
    {
        const char *header1 =
            "From: Alice <alice@example.com>\r\nSubject: First\r\n\r\n";
        const char *header2 =
            "From: Bob <bob@example.com>\r\nSubject: Second\r\n\r\n";
        char prefix[256];
        AmgBuffer response;
        AmgImapFetchRecord record;
        size_t position = 0;
        amg_buffer_init(&response);
        snprintf(prefix, sizeof(prefix),
                 "* 49 FETCH (UID 9001 FLAGS (\\Seen \\Flagged) "
                 "RFC822.SIZE 2049 BODY[HEADER.FIELDS (FROM SUBJECT)] {%lu}\r\n",
                 (unsigned long)strlen(header1));
        CHECK(amg_buffer_append_cstr(&response, prefix) == AMG_OK);
        CHECK(amg_buffer_append_cstr(&response, header1) == AMG_OK);
        CHECK(amg_buffer_append_cstr(&response, ")\r\n") == AMG_OK);
        snprintf(prefix, sizeof(prefix),
                 "* 50 FETCH (UID 9002 FLAGS () RFC822.SIZE 100 "
                 "BODY[HEADER.FIELDS (FROM SUBJECT)] {%lu}\r\n",
                 (unsigned long)strlen(header2));
        CHECK(amg_buffer_append_cstr(&response, prefix) == AMG_OK);
        CHECK(amg_buffer_append_cstr(&response, header2) == AMG_OK);
        CHECK(amg_buffer_append_cstr(&response,
                                     ")\r\nA000003 OK fetched\r\n") == AMG_OK);
        CHECK(amg_imap_fetch_record_next(response.data, response.length,
                                         &position, &record) == 1);
        CHECK(record.uid == 9001UL && record.rfc822_size == 2049UL);
        CHECK(record.seen && record.flagged);
        CHECK(record.literal_length == strlen(header1) &&
              !memcmp(record.literal, header1, strlen(header1)));
        CHECK(amg_imap_fetch_record_next(response.data, response.length,
                                         &position, &record) == 1);
        CHECK(record.uid == 9002UL && record.rfc822_size == 100UL);
        CHECK(!record.seen && !record.flagged);
        CHECK(record.literal_length == strlen(header2) &&
              !memcmp(record.literal, header2, strlen(header2)));
        CHECK(amg_imap_fetch_record_next(response.data, response.length,
                                         &position, &record) == 0);
        amg_buffer_free(&response);
    }
    {
        unsigned char *long_line = (unsigned char *)malloc(16386U);
        CHECK(long_line != NULL);
        if (long_line) {
            memset(long_line, 'X', 16384U);
            long_line[16384U] = '\r';
            long_line[16385U] = '\n';
            amg_imap_parser_init(&parser);
            CHECK(amg_imap_parser_feed(&parser, long_line, 16386U) == AMG_OK);
            CHECK(amg_imap_parser_next(&parser, &event) == 1);
            CHECK(event.length == 16386U);
            amg_imap_parser_free(&parser);
            free(long_line);
        }
    }
    {
        const char *oversized_literal = "* 1 FETCH (BODY[] {8388609}\r\n";
        amg_imap_parser_init(&parser);
        CHECK(amg_imap_parser_feed(&parser, oversized_literal,
                                   strlen(oversized_literal)) == AMG_OK);
        CHECK(amg_imap_parser_next(&parser, &event) == AMG_ERR_LIMIT);
        CHECK(parser.failure == AMG_IMAP_PARSER_FAILURE_LITERAL_LIMIT);
        CHECK(parser.failure_size == AMIGMAIL_MAX_MESSAGE + 1U);
        amg_imap_parser_free(&parser);
    }
}

static void test_storage_metadata(void)
{
    const char *path = "build/test-account.cfg";
    const char *master_path = "build/test-master.cfg";
    AmgAccount saved, loaded;
    AmgError error;
    char master[128];
    FILE *file;
    amg_account_init(&saved);
    amg_account_init(&loaded);
    strcpy(saved.display_name, "Andreas");
    strcpy(saved.email, "andreas@gmail.com");
    saved.fetch_on_start = 1;
    saved.periodic_fetch = 1;
    saved.fetch_days = 180U;
    CHECK(amg_storage_save_account(path, &saved, NULL, &error) == AMG_OK);
    CHECK(amg_storage_load_account(path, NULL, &loaded, &error) == AMG_OK);
    CHECK(!strcmp(loaded.display_name, saved.display_name));
    CHECK(!strcmp(loaded.email, saved.email));
    CHECK(loaded.fetch_on_start == 1);
    CHECK(loaded.periodic_fetch == 1);
    CHECK(loaded.fetch_days == 180U);
    CHECK(!strcmp(loaded.imap_host, "imap.gmail.com") &&
          loaded.imap_port == 993U);
    file=fopen(master_path,"wb");
    CHECK(file!=NULL);
    if(file){fputs("AMIGMAIL-ACCOUNT-1\nremembered_master=546573742d4d6173746572\n",file);fclose(file);}
    CHECK(amg_storage_load_remembered_master(
        master_path,master,sizeof(master))==AMG_OK);
    CHECK(!strcmp(master,"Test-Master"));
    amg_secure_clear(master,sizeof(master));
    remove(path);
    remove(master_path);
    amg_account_clear(&saved);
    amg_account_clear(&loaded);
}

static void test_headers_and_rfc2047(void)
{
    const char *source="Subject: =?UTF-8?B?R3LDvMOfZQ==?=\r\nX-Test: first\r\n\tsecond\r\n\r\nBody";AmgMailHeaders headers;size_t body;
    AmgBuffer decoded;amg_mail_headers_init(&headers);amg_buffer_init(&decoded);
    CHECK(amg_mail_headers_parse(source,strlen(source),&headers,&body)==AMG_OK);CHECK(!strcmp(amg_mail_header_get(&headers,"x-test"),"first second"));
    CHECK(!strcmp(source+body,"Body"));CHECK(amg_rfc2047_decode(amg_mail_header_get(&headers,"Subject"),&decoded)==AMG_OK);CHECK(!strcmp(text(&decoded),"Grüße"));
    amg_mail_headers_free(&headers);amg_buffer_free(&decoded);
}

static void test_mime(void)
{
    const char *message="Content-Type: multipart/alternative; boundary=abc\r\n\r\n--abc\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\nHallo=20Welt\r\n--abc\r\nContent-Type: text/html\r\n\r\n<b>Hallo</b>\r\n--abc--\r\n";
    const char *with_attachment="Content-Type: multipart/mixed; boundary=mix\r\n\r\n--mix\r\nContent-Type: text/plain; charset=UTF-8\r\n\r\nHallo\r\n--mix\r\nContent-Type: application/pdf; name=\"rechnung.pdf\"\r\nContent-Disposition: attachment; filename=\"rechnung.pdf\"\r\nContent-Transfer-Encoding: base64\r\n\r\nQUJD\r\n--mix--\r\n";
    AmgBuffer output,name,data;AmgError error;size_t attachment_count=0;amg_buffer_init(&output);CHECK(amg_mime_extract_text(message,strlen(message),&output,&error)==AMG_OK);CHECK(strstr(text(&output),"Hallo Welt")!=NULL);amg_buffer_free(&output);
    amg_buffer_init(&output);CHECK(amg_mime_attachment_summary(with_attachment,strlen(with_attachment),&output,&error)==AMG_OK);CHECK(strstr(text(&output),"rechnung.pdf")!=NULL);CHECK(strstr(text(&output),"application/pdf")!=NULL);amg_buffer_free(&output);
    CHECK(amg_mime_attachment_count(with_attachment,strlen(with_attachment),&attachment_count,&error)==AMG_OK);CHECK(attachment_count==1U);
    amg_buffer_init(&name);amg_buffer_init(&data);CHECK(amg_mime_extract_attachment(with_attachment,strlen(with_attachment),0U,&name,&data,&error)==AMG_OK);CHECK(!strcmp(text(&name),"rechnung.pdf"));CHECK(data.length==3U&&!memcmp(data.data,"ABC",3U));amg_buffer_free(&name);amg_buffer_free(&data);
    amg_buffer_init(&output);CHECK(amg_html_to_text("<p>A &amp; B</p><script>evil()</script><br>C",47U,&output)==AMG_OK);CHECK(strstr(text(&output),"evil")==NULL);CHECK(strstr((char*)output.data,"A & B")!=NULL);amg_buffer_free(&output);
}

static void test_smtp(void)
{
    AmgBuffer output,subject;AmgReplyDraft draft;AmgError error;memset(&draft,0,sizeof(draft));amg_buffer_init(&output);amg_buffer_init(&subject);
    CHECK(amg_smtp_dot_stuff("a\r\n.b\r\n..c\r\n",14U,&output)==AMG_OK);CHECK(!strcmp(text(&output),"a\r\n..b\r\n...c\r\n"));amg_buffer_free(&output);
    CHECK(amg_smtp_reply_subject("Re: Test",&subject)==AMG_OK);CHECK(!strcmp(text(&subject),"Re: Test"));amg_buffer_free(&subject);
    draft.from="me@gmail.com";draft.to="you@example.com";draft.subject="Test";draft.body_utf8="Hallo\n.Zeile";draft.in_reply_to="<old@example>";draft.references="<first@example> <old@example>";
    draft.date_rfc2822="Wed, 12 Aug 2026 10:00:00 +0200";draft.message_id="<new@gmail.com>";amg_buffer_init(&output);
    CHECK(amg_smtp_build_reply(&draft,&output,&error)==AMG_OK);CHECK(strstr(text(&output),"Subject: Re: Test\r\n")!=NULL);CHECK(strstr((char*)output.data,"\r\n..Zeile")!=NULL);amg_buffer_free(&output);
}

static void test_oauth(void)
{
    AmgOAuthTokens tokens;AmgError error;AmgOAuthConfig config={"id","","http://127.0.0.1:1234/","https://mail.google.com/"};AmgBuffer url;char verifier[129],challenge[129],state[65];
    memset(&tokens,0,sizeof(tokens));CHECK(amg_oauth_parse_token_json("{\"access_token\":\"abc\",\"expires_in\":3600,\"refresh_token\":\"def\"}",&tokens,&error)==AMG_OK);
    CHECK(!strcmp(tokens.access_token,"abc")&&!strcmp(tokens.refresh_token,"def")&&tokens.expires_in==3600UL);amg_oauth_tokens_clear(&tokens);
    CHECK(amg_oauth_parse_token_json("{\"error\":\"invalid_grant\"}",&tokens,&error)==AMG_ERR_AUTH);CHECK(strstr(error.message,"invalid_grant")!=NULL);
    CHECK(amg_oauth_generate_pkce(verifier,challenge,state,&error)==AMG_OK);CHECK(strlen(verifier)>=43U&&strlen(challenge)==43U&&strlen(state)>20U);
    amg_buffer_init(&url);CHECK(amg_oauth_build_authorize_url(&config,challenge,state,&url)==AMG_OK);CHECK(strstr(text(&url),"code_challenge_method=S256")!=NULL);CHECK(strstr((char*)url.data,"scope=https%3A%2F%2Fmail.google.com%2F")!=NULL);amg_buffer_free(&url);
}

static void test_sha256(void)
{
    static const unsigned char expected[32]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};unsigned char digest[32];
    amg_sha256((const unsigned char*)"abc",3U,digest);CHECK(!memcmp(digest,expected,32U));
}

static void test_account(void)
{
    AmgAccount account;AmgError error;amg_account_init(&account);strcpy(account.email,"user@gmail.com");amg_account_set_secret(&account.app_password,"abcd efgh ijkl mnop");
    CHECK(amg_account_validate(&account,&error)==AMG_OK);amg_account_set_secret(&account.app_password,"short");CHECK(amg_account_validate(&account,&error)==AMG_ERR_AUTH);amg_account_clear(&account);
}

static void test_i18n(void)
{
    char text_buffer[64];
    amg_i18n_init();
    CHECK(!amg_i18n_is_german());
    CHECK(!strcmp(amg_tr("Deutsch", "English"), "English"));
    amg_tr_snprintf(text_buffer, sizeof(text_buffer),
                    "%lu Nachricht", "%lu message", 3UL);
    CHECK(!strcmp(text_buffer, "3 message"));
}

int main(void)
{
    test_base64();test_quoted_printable();test_utf7();test_imap_parser();test_headers_and_rfc2047();test_mime();test_smtp();test_oauth();test_sha256();test_account();test_storage_metadata();test_i18n();
    printf("%u checks, %u failures\n",tests_run,tests_failed);return tests_failed?1:0;
}
