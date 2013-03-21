#ifndef URL_H
#define URL_H

typedef struct 
{
  char *psz_protocol;
  char *psz_username;
  char *psz_password;
  char *psz_host;
  int i_port;
  char *psz_path;
  char *psz_option;
  char *psz_buffer;
} url_t;

char* ct_DecodeUri(char *psz);
void ct_UrlParse(url_t *url, const char *psz_url, char option);
void ct_UrlClean(url_t *url);
int ct_UrlIsNotEncoded(const char *psz_url);

#endif 
