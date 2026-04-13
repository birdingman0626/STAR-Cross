/* config.h for bundled htslib 1.21 in STAR */
/* Minimal configuration - only zlib required */

/* #undef HAVE_LIBBZ2 */
/* #undef HAVE_LIBLZMA */
/* #undef HAVE_LIBDEFLATE */
/* #undef HAVE_LIBCURL */
/* #undef HAVE_EXTERNAL_HTSCODECS */

#ifdef _WIN32
#define HAVE_DRAND48
#endif
