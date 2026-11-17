#!/usr/bin/python3 -I
# Stand-in for UDisks2 on the test's own session bus (see README.md): USB
# drives that "mount" as folders under $W95_FAKE_MEDIA (with a file in
# each), unmount, and power off (they're gone, as when pulled out). The test
# plugs one in with Plug(label) and pulls one out with Pull(label) (not
# UDisks2's: only here); a file "busy"
# next to the log makes Unmount fail as it does when files are open. The
# computer's own disk is there too, as a system one (never listed). Calls
# are logged.
import os
import shutil
import sys
from gi.repository import Gio, GLib

ROOT = "/org/freedesktop/UDisks2"
XML = """
<node>
  <interface name="org.freedesktop.DBus.ObjectManager">
    <method name="GetManagedObjects"><arg type="a{oa{sa{sv}}}" direction="out"/></method>
    <signal name="InterfacesAdded"><arg type="o"/><arg type="a{sa{sv}}"/></signal>
    <signal name="InterfacesRemoved"><arg type="o"/><arg type="as"/></signal>
  </interface>
  <interface name="org.wayland95.Test">
    <method name="Plug"><arg type="s" direction="in"/></method>
    <method name="Pull"><arg type="s" direction="in"/></method>
  </interface>
</node>
"""
OBJ_XML = """
<node>
  <interface name="org.freedesktop.UDisks2.Filesystem">
    <method name="Mount"><arg type="a{sv}" direction="in"/><arg type="s" direction="out"/></method>
    <method name="Unmount"><arg type="a{sv}" direction="in"/></method>
  </interface>
  <interface name="org.freedesktop.UDisks2.Drive">
    <method name="PowerOff"><arg type="a{sv}" direction="in"/></method>
    <method name="Eject"><arg type="a{sv}" direction="in"/></method>
  </interface>
</node>
"""
root_node = Gio.DBusNodeInfo.new_for_xml(XML)
obj_node = Gio.DBusNodeInfo.new_for_xml(OBJ_XML)
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
log_path = sys.argv[1] if len(sys.argv) > 1 else "/dev/null"
log = open(log_path, "a", buffering=1)
media = os.environ.get("W95_FAKE_MEDIA", "/tmp/w95-fake-media")
busy_file = os.path.join(os.path.dirname(log_path), "busy")

# path -> {interface: {prop: GLib.Variant}}
objects = {}
registrations = {}
mounted = {}  # block path -> folder


def ay(s):
    return GLib.Variant("ay", s.encode() + b"\0")


def block_props(dev, label, size, drive, system=False):
    return {
        "Device": ay(dev), "PreferredDevice": ay(dev), "IdLabel": GLib.Variant("s", label),
        "IdType": GLib.Variant("s", "vfat" if not system else "ext4"), "IdUsage": GLib.Variant("s", "filesystem"),
        "Size": GLib.Variant("t", size), "HintIgnore": GLib.Variant("b", False), "HintSystem": GLib.Variant("b", system),
        "HintName": GLib.Variant("s", ""), "Drive": GLib.Variant("o", drive),
        "CryptoBackingDevice": GLib.Variant("o", "/"),
    }


def add_drive(name, label, size, usb=True):
    drive = ROOT + "/drives/" + name
    block = ROOT + "/block_devices/" + name.lower() + "1"
    objects[drive] = {"org.freedesktop.UDisks2.Drive": {
        "Vendor": GLib.Variant("s", "Test"), "Model": GLib.Variant("s", name),
        "Removable": GLib.Variant("b", usb), "Ejectable": GLib.Variant("b", False),
        "MediaRemovable": GLib.Variant("b", False), "ConnectionBus": GLib.Variant("s", "usb" if usb else ""),
        "CanPowerOff": GLib.Variant("b", usb)}}
    objects[block] = {
        "org.freedesktop.UDisks2.Block": block_props("/dev/" + name.lower() + "1", label, size, drive, not usb),
        "org.freedesktop.UDisks2.Filesystem": {"MountPoints": GLib.Variant("aay", [])},
    }
    for path in (drive, block):
        register(path)
        bus.emit_signal(None, ROOT, "org.freedesktop.DBus.ObjectManager", "InterfacesAdded",
                        GLib.Variant("(oa{sa{sv}})", (path, objects[path])))
    return block


