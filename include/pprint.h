#ifndef _PPRINT_H
#define _PPRINT_H

/*
 * color definitions and handy macros to print prettier messages
 *
 * This code is mostly from AFL's debug.h (american fuzzy lop, version 2.52b).
 */

#include <errno.h>

/* Terminal colors */

#ifdef PRINT_COLOR

#define cBLK "\x1b[0;30m"
#define cRED "\x1b[0;31m"
#define cGRN "\x1b[0;32m"
#define cBRN "\x1b[0;33m"
#define cBLU "\x1b[0;34m"
#define cMGN "\x1b[0;35m"
#define cCYA "\x1b[0;36m"
#define cLGR "\x1b[0;37m"
#define cGRA "\x1b[1;90m"
#define cLRD "\x1b[1;91m"
#define cLGN "\x1b[1;92m"
#define cYEL "\x1b[1;93m"
#define cLBL "\x1b[1;94m"
#define cPIN "\x1b[1;95m"
#define cLCY "\x1b[1;96m"
#define cBRI "\x1b[1;97m"
#define cRST "\x1b[0m"

#define bgBLK "\x1b[40m"
#define bgRED "\x1b[41m"
#define bgGRN "\x1b[42m"
#define bgBRN "\x1b[43m"
#define bgBLU "\x1b[44m"
#define bgMGN "\x1b[45m"
#define bgCYA "\x1b[46m"
#define bgLGR "\x1b[47m"
#define bgGRA "\x1b[100m"
#define bgLRD "\x1b[101m"
#define bgLGN "\x1b[102m"
#define bgYEL "\x1b[103m"
#define bgLBL "\x1b[104m"
#define bgPIN "\x1b[105m"
#define bgLCY "\x1b[106m"
#define bgBRI "\x1b[107m"

#else

#define cBLK ""
#define cRED ""
#define cGRN ""
#define cBRN ""
#define cBLU ""
#define cMGN ""
#define cCYA ""
#define cLGR ""
#define cGRA ""
#define cLRD ""
#define cLGN ""
#define cYEL ""
#define cLBL ""
#define cPIN ""
#define cLCY ""
#define cBRI ""
#define cRST ""

#define bgBLK ""
#define bgRED ""
#define bgGRN ""
#define bgBRN ""
#define bgBLU ""
#define bgMGN ""
#define bgCYA ""
#define bgLGR ""
#define bgGRA ""
#define bgLRD ""
#define bgLGN ""
#define bgYEL ""
#define bgLBL ""
#define bgPIN ""
#define bgLCY ""
#define bgBRI ""

#endif /* ^PPRINT */

/* Box drawing sequences */

#ifdef FANCY_BOXES

#define SET_G1 "\x1b)0"   /* Set G1 for box drawing    */
#define RESET_G1 "\x1b)B" /* Reset G1 to ASCII         */
#define bSTART "\x0e"     /* Enter G1 drawing mode     */
#define bSTOP "\x0f"      /* Leave G1 drawing mode     */
#define bH "q"            /* Horizontal line           */
#define bV "x"            /* Vertical line             */
#define bLT "l"           /* Left top corner           */
#define bRT "k"           /* Right top corner          */
#define bLB "m"           /* Left bottom corner        */
#define bRB "j"           /* Right bottom corner       */
#define bX "n"            /* Cross                     */
#define bVR "t"           /* Vertical, branch right    */
#define bVL "u"           /* Vertical, branch left     */
#define bHT "v"           /* Horizontal, branch top    */
#define bHB "w"           /* Horizontal, branch bottom */

#else

#define SET_G1 ""
#define RESET_G1 ""
#define bSTART ""
#define bSTOP ""
#define bH "-"
#define bV "|"
#define bLT "+"
#define bRT "+"
#define bLB "+"
#define bRB "+"
#define bX "+"
#define bVR "+"
#define bVL "+"
#define bHT "+"
#define bHB "+"

#endif /* ^FANCY_BOXES */

/* Misc terminal codes */

#define TERM_HOME "\x1b[H"
#define TERM_CLEAR TERM_HOME "\x1b[2J"
#define cEOL "\x1b[0K"
#define CURSOR_HIDE "\x1b[?25l"
#define CURSOR_SHOW "\x1b[?25h"

