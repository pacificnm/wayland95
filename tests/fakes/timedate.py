#!/usr/bin/python3 -I
# Stand-in for systemd-timedated (org.freedesktop.timedate1) on the test's
# own session bus (see README.md): its time zone starts as argv[2] (or
# America/New_York); SetTimezone changes it (any real zone) and says so,
# as timedated does. Calls are logged.
import sys
import os
from gi.repository import Gio, GLib

XML = """
<node>
  <interface name="org.freedesktop.timedate1">
    <method name="SetTimezone"><arg type="s" direction="in"/><arg type="b" direction="in"/></method>
    <method name="ListTimezones"><arg type="as" direction="out"/></method>
    <property name="Timezone" type="s" access="read"/>
  </interface>
</node>
"""
node = Gio.DBusNodeInfo.new_for_xml(XML)
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
log = open(sys.argv[1] if len(sys.argv) > 1 else "/dev/null", "a", buffering=1)
state = {"zone": sys.argv[2] if len(sys.argv) > 2 else "America/New_York"}


def call(conn, sender, path, iface, method, params, invocation):
    args = params.unpack()
    log.write("timedate: %s %s\n" % (method, args))
    if method == "SetTimezone":
        zone = args[0]
        if not os.path.exists("/usr/share/zoneinfo/" + zone) or ".." in zone:
            invocation.return_dbus_error("org.freedesktop.timedate1.InvalidTimeZone", "Invalid or not installed time zone '%s'" % zone)
            return
        state["zone"] = zone
        invocation.return_value(None)
        bus.emit_signal(None, "/org/freedesktop/timedate1", "org.freedesktop.DBus.Properties", "PropertiesChanged",
                        GLib.Variant("(sa{sv}as)", ("org.freedesktop.timedate1", {"Timezone": GLib.Variant("s", zone)}, [])))
    elif method == "ListTimezones":
        invocation.return_value(GLib.Variant("(as)", (["America/New_York", "Asia/Tokyo"],)))
    else:
        invocation.return_value(None)


def get(conn, sender, path, iface, prop):
    return GLib.Variant("s", state["zone"])


bus.register_object("/org/freedesktop/timedate1", node.interfaces[0], call, get, None)
Gio.bus_own_name_on_connection(bus, "org.freedesktop.timedate1", Gio.BusNameOwnerFlags.NONE, None, None)
log.write("timedate: ready\n")
GLib.MainLoop().run()
