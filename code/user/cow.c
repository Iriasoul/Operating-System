#include <error.h>
#include <stdio.h>
#include <ulib.h>

static volatile int value __attribute__((aligned(4096))) = 17;
static volatile int child_code __attribute__((aligned(4096))) = -1;
static const int readonly_code = -2;

int main(void)
{
    int child = fork();
    if (child == 0)
    {
        assert(value == 17);
        value = 23;
        assert(value == 23);
        exit(42);
    }
    assert(child > 0);
    value = 19;

    int observer = fork();
    if (observer == 0)
    {
        for (int i = 0; i < 8; i++)
        {
            yield();
        }
        assert(value == 19 && child_code == -1);
        return 0;
    }
    assert(observer > 0);

    assert(waitpid(child, (int *)&readonly_code) == -E_INVAL);
    assert(waitpid(child, (int *)&child_code) == 0);
    assert(child_code == 42 && value == 19);
    int observer_code;
    assert(waitpid(observer, &observer_code) == 0 && observer_code == 0);
    cprintf("cow pass.\n");
    return 0;
}
