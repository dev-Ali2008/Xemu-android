#include "xemu-xbe.h"
#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci.h"
#include "system/hw_accel.h"
#include "cpu.h"
#include "exec/target_page.h"

static int virt_to_phys(vaddr vaddr, hwaddr *phys_addr)
{
    MemTxAttrs attrs;
    CPUState *cs;
    hwaddr gpa;

    cs = qemu_get_cpu(0);
    if (!cs) {
        return 1; 
    }

    cpu_synchronize_state(cs);

    gpa = cpu_get_phys_page_attrs_debug(cs, vaddr & TARGET_PAGE_MASK, &attrs);
    if (gpa == -1) {
        return 1; 
    } else {
        *phys_addr = gpa + (vaddr & ~TARGET_PAGE_MASK);
    }

    return 0;
}

static ssize_t virt_dma_memory_read(vaddr vaddr, void *buf, size_t len)
{
    size_t num_bytes_read = 0;

    while (num_bytes_read < len) {

        hwaddr phys_addr = 0;
        if (virt_to_phys(vaddr + num_bytes_read, &phys_addr) != 0) {
            return -1;
        }


        size_t bytes_remaining_in_page = TARGET_PAGE_SIZE - (phys_addr & ~TARGET_PAGE_MASK);
        size_t num_bytes_to_read = MIN(len - num_bytes_read, bytes_remaining_in_page);


        dma_memory_read(&address_space_memory,
                        phys_addr,
                        buf + num_bytes_read,
                        num_bytes_to_read,
                        MEMTXATTRS_UNSPECIFIED);

        num_bytes_read += num_bytes_to_read;
    }

    return num_bytes_read;
}

struct xbe *xemu_get_xbe_info(void)
{
    vaddr hdr_addr_virt = 0x10000;

    static struct xbe xbe = {0};

    if (xbe.headers) {
        free(xbe.headers);
        xbe.headers = NULL;
    }


    hwaddr hdr_addr_phys = 0;
    if (virt_to_phys(hdr_addr_virt, &hdr_addr_phys) != 0) {
        return NULL;
    }


    uint32_t sig = ldl_le_phys(&address_space_memory, hdr_addr_phys);
    if (sig != 0x48454258) {
        return NULL;
    }


    xbe.headers_len = ldl_le_phys(&address_space_memory,
        hdr_addr_phys + offsetof(struct xbe_header, m_sizeof_headers));
    if (xbe.headers_len > 8*TARGET_PAGE_SIZE) {

        return NULL;
    }

    xbe.headers = malloc(xbe.headers_len);
    assert(xbe.headers != NULL);


    ssize_t bytes_read = virt_dma_memory_read(hdr_addr_virt,
                                              xbe.headers,
                                              xbe.headers_len);
    if (bytes_read != xbe.headers_len) {

        return NULL;
    }


    xbe.header = (struct xbe_header *)xbe.headers;


    vaddr cert_addr_virt = ldl_le_p(&xbe.header->m_certificate_addr);
    if ((cert_addr_virt == 0) || ((cert_addr_virt + sizeof(struct xbe_certificate)) > (hdr_addr_virt + xbe.headers_len))) {

        return NULL;
    }
    xbe.cert = (struct xbe_certificate *)(xbe.headers + cert_addr_virt - hdr_addr_virt);

    return &xbe;
}
