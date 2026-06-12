#include <assert.h>
#include <stdio.h>

#include "ui_diagnostics.h"

int main(void)
{
    assert(ui_diagnostics_enabled());
    puts("ui_diagnostics enabled tests passed");
    return 0;
}
