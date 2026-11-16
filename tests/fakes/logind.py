#!/usr/bin/python3 -I
# Stand-in for logind (and hostname1) on the test's own session bus (see
# README.md): a laptop that can sleep, its lid open. The test closes and
# opens the lid with SetLid (not logind's: only here). Calls are logged.
import os
import sys
from gi.repository import Gio, GLib

XML = """
<node>
  <interface name="org.freedesktop.login1.Manager">
    <method name="CanSuspend"><arg type="s" direction="out"/></method>
    <method name="CanHibernate"><arg type="s" direction="out"/></method>
    <method name="Suspend"><arg type="b" direction="in"/></method>
    <method name="Hibernate"><arg type="b" direction="in"/></method>
    <method name="PowerOff"><arg type="b" direction="in"/></method>
    <method name="Reboot"><arg type="b" direction="in"/></method>
    <method name="Inhibit">
      <arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="s" direction="in"/>
      <arg type="h" direction="out"/>
    </method>
    <method name="SetLid"><arg type="b" direction="in"/></method>
    <property name="LidClosed" type="b" access="read"/>
  </interface>
  <interface name="org.freedesktop.hostname1">
    <property name="Chassis" type="s" access="read"/>
  </interface>
</node>
"""
node = Gio.DBusNodeInfo.new_for_xml(XML)
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
log = open(sys.argv[1] if len(sys.argv) > 1 else "/dev/null", "a", buffering=1)
state = {"lid": False}


def manager_call(conn, sender, path, iface, method, params, invocation):
    args = params.unpack()
    log.write("logind: %s %s\n" % (method, args))
    if method in ("CanSuspend", "CanHibernate"):
        invocation.return_value(GLib.Variant("(s)", ("yes",)))
    elif method == "Inhibit":
        r, w = os.pipe()
        os.close(r)
        fds = Gio.UnixFDList.new_from_array([w])
        invocation.return_value_with_unix_fd_list(GLib.Variant("(h)", (0,)), fds)
    elif method == "SetLid":
        state["lid"] = args[0]
        invocation.return_value(None)
        bus.emit_signal(None, "/org/freedesktop/login1", "org.freedesktop.DBus.Properties", "PropertiesChanged",
                        GLib.Variant("(sa{sv}as)", ("org.freedesktop.login1.Manager",
                                                    {"LidClosed": GLib.Variant("b", state["lid"])}, [])))
    else:
        invocation.return_value(None)


def manager_get(conn, sender, path, iface, prop):
    return GLib.Variant("b", state["lid"])


def hostname_get(conn, sender, path, iface, prop):
    return GLib.Variant("s", "laptop")


bus.register_object("/org/freedesktop/login1", node.interfaces[0], manager_call, manager_get, None)
names = ["org.freedesktop.login1", "org.freedesktop.hostname1"]
owned = []
def got(conn, name):
    owned.append(name)
    if len(owned) == len(names):
        log.write("logind: ready\n")
bus.register_object("/org/freedesktop/hostname1", node.interfaces[1], None, hostname_get, None)
for n in names:
    Gio.bus_own_name_on_connection(bus, n, Gio.BusNameOwnerFlags.NONE, got, None)
GLib.MainLoop().run()
