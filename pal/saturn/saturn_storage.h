/**
 * saturn_storage.h - Saturn Backup RAM Storage Interface
 *
 * Provides save/load functionality using the Saturn's built-in 32KB
 * Backup RAM and optional external memory cartridges via the BUP
 * BIOS API.
 *
 * The core cui_pal_storage_t interface (save/load/exists/delete/free_space)
 * uses the currently selected device. Saturn-specific functions allow
 * device selection and querying.
 */

#ifndef SATURN_STORAGE_H
#define SATURN_STORAGE_H

#include "../../core/include/cui_pal.h"

#ifdef __SATURN__
#include "sgl_defs.h"
#else
#include <stdint.h>
typedef uint8_t  Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef int32_t  Sint32;
#endif

/*============================================================================
 * BUP BIOS Types (matching SEGA_BUP.H struct layouts)
 *============================================================================*/

/** BUP device configuration (filled by BUP_Init) */
typedef struct {
    Uint16 unit_id;
    Uint16 partition;
} saturn_bup_config_t;

/** BUP device status */
typedef struct {
    Uint32 totalsize;   /* Total size in blocks */
    Uint32 totalblock;  /* Total number of blocks */
    Uint32 blocksize;   /* Bytes per block */
    Uint32 freesize;    /* Free size in bytes */
    Uint32 freeblock;   /* Free blocks */
    Uint32 datanum;     /* Number of files stored */
} saturn_bup_stat_t;

/** BUP date (packed format) */
typedef struct {
    Uint8 year;     /* Offset from 1980 */
    Uint8 month;
    Uint8 day;
    Uint8 time;     /* Hour */
    Uint8 min;
    Uint8 week;     /* Day of week */
} saturn_bup_date_t;

/** BUP directory entry */
typedef struct {
    char   filename[12];  /* 11 chars + null */
    char   comment[11];   /* 10 chars + null */
    Uint8  language;
    Uint32 date;          /* Packed date */
    Uint32 datasize;      /* Data size in bytes */
    Uint16 blocksize;     /* Size in blocks */
} saturn_bup_dir_t;

/*============================================================================
 * BUP Error Codes
 *============================================================================*/

typedef enum {
    SATURN_BUP_OK             = 0,
    SATURN_BUP_NOT_CONNECTED  = 1,
    SATURN_BUP_UNFORMATTED    = 2,
    SATURN_BUP_WRITE_PROTECTED = 3,
    SATURN_BUP_NO_SPACE       = 4,
    SATURN_BUP_NOT_FOUND      = 5,
    SATURN_BUP_ALREADY_EXISTS = 6,
    SATURN_BUP_VERIFY_FAILED  = 7,
    SATURN_BUP_BROKEN         = 8
} saturn_bup_error_t;

/*============================================================================
 * BUP BIOS Function Pointers (Saturn hardware only)
 *============================================================================*/

#ifdef __SATURN__

#define BUP_LIB_ADDRESS    (*(volatile Uint32 *)(0x6000350 + 8))
#define BUP_VECTOR_ADDRESS (*(volatile Uint32 *)(0x6000350 + 4))

/* BUP_Init: Initialize backup library
 * void BUP_Init(Uint32 *lib, Uint32 *work, saturn_bup_config_t configs[3]) */
#define BUP_Init(lib, work, configs) \
    ((void (*)(Uint32*, Uint32*, saturn_bup_config_t*))(BUP_LIB_ADDRESS))(lib, work, configs)

/* BUP_SelPart: Select partition
 * Sint32 BUP_SelPart(Uint32 device, Uint16 partition) */
#define BUP_SelPart(dev, part) \
    ((Sint32 (*)(Uint32, Uint16))(*(volatile Uint32 *)(BUP_VECTOR_ADDRESS + 4)))(dev, part)

/* BUP_Format: Format device
 * Sint32 BUP_Format(Uint32 device) */
#define BUP_Format(dev) \
    ((Sint32 (*)(Uint32))(*(volatile Uint32 *)(BUP_VECTOR_ADDRESS + 8)))(dev)

/* BUP_Stat: Get device status
 * Sint32 BUP_Stat(Uint32 device, Uint32 datasize, saturn_bup_stat_t *stat) */
#define BUP_Stat(dev, sz, stat) \
    ((Sint32 (*)(Uint32, Uint32, saturn_bup_stat_t*))(*(volatile Uint32 *)(BUP_VECTOR_ADDRESS + 12)))(dev, sz, stat)

/* BUP_Write: Write file
 * Sint32 BUP_Write(Uint32 device, saturn_bup_dir_t *dir, void *data, Uint8 mode) */
#define BUP_Write(dev, dir, data, mode) \
    ((Sint32 (*)(Uint32, saturn_bup_dir_t*, void*, Uint8))(*(volatile Uint32 *)(BUP_VECTOR_ADDRESS + 16)))(dev, dir, data, mode)

