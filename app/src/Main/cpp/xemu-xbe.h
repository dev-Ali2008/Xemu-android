
#ifndef XEMU_XBE_H
#define XEMU_XBE_H

#include <stdint.h>


#pragma pack(1)
struct xbe_header
{
    uint32_t m_magic;                         
    uint8_t  m_digsig[256];                   
    uint32_t m_base;                          
    uint32_t m_sizeof_headers;                
    uint32_t m_sizeof_image;                  
    uint32_t m_sizeof_image_header;           
    uint32_t m_timedate;                      
    uint32_t m_certificate_addr;              
    uint32_t m_sections;                      
    uint32_t m_section_headers_addr;          

    struct init_flags
    {
        uint32_t m_mount_utility_drive    : 1;  
        uint32_t m_format_utility_drive   : 1;  
        uint32_t m_limit_64mb             : 1;  
        uint32_t m_dont_setup_harddisk    : 1;  
        uint32_t m_unused                 : 4;  
        uint32_t m_unused_b1              : 8;  
        uint32_t m_unused_b2              : 8;  
        uint32_t m_unused_b3              : 8;  
    } m_init_flags;

    uint32_t m_entry;                         
    uint32_t m_tls_addr;                      
    uint32_t m_pe_stack_commit;               
    uint32_t m_pe_heap_reserve;               
    uint32_t m_pe_heap_commit;                
    uint32_t m_pe_base_addr;                  
    uint32_t m_pe_sizeof_image;               
    uint32_t m_pe_checksum;                   
    uint32_t m_pe_timedate;                   
    uint32_t m_debug_pathname_addr;           
    uint32_t m_debug_filename_addr;           
    uint32_t m_debug_unicode_filename_addr;   
    uint32_t m_kernel_image_thunk_addr;       
    uint32_t m_nonkernel_import_dir_addr;     
    uint32_t m_library_versions;              
    uint32_t m_library_versions_addr;         
    uint32_t m_kernel_library_version_addr;   
    uint32_t m_xapi_library_version_addr;     
    uint32_t m_logo_bitmap_addr;              
    uint32_t m_logo_bitmap_size;              
};

struct xbe_certificate
{
    uint32_t m_size;                          
    uint32_t m_timedate;                      
    uint32_t m_titleid;                       
    uint16_t m_title_name[40];                
    uint32_t m_alt_title_id[0x10];            
    uint32_t m_allowed_media;                 
    uint32_t m_game_region;                   
    uint32_t m_game_ratings;                  
    uint32_t m_disk_number;                   
    uint32_t m_version;                       
    uint8_t  m_lan_key[16];                   
    uint8_t  m_sig_key[16];                   
    uint8_t  m_title_alt_sig_key[16][16];     
};
#pragma pack()

struct xbe {

	uint8_t *headers;
	uint32_t headers_len;


	struct xbe_header *header;
	struct xbe_certificate *cert;
};

#ifdef __cplusplus
extern "C" {
#endif


struct xbe *xemu_get_xbe_info(void);

#ifdef __cplusplus
}
#endif

#endif
