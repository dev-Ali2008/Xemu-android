#include <stddef.h>

const char *g_libintl_bindtextdomain(const char *domain, const char *dir) {
  (void)domain;
  return dir;
}

const char *g_libintl_bind_textdomain_codeset(const char *domain, const char *codeset) {
  (void)domain;
  return codeset;
}

const char *g_libintl_dgettext(const char *domain, const char *msgid) {
  (void)domain;
  return msgid;
}

const char *g_libintl_dcgettext(const char *domain, const char *msgid, int category) {
  (void)domain;
  (void)category;
  return msgid;
}

const char *g_libintl_dngettext(const char *domain, const char *msgid1, const char *msgid2, unsigned long n) {
  (void)domain;
  return (n == 1) ? msgid1 : msgid2;
}

const char *g_libintl_gettext(const char *msgid) {
  return msgid;
}

const char *g_libintl_textdomain(const char *domain) {
  return domain ? domain : "messages";
}
