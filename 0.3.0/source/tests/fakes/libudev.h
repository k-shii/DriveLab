#pragma once

// Test-only ABI subset. The production target always uses the system libudev.h.
// This header cannot open hardware and is visible only to the native-source test.
extern "C" {
struct udev;
struct udev_enumerate;
struct udev_list_entry;
struct udev_device;

udev* udev_new();
udev* udev_unref(udev*);
udev_enumerate* udev_enumerate_new(udev*);
udev_enumerate* udev_enumerate_unref(udev_enumerate*);
int udev_enumerate_add_match_subsystem(udev_enumerate*, const char*);
int udev_enumerate_scan_devices(udev_enumerate*);
udev_list_entry* udev_enumerate_get_list_entry(udev_enumerate*);
udev_list_entry* udev_list_entry_get_next(udev_list_entry*);
const char* udev_list_entry_get_name(udev_list_entry*);
udev_device* udev_device_new_from_syspath(udev*, const char*);
udev_device* udev_device_unref(udev_device*);
const char* udev_device_get_sysname(udev_device*);
const char* udev_device_get_syspath(udev_device*);
const char* udev_device_get_devnode(udev_device*);
const char* udev_device_get_devtype(udev_device*);
const char* udev_device_get_property_value(udev_device*, const char*);
const char* udev_device_get_sysattr_value(udev_device*, const char*);
udev_device* udev_device_get_parent_with_subsystem_devtype(udev_device*, const char*, const char*);
}
