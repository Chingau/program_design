#include "stdio.h"

typedef int (*init_fn_t)(void);
#define USED                    __attribute__((used))
#define SECTION(x)              __attribute__((section(x)))
#define INIT_EXPORT_HEAD(fn)  const init_fn_t _g_init_##fn SECTION("._g_init.head") USED = fn
#define INIT_EXPORT_TAIL(fn)  const init_fn_t _g_init_##fn SECTION("._g_init.tail") USED = fn
#define DECLARE_INIT(fn, level)  const init_fn_t _g_init_##fn SECTION("._g_init.node."#level) USED = fn

void _g_init_func(void);

int main(int argc, const char *argv[])
{
    _g_init_func();
    return 0;
}

static int start(void)
{
    printf("init %s\r\n", __func__);
    return 0;
}
INIT_EXPORT_HEAD(start);

static int end(void)
{
    printf("init %s\r\n", __func__);
    return 0;
}
INIT_EXPORT_TAIL(end);

static int fun_1(void)
{
    printf("init %s\r\n", __func__);
    return 0;
}
DECLARE_INIT(fun_1, 1.1);

static int fun_3(void)
{
    printf("init %s\r\n", __func__);
    return 0;
}
DECLARE_INIT(fun_3, 5.abc);

static int fun_2(void)
{
    printf("init %s\r\n", __func__);
    return 0;
}
DECLARE_INIT(fun_2, 2.exy);

void _g_init_func(void) 
{
    const init_fn_t *fun_ptr;

    for (fun_ptr = &_g_init_start; fun_ptr <= &_g_init_end; ++fun_ptr) {
        (*fun_ptr)();
    }
}