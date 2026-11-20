#!/usr/bin/python3 -I
# Stand-in for PackageKit on the test's own session bus (see README.md).
# Every GetUpdates finds two updates; RefreshCache just finishes. One
# program is installed: "zeta", which owns any .desktop file named zeta*.desktop
# (Add/Remove Programs lists it; Start menu > Uninstall... removes it).
import sys
from gi.repository import Gio, GLib

XML = """
<node>
  <interface name="org.freedesktop.PackageKit">
    <method name="CreateTransaction"><arg type="o" direction="out"/></method>
  </interface>
  <interface name="org.freedesktop.PackageKit.Transaction">
    <method name="SetHints"><arg type="as" direction="in"/></method>
    <method name="RefreshCache"><arg type="b" direction="in"/></method>
    <method name="GetUpdates"><arg type="t" direction="in"/></method>
    <method name="GetPackages"><arg type="t" direction="in"/></method>
    <method name="SearchFiles"><arg type="t" direction="in"/><arg type="as" direction="in"/></method>
    <method name="GetFiles"><arg type="as" direction="in"/></method>
    <method name="RemovePackages"><arg type="t" direction="in"/><arg type="as" direction="in"/>
      <arg type="b" direction="in"/><arg type="b" direction="in"/></method>
    <signal name="Package"><arg type="u"/><arg type="s"/><arg type="s"/></signal>
    <signal name="Files"><arg type="s"/><arg type="as"/></signal>
    <signal name="Finished"><arg type="u"/><arg type="u"/></signal>
  </interface>
</node>
"""
node = Gio.DBusNodeInfo.new_for_xml(XML)
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
count = 0
log = open(sys.argv[1] if len(sys.argv) > 1 else "/dev/null", "a", buffering=1)


def emit(path, name, signature, args):
    bus.emit_signal(None, path, "org.freedesktop.PackageKit.Transaction", name, GLib.Variant(signature, args))


ZETA = "zeta;1.0;amd64;installed"
zeta_files = []  # the zeta*.desktop files SearchFiles was asked about


def finish_later(path, signals=()):
    def later():
        for name, signature, args in signals:
            emit(path, name, signature, args)
        emit(path, "Finished", "(uu)", (1, 10))
        return False
    GLib.timeout_add(100, later)


def transaction_call(conn, sender, path, iface, method, params, invocation):
    log.write("packagekit: %s %s\n" % (method, params.unpack()))
    invocation.return_value(None)
    if method == "GetPackages":
        finish_later(path, [("Package", "(uss)", (1, ZETA, "Zeta"))])
    elif method == "SearchFiles":
        found = [f for f in params.unpack()[1] if f.rsplit("/", 1)[-1].startswith("zeta")]
        zeta_files[:] = found
        finish_later(path, [("Package", "(uss)", (1, ZETA, "Zeta"))] if found else [])
    elif method == "GetFiles":
        finish_later(path, [("Files", "(sas)", (ZETA, zeta_files))])
    elif method == "RemovePackages":
        finish_later(path)
    if method == "GetUpdates":
        def later():
            emit(path, "Package", "(uss)", (8, "wayland95;0.9.9;amd64;wayland95", "Wayland 95"))
            emit(path, "Package", "(uss)", (2, "hello;2.10-4;amd64;debian", "hello"))
            emit(path, "Finished", "(uu)", (1, 10))
            return False
        GLib.timeout_add(100, later)
    elif method == "RefreshCache":
        GLib.timeout_add(100, lambda: emit(path, "Finished", "(uu)", (1, 10)) or False)


def root_call(conn, sender, path, iface, method, params, invocation):
    global count
    count += 1
    tpath = "/%d_test" % count
    bus.register_object(tpath, node.interfaces[1], transaction_call, None, None)
    log.write("packagekit: CreateTransaction -> %s\n" % tpath)
    invocation.return_value(GLib.Variant("(o)", (tpath,)))


bus.register_object("/org/freedesktop/PackageKit", node.interfaces[0], root_call, None, None)
Gio.bus_own_name_on_connection(bus, "org.freedesktop.PackageKit", Gio.BusNameOwnerFlags.NONE,
                               lambda *a: log.write("packagekit: ready\n"), None)
GLib.MainLoop().run()
