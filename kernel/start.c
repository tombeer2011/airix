#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/pic.h>
#include <kernel/pit.h>
#include <kernel/pci.h>
#include <kernel/ide.h>
#include <kernel/exception.h>
#include <kernel/keyboard.h>
#include <kernel/console.h>
#include <kernel/klib.h>
#include <mm/pmm.h>
#include <mm/paging.h>
#include <lib/string.h>

struct boot_info
{
    physical_addr_t free_addr;
    uint32_t num_mmap_entries;
    struct mmap_entry mmap_entries[1];
};

static struct boot_info *boot_info;

static void test_isr_timer()
{
    static char c = 0;
    put_char_at(18, 0, c++);
}

static void test_console_char_consumer(console_char_t c, void *data)
{
    (void)data;
    if (c >= SHIFT('a') && c <= SHIFT('z'))
        put_char_at(17, 0, (c & 0xFF) - 32);
    else if (c >= SHIFT('A') && c <= SHIFT('Z'))
        put_char_at(17, 0, (c & 0xFF) + 32);
    else
        put_char_at(17, 0, c & 0xFF);
}

static void test_install_keyboard()
{
    static struct console console;
    struct key_code_handler handler;

    console.char_consumer = test_console_char_consumer;
    console.data = NULL;

    handler.handler = console_key_code_handler;
    handler.data = &console;
    kbd_set_key_code_handler(&handler);
}

static unsigned char sector_data[512];
static bool writed = false;

static void test_ide_drive_write();

static void test_read_complete(struct ide_dma_io *io_data, bool error)
{
    char *data = CAST_PHYSICAL_TO_VIRTUAL(io_data->buffer);

    if (error)
    {
        printk("Read data from ide to address %p error.\n", data);
        return ;
    }

    printk("Read complete address: %p, size: %u\n", data, io_data->size);
    for (size_t i = 0; i < 16; ++i)
        printk("%x ", data[i]++);
    printk("\n");

    if (!writed)
        test_ide_drive_write();
    writed = true;
}

static void test_ide_drive_read()
{
    struct ide_dma_io io_data;

    io_data.drive = 0;
    io_data.start = 0;
    io_data.sector_count = 1;
    io_data.buffer = CAST_VIRTUAL_TO_PHYSICAL(sector_data);
    io_data.size = sizeof(sector_data);
    io_data.data = NULL;
    io_data.complete_func = test_read_complete;

    ide_dma_read_sectors(&io_data);
}

static void test_write_complete(struct ide_dma_io *io_data, bool error)
{
    char *data = CAST_PHYSICAL_TO_VIRTUAL(io_data->buffer);

    if (error)
    {
        printk("Write data to ide from address %p error.\n", data);
        return ;
    }

    memset(data, 0, io_data->size);
    test_ide_drive_read();
}

static void test_ide_drive_write()
{
    struct ide_dma_io io_data;

    io_data.drive = 0;
    io_data.start = 0;
    io_data.sector_count = 1;
    io_data.buffer = CAST_VIRTUAL_TO_PHYSICAL(sector_data);
    io_data.size = sizeof(sector_data);
    io_data.data = NULL;
    io_data.complete_func = test_write_complete;

    ide_dma_write_sectors(&io_data);
}

void init_paging(physical_addr_t bi)
{
    struct boot_info *binfo = (void *)bi;
    physical_addr_t free = ALIGN_PAGE(binfo->free_addr);

    /* Init paging for kernel */
    free = VIRTUAL_TO_PHYSICAL(pg_init_paging)(free);
    /* Now all addresses are virtual address */

    /* Cast to virtual address */
    boot_info = CAST_PHYSICAL_TO_VIRTUAL(bi);
    boot_info->free_addr = free;
}

void kernel_main();

/* Kernel entry */
void kernel_entry()
{
    physical_addr_t free = boot_info->free_addr;

    /* Complete setup paging for kernel */
    free = pg_complete_paging(free, boot_info->mmap_entries,
                              boot_info->num_mmap_entries);

    clear_screen();
    printk("[%-8s] initialize kernel ...\n", "Entry");

    pmm_initialize(free, boot_info->mmap_entries, boot_info->num_mmap_entries);

    gdt_initialize();
    idt_initialize();
    pic_initialize();
    pit_initialize(50);
    kbd_initialize();
    exception_handle_initialize();
    pci_detecting_devices();

    pmm_print_statistics(boot_info->mmap_entries, boot_info->num_mmap_entries);
    printk("[%-8s] success!\n\n", "Entry");

    pic_register_isr(IRQ0, test_isr_timer);
    test_install_keyboard();
    test_ide_drive_read();

    start_int();
    kernel_main();
}
