/**
 * @file version.h
 * @brief Firmware version information
 *
 * Defines the firmware version number displayed on web pages and used
 * for update compatibility checks.
 */

#ifndef VERSION_H
#define VERSION_H

// Firmware version components
#define FIRMWARE_VERSION_MAJOR 0
#define FIRMWARE_VERSION_MINOR 11
#define FIRMWARE_VERSION_PATCH 0

// Version string for display
#define FIRMWARE_VERSION_STRING "0.11.0"

// Build type indicator
#ifdef FACTORY_INTERNAL_VERSION
    #define FIRMWARE_BUILD_TYPE "FACTORY INTERNAL"
    #define FIRMWARE_BUILD_TYPE_SHORT "FACTORY"
#else
    #define FIRMWARE_BUILD_TYPE "PRODUCTION"
    #define FIRMWARE_BUILD_TYPE_SHORT "PROD"
#endif

// Combined version with build type
#ifdef FACTORY_INTERNAL_VERSION
    #define FIRMWARE_FULL_VERSION "v" FIRMWARE_VERSION_STRING " (FACTORY INTERNAL)"
#else
    #define FIRMWARE_FULL_VERSION "v" FIRMWARE_VERSION_STRING
#endif

#endif // VERSION_H
