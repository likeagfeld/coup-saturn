/**
 * saturn_storage.c - Saturn Backup RAM Storage Implementation
 *
 * Implements the cui_pal_storage_t interface using the Saturn BUP BIOS API.
 * Supports the built-in 32KB backup RAM and optional external cartridges.
 *
 * Non-Saturn builds get stub implementations that return errors.
 */

#include "saturn_storage.h"
#include <string.h>

/*============================================================================
 * Filename Utilities (platform-independent)
 *============================================================================*/

void saturn_storage_pad_filename(char* dst, const char* src)
{
    if (!dst) return;
    if (!src) {
        memset(dst, ' ', SATURN_BUP_FILENAME_MAX);
        dst[SATURN_BUP_FILENAME_MAX] = '\0';
        return;
    }

    int i;
    for (i = 0; i < SATURN_BUP_FILENAME_MAX && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    for (; i < SATURN_BUP_FILENAME_MAX; i++) {
        dst[i] = ' ';
    }
    dst[SATURN_BUP_FILENAME_MAX] = '\0';
}

/*============================================================================
 * Error String Mapping (platform-independent)
 *============================================================================*/

const char* saturn_storage_error_string(saturn_bup_error_t err)
{
    switch (err) {
        case SATURN_BUP_OK:              return "OK";
        case SATURN_BUP_NOT_CONNECTED:   return "Device not connected";
        case SATURN_BUP_UNFORMATTED:     return "Device not formatted";
        case SATURN_BUP_WRITE_PROTECTED: return "Write protected";
        case SATURN_BUP_NO_SPACE:        return "No space";
        case SATURN_BUP_NOT_FOUND:       return "File not found";
        case SATURN_BUP_ALREADY_EXISTS:  return "File already exists";
        case SATURN_BUP_VERIFY_FAILED:   return "Verify failed";
        case SATURN_BUP_BROKEN:          return "Data broken";
        default:                         return "Unknown error";
    }
}

#ifdef __SATURN__

/*============================================================================
 * Saturn Implementation
 *============================================================================*/

static saturn_bup_config_t s_configs[3];
static Uint32              s_active_device = 0;
static saturn_bup_error_t  s_last_error = SATURN_BUP_OK;
static bool                s_initialized = false;
static Uint32 s_bup_lib[4096] __attribute__((aligned(4)));  /* 16KB for BIOS lib code */

/*----------------------------------------------------------------------------
 * Init / Shutdown
 *----------------------------------------------------------------------------*/

static cui_result_t saturn_storage_init(void)
{
    if (s_initialized) return CUI_OK;

    /* Work buffer needed only during BUP_Init */
    Uint32 work[2048];  /* 8KB */

    slResetDisable();
    BUP_Init(s_bup_lib, work, s_configs);
    slResetEnable();

    /* Verify internal RAM is present */
    if (s_configs[0].unit_id == 0) {
        s_last_error = SATURN_BUP_NOT_CONNECTED;
        return CUI_ERROR_STORAGE_FAILED;
    }

    /* Check if internal RAM needs formatting */
    saturn_bup_stat_t stat;
    Sint32 rc = BUP_Stat(0, 0, &stat);
    if (rc == (Sint32)SATURN_BUP_UNFORMATTED) {
        slResetDisable();
        rc = BUP_Format(0);
        slResetEnable();
        if (rc != (Sint32)SATURN_BUP_OK) {
            s_last_error = (saturn_bup_error_t)rc;
            return CUI_ERROR_STORAGE_FAILED;
        }
    }

    s_initialized = true;
    s_last_error = SATURN_BUP_OK;
    return CUI_OK;
}

static void saturn_storage_shutdown(void)
{
    s_initialized = false;
}

/*----------------------------------------------------------------------------
 * Save
 *----------------------------------------------------------------------------*/

static bool saturn_storage_save(const char* filename, const void* data, uint32_t size)
{
    if (!s_initialized || !filename || !data || size == 0) return false;

    /* Prepare filename */
    char padded[12];
    saturn_storage_pad_filename(padded, filename);

    /* Build directory entry */
    saturn_bup_dir_t dir;
    memset(&dir, 0, sizeof(dir));
    memcpy(dir.filename, padded, 12);
    memcpy(dir.comment, "cui       ", 11); /* Default comment */
    dir.language = 0;
    dir.datasize = size;

    /* Get RTC timestamp */
    slGetStatus();
    saturn_bup_date_t bdate;
    bdate.year   = (Uint8)(slDec2Hex(Smpc_Status->rtc.year) - 1980);
    bdate.month  = (Uint8)(slDec2Hex(Smpc_Status->rtc.month & 0x0F));
    bdate.day    = (Uint8)(slDec2Hex(Smpc_Status->rtc.date));
    bdate.time   = (Uint8)(slDec2Hex(Smpc_Status->rtc.hour));
    bdate.min    = (Uint8)(slDec2Hex(Smpc_Status->rtc.minute));
    bdate.week   = (Uint8)((Smpc_Status->rtc.month >> 4) & 0x0F);
    dir.date = BUP_SetDate(&bdate);

    /* Delete existing file first (overwrite mode) */
    BUP_Delete(s_active_device, padded);

    /* Write */
    slResetDisable();
    Sint32 rc = BUP_Write(s_active_device, &dir, (void*)data, 0);
    slResetEnable();

    if (rc != (Sint32)SATURN_BUP_OK) {
        s_last_error = (saturn_bup_error_t)rc;
        return false;
    }

    /* Verify */
    rc = BUP_Verify(s_active_device, padded, (void*)data);
    if (rc != (Sint32)SATURN_BUP_OK) {
        s_last_error = (saturn_bup_error_t)rc;
        return false;
    }

    s_last_error = SATURN_BUP_OK;
    return true;
}

/*----------------------------------------------------------------------------
 * Load
 *----------------------------------------------------------------------------*/

static bool saturn_storage_load(const char* filename, void* buffer,
                                 uint32_t buffer_size, uint32_t* out_size)
{
    if (!s_initialized || !filename || !buffer || buffer_size == 0) return false;

    char padded[12];
    saturn_storage_pad_filename(padded, filename);

    /* Get file info to check size */
    saturn_bup_dir_t dir;
    Sint32 count = BUP_Dir(s_active_device, padded, 1, &dir);
    if (count <= 0) {
        s_last_error = SATURN_BUP_NOT_FOUND;
        return false;
    }

    if (buffer_size < dir.datasize) {
        s_last_error = SATURN_BUP_BROKEN;  /* Buffer too small */
        return false;
    }

    /* Read */
    Sint32 rc = BUP_Read(s_active_device, padded, buffer);
    if (rc != (Sint32)SATURN_BUP_OK) {
        s_last_error = (saturn_bup_error_t)rc;
        return false;
    }

    if (out_size) *out_size = dir.datasize;

    s_last_error = SATURN_BUP_OK;
    return true;
}

/*----------------------------------------------------------------------------
 * Exists
 *----------------------------------------------------------------------------*/

static bool saturn_storage_exists(const char* filename)
{
    if (!s_initialized || !filename) return false;

    char padded[12];
    saturn_storage_pad_filename(padded, filename);

    saturn_bup_dir_t dir;
    Sint32 count = BUP_Dir(s_active_device, padded, 1, &dir);
    return count > 0;
}

/*----------------------------------------------------------------------------
 * Delete
 *----------------------------------------------------------------------------*/

static bool saturn_storage_delete(const char* filename)
{
    if (!s_initialized || !filename) return false;

    char padded[12];
    saturn_storage_pad_filename(padded, filename);

    slResetDisable();
    Sint32 rc = BUP_Delete(s_active_device, padded);
    slResetEnable();

    if (rc != (Sint32)SATURN_BUP_OK) {
        s_last_error = (saturn_bup_error_t)rc;
        return false;
    }

    s_last_error = SATURN_BUP_OK;
    return true;
}

/*----------------------------------------------------------------------------
 * Free Space
 *----------------------------------------------------------------------------*/

static uint32_t saturn_storage_get_free_space(void)
{
    if (!s_initialized) return 0;

    saturn_bup_stat_t stat;
    Sint32 rc = BUP_Stat(s_active_device, 0, &stat);
    if (rc != (Sint32)SATURN_BUP_OK) {
        s_last_error = (saturn_bup_error_t)rc;
        return 0;
    }

    return stat.freesize;
}

/*----------------------------------------------------------------------------
 * Saturn-Specific API
 *----------------------------------------------------------------------------*/

void saturn_storage_set_device(Uint32 device)
{
    if (device <= 2) s_active_device = device;
}

Uint32 saturn_storage_get_device(void)
{
    return s_active_device;
}

bool saturn_storage_get_device_info(Uint32 device, saturn_bup_stat_t* out)
{
    if (!s_initialized || !out) return false;

    Sint32 rc = BUP_Stat(device, 0, out);
    if (rc != (Sint32)SATURN_BUP_OK) {
        s_last_error = (saturn_bup_error_t)rc;
        return false;
    }
    return true;
}

bool saturn_storage_is_device_connected(Uint32 device)
{
    if (!s_initialized || device > 2) return false;
    return s_configs[device].unit_id != 0;
}

bool saturn_storage_get_file_info(const char* filename, saturn_bup_dir_t* out)
{
    if (!s_initialized || !filename || !out) return false;

    char padded[12];
    saturn_storage_pad_filename(padded, filename);

    Sint32 count = BUP_Dir(s_active_device, padded, 1, out);
    if (count <= 0) {
        s_last_error = SATURN_BUP_NOT_FOUND;
        return false;
    }
    return true;
}

int saturn_storage_list_files(saturn_storage_file_cb_t cb, void* ud)
{
    if (!s_initialized) return -1;

    /* Get file count first */
    saturn_bup_stat_t stat;
    Sint32 rc = BUP_Stat(s_active_device, 0, &stat);
    if (rc != (Sint32)SATURN_BUP_OK) {
        s_last_error = (saturn_bup_error_t)rc;
        return -1;
    }

    if (stat.datanum == 0) return 0;
    if (!cb) return (int)stat.datanum;

    /* List files one at a time using wildcard */
    saturn_bup_dir_t dirs[1];
    /* Use empty string to match all files */
    Sint32 count = BUP_Dir(s_active_device, "", (Uint16)stat.datanum, dirs);
    if (count <= 0) return 0;

    /* Re-query each file individually for proper data */
    /* Note: BUP_Dir with count>1 fills an array, but we iterate safely */
    int listed = 0;
    for (Sint32 i = 0; i < count && i < (Sint32)stat.datanum; i++) {
        if (!cb(&dirs[i], ud)) break;
        listed++;
    }
    return listed;
}

saturn_bup_error_t saturn_storage_get_last_error(void)
{
    return s_last_error;
}

/*----------------------------------------------------------------------------
 * Core Interface vtable
 *----------------------------------------------------------------------------*/

static cui_pal_storage_t s_storage = {
    .init        = saturn_storage_init,
    .shutdown    = saturn_storage_shutdown,
    .save        = saturn_storage_save,
    .load        = saturn_storage_load,
    .exists      = saturn_storage_exists,
    .delete_file = saturn_storage_delete,
    .get_free_space = saturn_storage_get_free_space,
};

cui_pal_storage_t* cui_saturn_storage(void)
{
    return &s_storage;
}

#else /* !__SATURN__ */

/*============================================================================
 * Stub Implementation (non-Saturn builds)
 *============================================================================*/

static saturn_bup_error_t s_last_error = SATURN_BUP_NOT_CONNECTED;
static Uint32 s_active_device = 0;

void saturn_storage_set_device(Uint32 device) { (void)device; }
Uint32 saturn_storage_get_device(void) { return s_active_device; }

bool saturn_storage_get_device_info(Uint32 device, saturn_bup_stat_t* out)
{
    (void)device; (void)out;
    return false;
}

bool saturn_storage_is_device_connected(Uint32 device)
{
    (void)device;
    return false;
}

bool saturn_storage_get_file_info(const char* filename, saturn_bup_dir_t* out)
{
    (void)filename; (void)out;
    return false;
}

int saturn_storage_list_files(saturn_storage_file_cb_t cb, void* ud)
{
    (void)cb; (void)ud;
    return -1;
}

saturn_bup_error_t saturn_storage_get_last_error(void)
{
    return s_last_error;
}

static cui_pal_storage_t s_storage = {
    .init        = NULL,
    .shutdown    = NULL,
    .save        = NULL,
    .load        = NULL,
    .exists      = NULL,
    .delete_file = NULL,
    .get_free_space = NULL,
};

cui_pal_storage_t* cui_saturn_storage(void)
{
    return &s_storage;
}

#endif /* __SATURN__ */