def remove(path):
    ifaces = list(objects.pop(path, {}).keys())
    if path in registrations:
        for rid in registrations.pop(path):
            bus.unregister_object(rid)
    bus.emit_signal(None, ROOT, "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved",
                    GLib.Variant("(oas)", (path, ifaces)))


def set_mounts(path, folders):
    v = GLib.Variant("aay", [f.encode() + b"\0" for f in folders])
    objects[path]["org.freedesktop.UDisks2.Filesystem"]["MountPoints"] = v
    bus.emit_signal(None, path, "org.freedesktop.DBus.Properties", "PropertiesChanged",
                    GLib.Variant("(sa{sv}as)", ("org.freedesktop.UDisks2.Filesystem", {"MountPoints": v}, [])))


def error(invocation, name, message):
    invocation.return_dbus_error("org.freedesktop.UDisks2.Error." + name, message)


def object_call(conn, sender, path, iface, method, params, invocation):
    log.write("udisks: %s %s\n" % (method, path))
    if path not in objects:
        return error(invocation, "Failed", "No such object")
    if method == "Mount":
        if path in mounted:
            return error(invocation, "AlreadyMounted", "Already mounted")
        label = objects[path]["org.freedesktop.UDisks2.Block"]["IdLabel"].unpack() or "disk"
        folder = os.path.join(media, label)
        os.makedirs(folder, exist_ok=True)
        with open(os.path.join(folder, "on-the-stick.txt"), "w") as f:
            f.write("hello from %s\n" % label)
        mounted[path] = folder
        set_mounts(path, [folder])
        invocation.return_value(GLib.Variant("(s)", (folder,)))
    elif method == "Unmount":
        if os.path.exists(busy_file):
            return error(invocation, "DeviceBusy", "Error unmounting: target is busy")
        folder = mounted.pop(path, None)
        if folder:
            shutil.rmtree(folder, ignore_errors=True)
        set_mounts(path, [])
        invocation.return_value(None)
    elif method in ("PowerOff", "Eject"):
        for block, props in list(objects.items()):
            b = props.get("org.freedesktop.UDisks2.Block")
            if b and b["Drive"].unpack() == path:
                remove(block)
        remove(path)
        invocation.return_value(None)


def register(path):
    ids = []
    for info in obj_node.interfaces:
        if info.name in objects[path]:
            ids.append(bus.register_object(path, info, object_call, None, None))
    registrations[path] = ids


def root_call(conn, sender, path, iface, method, params, invocation):
    if method == "GetManagedObjects":
        invocation.return_value(GLib.Variant("(a{oa{sa{sv}}})", (objects,)))
    elif method == "Plug":
        label = params.unpack()[0]
        log.write("udisks: Plug %s\n" % label)
        add_drive(label.upper().replace(" ", "_"), label, 16 * 1000 ** 3)
        invocation.return_value(None)
    elif method == "Pull":
        # pulled out as it is, mounted or not (as the kernel sees it go)
        name = params.unpack()[0].upper().replace(" ", "_")
        log.write("udisks: Pull %s\n" % name)
        remove(ROOT + "/block_devices/" + name.lower() + "1")
        remove(ROOT + "/drives/" + name)
        invocation.return_value(None)


for info in root_node.interfaces:
    bus.register_object(ROOT, info, root_call, None, None)
add_drive("Internal", "", 512 * 1000 ** 3, usb=False)
add_drive("STICK", "STICK", 32 * 1000 ** 3)


def acquired(conn, name):
    log.write("udisks: ready\n")


Gio.bus_own_name_on_connection(bus, "org.freedesktop.UDisks2", Gio.BusNameOwnerFlags.NONE, acquired, None)
GLib.MainLoop().run()
