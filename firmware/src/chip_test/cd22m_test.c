#include "common/cd22m.h"

void cd22m_test(void) {
    cd22m_init();
    cd22m_reset();

    // x0 -> y0, x1 -> y1, ..., x7 -> y7
    for (int i = 0; i < 8; i++) {
        cd22m_set_switch(i, i, true);
    }
}