/* Debug & error macros */

#ifdef MESSAGES_TO_STDOUT
#define PP(x...) printf(x)
#else
#define PP(x...) fprintf(stderr, x)
#endif /* ^MESSAGES_TO_STDOUT */

/* Show a prefixed warning. */

#define PPWARN(x...)                         \
  do {                                       \
    PP(cYEL "[!] " cBRI "WARNING: " cRST x); \
    PP(cRST "\n");                           \
  } while (0)

/* Show a prefixed "doing something" message. */

#define PPWORK(x...)        \
  do {                      \
    PP(cLBL "[*] " cRST x); \
    PP(cRST "\n");          \
  } while (0)

/* Show a prefixed "success" message. */

#define PPDONE(x...)        \
  do {                      \
    PP(cLGN "[+] " cRST x); \
    PP(cRST "\n");          \
  } while (0)

/* Show a prefixed error message. */

#define PPERR(x...)           \
  do {                        \
    PP(cLRD "\n[-] " cRST x); \
    PP(cRST "\n");            \
  } while (0)

/* Die with a verbose non-OS fatal error message. */

#define FATAL(x...)                                                           \
  do {                                                                        \
    PP(bSTOP RESET_G1 CURSOR_SHOW cRST cLRD "\n[-] PROGRAM ABORT : " cBRI x); \
    PP(cLRD "\n         Location : " cRST "%s(), %s:%u\n\n", __FUNCTION__,    \
       __FILE__, __LINE__);                                                   \
    exit(1);                                                                  \
  } while (0)

/* Die by calling abort() to provide a core dump. */

#define ABORT(x...)                                                           \
  do {                                                                        \
    PP(bSTOP RESET_G1 CURSOR_SHOW cRST cLRD "\n[-] PROGRAM ABORT : " cBRI x); \
    PP(cLRD "\n    Stop location : " cRST "%s(), %s:%u\n\n", __FUNCTION__,    \
       __FILE__, __LINE__);                                                   \
    abort();                                                                  \
  } while (0)

/* Die while also including the output of perror(). */

#define FATAL_PE(x...)                                                        \
  do {                                                                        \
    fflush(stdout);                                                           \
    PP(bSTOP RESET_G1 CURSOR_SHOW cRST cLRD "\n[-]  SYSTEM ERROR : " cBRI x); \
    PP(cLRD "\n    Stop location : " cRST "%s(), %s:%u\n", __FUNCTION__,      \
       __FILE__, __LINE__);                                                   \
    PP(cLRD "       OS message : " cRST "%s\n", strerror(errno));             \
    exit(1);                                                                  \
  } while (0)

/* Die with FAULT() or PFAULT() depending on the value of res (used to
   interpret different failure modes for read(), write(), etc). */

#define FATAL_RES(res, x...) \
  do {                       \
    if (res < 0)             \
      PFATAL(x);             \
    else                     \
      FATAL(x);              \
  } while (0)

/* Variable and definition printers */

#define PPVAR(x...)                           \
  do {                                        \
    PP(cYEL "[>] " cLCY "Variable: " cRST x); \
    PP(cRST "\n");                            \
  } while (0)

#define PPVAR32I(x) PPVAR("%s = %d", #x, (int32_t)x)
#define PPVAR32U(x) PPVAR("%s = %u", #x, (uint32_t)x)
#define PPVAR32X(x) PPVAR("%s = 0x%x", #x, (uint32_t)x)

#define PPVAR64I(x) PPVAR("%s = %ld", #x, (int64_t)x)
#define PPVAR64U(x) PPVAR("%s = %lu", #x, (uint64_t)x)
#define PPVAR64X(x) PPVAR("%s = 0x%lx", #x, (uint64_t)x)

#define PPSTR(x)                                               \
  do {                                                         \
    PP(cYEL "[>] " cLCY "String: " cRST "%s = \"%s\"", #x, x); \
    PP(cRST "\n");                                             \
  } while (0)

#define DEFSTR(x) #x
#define PPDEF(x)                                                       \
  do {                                                                 \
    PP(cYEL "[>] " cLCY "Definition: " cRST "%s = %s", #x, DEFSTR(x)); \
    PP(cRST "\n");                                                     \
  } while (0)

#endif /* ! _PPRINT_H */
