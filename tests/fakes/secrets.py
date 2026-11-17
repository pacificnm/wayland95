#!/usr/bin/python3 -I
# Stand-in for the desktop's keyring (the freedesktop Secret Service,
# org.freedesktop.secrets) on the test's own session bus: secrets kept in
# memory and written to FILE (JSON: label, attributes, secret) for the test
# to read. Never the real keyring. Calls are logged.
#
#   secrets.py LOG FILE
import json
import sys
from gi.repository import Gio, GLib

XML = """
<node>
  <interface name="org.freedesktop.Secret.Service">
    <method name="OpenSession"><arg type="s" direction="in"/><arg type="v" direction="in"/>
      <arg type="v" direction="out"/><arg type="o" direction="out"/></method>
    <method name="SearchItems"><arg type="a{ss}" direction="in"/><arg type="ao" direction="out"/><arg type="ao" direction="out"/></method>
    <method name="Unlock"><arg type="ao" direction="in"/><arg type="ao" direction="out"/><arg type="o" direction="out"/></method>
    <method name="GetSecrets"><arg type="ao" direction="in"/><arg type="o" direction="in"/>
      <arg type="a{o(oayays)}" direction="out"/></method>
  </interface>
  <interface name="org.freedesktop.Secret.Collection">
    <method name="CreateItem"><arg type="a{sv}" direction="in"/><arg type="(oayays)" direction="in"/><arg type="b" direction="in"/>
      <arg type="o" direction="out"/><arg type="o" direction="out"/></method>
  </interface>
  <interface name="org.freedesktop.Secret.Item">
    <method name="Delete"><arg type="o" direction="out"/></method>
  </interface>
</node>
"""
node = Gio.DBusNodeInfo.new_for_xml(XML)
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
log = open(sys.argv[1], "a", buffering=1)
store_path = sys.argv[2]
items = {}  # path -> {label, attributes, secret}
counter = [0]
registered = {}


def save():
    json.dump(list(items.values()), open(store_path, "w"))


def register_item(path):
    registered[path] = bus.register_object(path, node.interfaces[2], item_call, None, None)


def item_call(conn, sender, path, iface, method, params, inv):
    log.write("secrets: %s %s\n" % (method, path))
    items.pop(path, None)
    if path in registered:
        bus.unregister_object(registered.pop(path))
    save()
    inv.return_value(GLib.Variant("(o)", ("/",)))


def call(conn, sender, path, iface, method, params, inv):
    args = params.unpack()
    log.write("secrets: %s\n" % method)
    if method == "OpenSession":
        inv.return_value(GLib.Variant("(vo)", (GLib.Variant("s", ""), "/org/freedesktop/secrets/session/1")))
    elif method == "SearchItems":
        want = args[0]
        found = [p for p, it in items.items() if all(it["attributes"].get(k) == v for k, v in want.items())]
        inv.return_value(GLib.Variant("(aoao)", (found, [])))
    elif method == "Unlock":
        inv.return_value(GLib.Variant("(aoo)", (args[0], "/")))
    elif method == "GetSecrets":
        out = {p: ("/org/freedesktop/secrets/session/1", b"", items[p]["secret"].encode(), "text/plain")
               for p in args[0] if p in items}
        inv.return_value(GLib.Variant("(a{o(oayays)})", (out,)))
    elif method == "CreateItem":
        props, secret, replace = args
        attrs = props.get("org.freedesktop.Secret.Item.Attributes", {})
        if replace:
            for p in [p for p, it in items.items() if it["attributes"] == attrs]:
                items.pop(p)
        counter[0] += 1
        path = "/org/freedesktop/secrets/collection/login/%d" % counter[0]
        items[path] = {"label": props.get("org.freedesktop.Secret.Item.Label", ""), "attributes": attrs,
                       "secret": bytes(secret[2]).decode()}
        register_item(path)
        save()
        inv.return_value(GLib.Variant("(oo)", (path, "/")))


bus.register_object("/org/freedesktop/secrets", node.interfaces[0], call, None, None)
bus.register_object("/org/freedesktop/secrets/aliases/default", node.interfaces[1], call, None, None)
save()


def acquired(conn, name):
    log.write("secrets: ready\n")


Gio.bus_own_name_on_connection(bus, "org.freedesktop.secrets", Gio.BusNameOwnerFlags.NONE, acquired, None)
GLib.MainLoop().run()