/* BUP_Read: Read file
 * Sint32 BUP_Read(Uint32 device, char *filename, void *buffer) */
#define BUP_Read(dev, name, buf) \
    ((Sint32 (*)(Uint32, const char*, void*))(*(volatile Uint32 *)(BUP_VECTOR_ADDRESS + 20)))(dev, name, buf)

/* BUP_Delete: Delete file
 * Sint32 BUP_Delete(Uint32 device, char *filename) */
#define BUP_Delete(dev, name) \
    ((Sint32 (*)(Uint32, const char*))(*(volatile Uint32 *)(BUP_VECTOR_ADDRESS + 24)))(dev, name)

/* BUP_Dir: List directory / get file info
 * Sint32 BUP_Dir(Uint32 device, char *filename, Uint16 max, saturn_bup_dir_t *dir) */
#define BUP_Dir(dev, name, max, dir) \
    ((Sint32 (*)(Uint32, const char*, Uint16, saturn_bup_dir_t*))(*(volatile Uint32 *)(BUP_VECTOR_ADDRESS + 28)))(dev, name, max, dir)

/* BUP_Verify: Verify written data
 * Sint32 BUP_Verify(Uint32 device, char *filename, void *data) */
#define BUP_Verify(dev, name, data) \
    ((Sint32 (*)(Uint32, const char*, void*))(*(volatile Uint32 *)(BUP_VECTOR_ADDRESS + 32)))(dev, name, data)

/* BUP_GetDate: Expand packed date to struct
 * void BUP_GetDate(Uint32 date, saturn_bup_date_t *out) */
#define BUP_GetDate(date, out) \
    ((void (*)(Uint32, saturn_bup_date_t*))(*(volatile Uint32 *)(BUP_VECTOR_ADDRESS + 36)))(date, out)

/* BUP_SetDate: Compress struct to packed date
 * Uint32 BUP_SetDate(saturn_bup_date_t *in) */
#define BUP_SetDate(in) \
    ((Uint32 (*)(saturn_bup_date_t*))(*(volatile Uint32 *)(BUP_VECTOR_ADDRESS + 40)))(in)

#endif /* __SATURN__ */

/*============================================================================
 * Saturn Storage Filename Limits
 *============================================================================*/

#define SATURN_BUP_FILENAME_MAX  11  /* Max filename length (excluding null) */
#define SATURN_BUP_COMMENT_MAX   10  /* Max comment length (excluding null) */

/*============================================================================
 * Core Storage Interface
 *============================================================================*/

/**
 * Get the Saturn storage interface for registration with cui_platform_t.
 * @return Pointer to static storage vtable
 */
cui_pal_storage_t* cui_saturn_storage(void);

/*============================================================================
 * Saturn-Specific API
 *============================================================================*/

/**
 * Select the active storage device.
 * @param device 0 = internal backup RAM (default), 1 = cartridge, 2 = serial
 */
void saturn_storage_set_device(Uint32 device);

/**
 * Get the currently active storage device.
 * @return Current device index
 */
Uint32 saturn_storage_get_device(void);

/**
 * Get device status information.
 * @param device Device index
 * @param out Output status structure
 * @return true on success
 */
bool saturn_storage_get_device_info(Uint32 device, saturn_bup_stat_t* out);

/**
 * Check if a device is connected and accessible.
 * @param device Device index
 * @return true if connected
 */
bool saturn_storage_is_device_connected(Uint32 device);

/**
 * Get file information for a specific file.
 * @param filename File to query
 * @param out Output directory entry
 * @return true if file found
 */
bool saturn_storage_get_file_info(const char* filename, saturn_bup_dir_t* out);

/**
 * Callback for file listing.
 * @param entry Directory entry for this file
 * @param ud User data pointer
 * @return true to continue listing, false to stop
 */
typedef bool (*saturn_storage_file_cb_t)(const saturn_bup_dir_t* entry, void* ud);

/**
 * List all files on the active device.
 * @param cb Callback invoked for each file
 * @param ud User data passed to callback
 * @return Number of files listed, or -1 on error
 */
int saturn_storage_list_files(saturn_storage_file_cb_t cb, void* ud);

/**
 * Get the last BUP error code.
 * @return Last error from a BUP operation
 */
saturn_bup_error_t saturn_storage_get_last_error(void);

/**
 * Get a human-readable string for a BUP error code.
 * @param err Error code
 * @return Static string describing the error
 */
const char* saturn_storage_error_string(saturn_bup_error_t err);

/*============================================================================
 * Filename Utilities
 *============================================================================*/

/**
 * Pad/truncate a filename to fit the BUP 11-character format.
 * Copies up to 11 characters, pads with spaces if shorter.
 * @param dst Destination buffer (must be at least 12 bytes)
 * @param src Source filename string
 */
void saturn_storage_pad_filename(char* dst, const char* src);

#endif /* SATURN_STORAGE_H */
