#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <arch/rtc.h>

#include <ext2/fs_ext2.h>

#include <kos/blockdev.h>
#include <kos/dbgio.h>

#include <dc/flashrom.h>

// --- Default to writing to SD
#ifdef SD_WRITE
    #undef DCLOAD_WRITE
#endif
#ifndef DCLOAD_WRITE
    #include <dc/sd.h>
    #define SD_WRITE
    #define MOUNT_POINT "/sd"
#else
    #define MOUNT_POINT "/pc"
#endif
// ---

/*
        https://dreamcast.wiki/Memory_map
        Use privileged, uncached memory access to copy 0x00200000 to 0x0021FFFF
        0xA is the flag.
*/
#define FLASH_ADDR_START 0xA0200000
#define FLASH_ADDR_END 0xA021FFFF

#define READ_BLOCK_SIZE 2048 // TODO: optimize this if needed

#define MAX_PATH 64
#define ERRMSG_SD_INIT "Could not initialize the SD card. Please make sure that you have an SD card adapter plugged in and an SD card inserted.\n"
#define ERRMSG_SD_NO_PART "Could not find the first partition on the SD card\n"
#define ERRMSG_SD_INVALID_FS "MBR indicates a non-ext2 filesystem."
#define ERRMSG_SD_EXT2_INIT "Could not initialize fs_ext2\n"
#define ERRMSG_SD_MOUNT "Could not mount SD card as ext2fs. Please make sure the card has been properly formatted.\n"


void exit_fatal(char *message)
{
    dbgio_printf(message);
    exit(EXIT_FAILURE);
}

#ifdef SD_WRITE
void init_sd_access(kos_blockdev_t *sd_dev, uint8 partition_type)
{
    // Init SD Subsystem
    if(sd_init())
        exit_fatal(ERRMSG_SD_INIT);

    /* Grab the block device for the first partition on the SD card. Note that
       you must have the SD card formatted with an MBR partitioning scheme. */
    if(sd_blockdev_for_partition(0, sd_dev, &partition_type))
        exit_fatal(ERRMSG_SD_NO_PART);

    // Check to see if the MBR says that we have a Linux partition.
    if(partition_type != 0x83)
        exit_fatal(ERRMSG_SD_INVALID_FS);

    // Initialize fs_ext2 and attempt to mount the device.
    if(fs_ext2_init())
        exit_fatal(ERRMSG_SD_EXT2_INIT);

    if(fs_ext2_mount(MOUNT_POINT, sd_dev, FS_EXT2_MOUNT_READWRITE))
        exit_fatal(ERRMSG_SD_MOUNT);
}

void shutdown_sd_access() {
    fs_ext2_unmount(MOUNT_POINT);
    fs_ext2_shutdown();
    sd_shutdown();
}

#endif

int main(int argc, char **argv) 
{
    time_t cur_dt = rtc_unix_secs();

#ifdef SD_WRITE
    kos_blockdev_t sd_dev;
    uint8 partition_type = 0;
#endif

    size_t read_offset = FLASH_ADDR_START;
    size_t write_offset = 0;

    uint8 buf[READ_BLOCK_SIZE];
    char filepath[MAX_PATH];
    FILE *fp;

    dbgio_init();
    dbgio_enable();

#ifdef SD_WRITE
    // Init SD subsystem and ext2 access
    init_sd_access(&sd_dev, partition_type);
#endif

    // Open file for writing
    snprintf(filepath,MAX_PATH,"%s/flash_%lld", MOUNT_POINT, cur_dt);
    dbgio_printf("Opening outfile: %s\n", filepath);

    if ((fp = fopen(filepath, "wb+")) == NULL) {
        // We couldn't open the file
#ifdef SD_WRITE
        shutdown_sd_access();
#endif
        dbgio_printf("ERROR: Could not open file for writing: %s\n", strerror(errno));
        exit_fatal("bye :(");
    }

    /*
        Read from flash in chunks of READ_BLOCK_SIZE,
        then immediately write each chunk to the destination file
    */
    while (memcpy(&buf, (void *)read_offset, READ_BLOCK_SIZE) != NULL && (read_offset <= FLASH_ADDR_END))
    {
        if (fwrite(buf, sizeof(buf), 1, fp) < 0) {
            dbgio_printf("ERROR: Could not write to file: %s\n", strerror(errno));
            break;
        } else {
            write_offset += READ_BLOCK_SIZE;
        }
        dbgio_printf("0x%0X - 0x%0X dumped, bytes written:%d\n", read_offset, (read_offset + READ_BLOCK_SIZE), write_offset);
        read_offset += READ_BLOCK_SIZE;
    }

    /*
        Clean up and exit
    */
    fclose(fp);

    dbgio_printf("Dumped %d bytes to %s", write_offset, filepath);

#ifdef SD_WRITE
    shutdown_sd_access();
#endif

    return 0;
}