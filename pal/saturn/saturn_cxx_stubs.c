/**
 * saturn_cxx_stubs.c - C++ runtime stubs for XMP library
 *
 * The XMP library was compiled with the Hitachi C++ compiler and
 * references C++ runtime functions. We provide minimal stubs here.
 */

#ifdef __SATURN__

/* C++ delete operator - called by XMP library's modem code */
void __builtin_delete(void* ptr)
{
    /* In a freestanding Saturn environment with Jo Engine,
     * we can use jo_free if memory was allocated with jo_malloc.
     * However, since the XMP library was compiled separately,
     * it likely used the original SDK's memory management.
     * For now, this is a no-op since the XMP library handles
     * its own memory internally. */
    (void)ptr;
}

/* C++ new operator - in case it's needed */
void* __builtin_new(unsigned long size)
{
    /* Jo Engine's malloc */
    extern void* jo_malloc(unsigned int size);
    return jo_malloc((unsigned int)size);
}

#endif /* __SATURN__ */
