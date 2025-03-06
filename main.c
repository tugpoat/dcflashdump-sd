#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <arch/rtc.h>

#include <ext2/fs_ext2.h>

#include <kos/blockdev.h>
#include <kos/dbgio.h>

#include <dc/sd.h>
#include <dc/flashrom.h>

#define READ_BLOCK_SIZE 2048 // TODO: optimize this if needed

#define ERRMSG_SD_INIT "Could not initialize the SD card. Please make sure that you have an SD card adapter plugged in and an SD card inserted.\n"
#define ERRMSG_SD_NO_PART "Could not find the first partition on the SD card\n"
#define ERRMSG_SD_INVALID_FS "MBR indicates a non-ext2 filesystem."
#define ERRMSG_SD_EXT2_INIT "Could not initialize fs_ext2\n"
#define ERRMSG_SD_MOUNT "Could not mount SD card as ext2fs. Please make sure the card has been properly formatted.\n"

void exit_fatal(char *message) {
    printf(message);
    dbgio_printf(message);
    exit(EXIT_FAILURE);
}

void init_sd_access(kos_blockdev_t *sd_dev, uint8 partition_type) {
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

    if(fs_ext2_mount("/sd", sd_dev, FS_EXT2_MOUNT_READWRITE))
        exit_fatal(ERRMSG_SD_MOUNT);
}

int main(int argc, char **argv) {
    time_t cur_dt = rtc_unix_secs();

    kos_blockdev_t sd_dev;
    uint8 partition_type = 0;
    //struct dirent *entry;
    FILE *fp;

    dbgio_init();
    dbgio_enable();

    // Init SD subsystem and ext2 access
    init_sd_access(&sd_dev, partition_type);

    // Open file for writing
    char filepath[64];
    sprintf(filepath,"/sd/flash_%lld", cur_dt);

    if (!(fp = fopen(filepath, "w"))) {
        printf("Could not create file: %s\n", strerror(errno));
        dbgio_printf("Could not create file: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    size_t read_offset = 0;
    size_t bytes_read = 0;
    char buf[READ_BLOCK_SIZE];

    while ((bytes_read = flashrom_read(read_offset, buf, READ_BLOCK_SIZE)) > -1) {
        read_offset += bytes_read;
        if (fwrite(buf, sizeof(buf), 1, fp) < 0) {
            printf("Could not write to file: %s\n", strerror(errno));
            dbgio_printf("Could not write to file: %s\n", strerror(errno));
            break;
        }
    }

    fclose(fp);

    printf("Dumped flash to %s", filepath);
    dbgio_printf("Dumped flash to %s", filepath);

    fs_ext2_unmount("/sd");
    fs_ext2_shutdown();
    sd_shutdown();
    
    return 0;
}