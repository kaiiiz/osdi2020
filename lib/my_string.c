#include "my_math.h"
#include "typedef.h"

char *itox64(uint64_t value, char *s) {
    if (value == 0) return "0";

    int idx = 0;

    char tmp[64 + 1];
    int tidx = 0;
    while (value) {
        int r = value % 16;
        if (r < 10) {
            tmp[tidx++] = '0' + r;
        }
        else {
            tmp[tidx++] = 'a' + r - 10;
        }
        value /= 16;
    }

    // reverse tmp
    int i;
    for (i = tidx - 1; i >= 0; i--) {
        s[idx++] = tmp[i];
    }
    s[idx] = '\0';

    return s;
}

char *itoa(int value, char *s) {
    int idx = 0;
    if (value < 0) {
        value *= -1;
        s[idx++] = '-';
    }

    char tmp[10];
    int tidx = 0;
    do {
        tmp[tidx++] = '0' + value % 10;
        value /= 10;
    } while (value != 0 && tidx < 11);

    // reverse tmp
    int i;
    for (i = tidx - 1; i >= 0; i--) {
        s[idx++] = tmp[i];
    }
    s[idx] = '\0';

    return s;
}

char *ftoa(float value, char *s) {
    int idx = 0;
    if (value < 0) {
        value = -value;
        s[idx++] = '-';
    }

    int ipart = (int)value;
    float fpart = value - (float)ipart;

    // convert ipart
    char istr[11];  // 10 digit
    itoa(ipart, istr);

    // convert fpart
    char fstr[7];  // 6 digit
    fpart *= (int)pow(10, 6);
    itoa((int)fpart, fstr);

    // copy int part
    char *ptr = istr;
    while (*ptr) s[idx++] = *ptr++;
    s[idx++] = '.';
    // copy float part
    ptr = fstr;
    while (*ptr) s[idx++] = *ptr++;
    s[idx] = '\0';

    return s;
}

unsigned int my_vsprintf(char *dst, char *fmt, __builtin_va_list args) {
    char *dst_orig = dst;

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            // escape %
            if (*fmt == '%') {
                goto put;
            }
            // string
            if (*fmt == 's') {
                char *p = __builtin_va_arg(args, char *);
                while (*p) {
                    *dst++ = *p++;
                }
            }
            // char
            if (*fmt == 'c') {
                char c = __builtin_va_arg(args, int);
                *dst++ = c;
            }
            // number
            if (*fmt == 'd') {
                int arg = __builtin_va_arg(args, int);
                char buf[11];
                char *p = itoa(arg, buf);
                while (*p) {
                    *dst++ = *p++;
                }
            }
            // hex
            if (*fmt == 'x') {
                uint64_t arg = __builtin_va_arg(args, uint64_t);
                char buf[64 + 1];
                char *p = itox64(arg, buf);
                while (*p) {
                    *dst++ = *p++;
                }
            }
            // float
            if (*fmt == 'f') {
                float arg = (float)__builtin_va_arg(args, double);
                char buf[19];  // sign + 10 int + dot + 6 float
                char *p = ftoa(arg, buf);
                while (*p) {
                    *dst++ = *p++;
                }
            }
        }
        else {
        put:
            *dst++ = *fmt;
        }
        fmt++;
    }
    *dst = '\0';

    return dst - dst_orig;  // return written bytes
}

unsigned int my_sprintf(char *dst, char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    return my_vsprintf(dst, fmt, args);
}

int strcmp(const char *X, const char *Y) {
    while (*X) {
        if (*X != *Y)
            break;
        X++;
        Y++;
    }
    return *(const unsigned char *)X - *(const unsigned char *)Y;
}

int strcpy(char *dest, const char *src) {
    int i = 0;
    while (src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
    return i;
}

int strlen(const char *str) {
    const char *s;

    for (s = str; *s; ++s)
        ;
    return (s - str);
}
