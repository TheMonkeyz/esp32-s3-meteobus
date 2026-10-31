// Who is downloading now (see netq.h)
#include "netq.h"

static unsigned busy;                    // NETQ_* bits

void netq_set(netq_who_t who, bool on)
{
    if (on) __atomic_fetch_or(&busy, (unsigned)who, __ATOMIC_ACQ_REL);
    else __atomic_fetch_and(&busy, ~(unsigned)who, __ATOMIC_ACQ_REL);
}

bool netq_others_busy(netq_who_t self)
{
    return (__atomic_load_n(&busy, __ATOMIC_ACQUIRE) & ~(unsigned)self) != 0;
}
