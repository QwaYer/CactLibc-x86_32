#include "ctype.h"

int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int iscntrl(int c) { return (c >= 0 && c < 0x20) || c == 0x7F; }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isgraph(int c) { return c > 0x20 && c < 0x7F; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isprint(int c) { return c >= 0x20 && c < 0x7F; }
int ispunct(int c) { return isgraph(c) && !isalnum(c); }
int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c; }
int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c; }
