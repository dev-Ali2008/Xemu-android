#include "qemu/osdep.h"
#include "ui/input.h"
#include "qapi/qapi-types-ui.h"

int qemu_input_key_number_to_qcode(unsigned int nr)
{
    (void)nr;
    return Q_KEY_CODE_UNMAPPED;
}

const guint qemu_input_map_usb_to_qcode_len = 0;
const guint16 qemu_input_map_usb_to_qcode[] = { Q_KEY_CODE_UNMAPPED };
