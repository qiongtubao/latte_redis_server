#include <stddef.h>
#include <stdint.h>
#include <string.h>


/* Define redis_fsync to fdatasync() in Linux and fsync() for all the rest */
#ifdef __linux__
#define redis_fsync fdatasync
#else
#define redis_fsync fsync
#endif

#ifdef __GNUC__
#  define likely(x)   __builtin_expect(!!(x), 1)
#  define unlikely(x) __builtin_expect(!!(x), 0)
#else
#  define likely(x)   !!(x)
#  define unlikely(x) !!(x)
#endif

int ll2string(char *dst, size_t dstlen, long long svalue);
int ull2string(char *dst, size_t dstlen, unsigned long long value);
int string2ll(const char *s, size_t slen, long long *value) ;
long long ustime(void);
long long mstime(void);
void getRandomBytes(unsigned char *p, size_t len);