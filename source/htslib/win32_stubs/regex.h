// Stub regex.h for Windows builds
// htslib 1.21 uses regex for filter expressions. STAR doesn't use hts_expr filters.
#ifndef _REGEX_H_WIN32_STUB
#define _REGEX_H_WIN32_STUB

#define REG_EXTENDED 1
#define REG_ICASE    2
#define REG_NOSUB    4
#define REG_NOMATCH  1

typedef struct {
    void *re_pcre;
    size_t re_nsub;
} regex_t;

typedef size_t regoff_t;

typedef struct {
    regoff_t rm_so;
    regoff_t rm_eo;
} regmatch_t;

static __inline int regcomp(regex_t *preg, const char *pattern, int cflags) {
    (void)preg; (void)pattern; (void)cflags;
    return REG_NOMATCH; // always fail - STAR doesn't use regex filters
}

static __inline int regexec(const regex_t *preg, const char *string, size_t nmatch, regmatch_t pmatch[], int eflags) {
    (void)preg; (void)string; (void)nmatch; (void)pmatch; (void)eflags;
    return REG_NOMATCH;
}

static __inline void regfree(regex_t *preg) { (void)preg; }

static __inline size_t regerror(int errcode, const regex_t *preg, char *errbuf, size_t errbuf_size) {
    (void)errcode; (void)preg;
    if (errbuf && errbuf_size > 0) errbuf[0] = '\0';
    return 0;
}

#endif
