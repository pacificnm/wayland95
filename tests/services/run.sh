#!/bin/bash
# Tests wayland95-services (the Services control panel's root helper)
# without root and without touching this computer: a copy of it works on a
# scratch folder in place of /etc and /sys, with stand-ins for systemctl,
# wg-quick and wg's "show" (tests/services/fakes; keys come from the real
# wg, which needs no root for them).
#
#   tests/services/run.sh           the tests
#   tests/services/run.sh --setup   only make the scratch copy, and print
#                                   what to set to try the windows by hand:
#                                   W95_FAKE_ROOT and a PATH with the
#                                   stand-in pkexec first
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
command -v wg >/dev/null || { echo "Missing wg (sudo apt install wireguard-tools)" >&2; exit 2; }

ROOT=$(mktemp -d /tmp/wayland95-services.XXXXXX)
export W95_FAKE_ROOT=$ROOT
mkdir -p "$ROOT/etc" "$ROOT/sys/class/net"
sed -e "s|^PATH=.*|PATH=$HERE/fakes:/usr/sbin:/usr/bin:/sbin:/bin|" \
    -e "s|/etc/|$ROOT/etc/|g" -e "s|/sys/class/net|$ROOT/sys/class/net|g" \
    "$REPO/session/wayland95-services" >"$ROOT/helper"
chmod +x "$ROOT/helper"
if [ "${1:-}" = --setup ]; then
    echo "export W95_FAKE_ROOT=$ROOT"
    echo "export PATH=$HERE/fakes:\$PATH"
    exit 0
fi
trap 'rm -rf "$ROOT"' EXIT

pass=0 fail=0
ok() { echo "PASS  $1"; pass=$((pass + 1)); }
bad() { echo "FAIL  $1"; [ -n "${2:-}" ] && echo "      $2"; fail=$((fail + 1)); }
expect() { if [ "$1" -eq 0 ]; then ok "$2"; else bad "$2" "${3:-}"; fi; }
helper() { "$ROOT/helper" "$@"; }   # stdin as given
WG=$ROOT/etc/wireguard
conf() { cat "$WG/$1.conf" 2>/dev/null; }
value() { sed -n "s/^$2 = //p" "$WG/$1.conf" | head -n 1; }

echo "== wayland95-services: WireGuard"
out=$(helper wg-list 2>&1); [ -z "$out" ]; expect $? "no tunnels: wg-list says nothing" "$out"

# --- a new tunnel ---------------------------------------------------------------
out=$(printf '[interface]\nPrivateKey=generate\n' | helper wg-save home 2>&1)
[ $? -eq 0 ] && [ -f "$WG/home.conf" ]; expect $? "wg-save with a key to be made: the tunnel's file is there" "$out"
[ "$(stat -c %a "$WG/home.conf")" = 600 ] && [ "$(stat -c %a "$WG")" = 700 ]
expect $? "(only root may read it: 600 in a 700 folder)" "$(stat -c %a "$WG/home.conf") in $(stat -c %a "$WG")"
key=$(value home PrivateKey)
[ ${#key} -eq 44 ]; expect $? "(with a private key made for it)" "${#key} characters"
pub=$(printf '%s' "$key" | wg pubkey)
shown=$(helper wg-show home 2>&1)
echo "$shown" | grep -qx "PublicKey=$pub" && ! echo "$shown" | grep -q "$key" && ! echo "$shown" | grep -q PrivateKey
expect $? "wg-show gives its public key, never the private one" "$shown"
[ "$(helper wg-list)" = "home no no" ]; expect $? "wg-list: there, not started, not connected" "$(helper wg-list)"

# --- its settings and a peer ------------------------------------------------------
peer1=$(wg genkey | wg pubkey) psk1=$(wg genpsk)
out=$(printf '%s\n' '[interface]' 'PrivateKey=keep' 'Address=10.200.200.11/32, fd00::11/128' 'ListenPort=36750' 'DNS=10.200.200.1' 'MTU=' \
    '[peer]' "PublicKey=$peer1" "PresharedKey=$psk1" 'AllowedIPs=10.200.200.0/24, 192.168.50.0/24' 'Endpoint=vpn.example.com:51820' 'PersistentKeepalive=25' \
    | helper wg-save home 2>&1)
[ $? -eq 0 ] && [ "$(value home PrivateKey)" = "$key" ] && [ "$(value home Address)" = "10.200.200.11/32, fd00::11/128" ] \
    && [ "$(value home ListenPort)" = 36750 ] && [ "$(value home DNS)" = 10.200.200.1 ] && ! grep -q '^MTU' "$WG/home.conf" \
    && [ "$(value home PublicKey)" = "$peer1" ] && [ "$(value home PresharedKey)" = "$psk1" ] \
    && [ "$(value home Endpoint)" = vpn.example.com:51820 ] && [ "$(value home PersistentKeepalive)" = 25 ]
expect $? "wg-save: addresses, port, DNS and a peer are written; \"keep\" keeps the private key" "$out: $(conf home | tr '\n' '|')"
shown=$(helper wg-show home)
echo "$shown" | grep -qx "PresharedKey=yes" && ! echo "$shown" | grep -q "$psk1"
expect $? "wg-show says a preshared key is set, not what it is" "$shown"
out=$(printf '%s\n' '[interface]' 'PrivateKey=keep' 'Address=10.200.200.11/32' '[peer]' "PublicKey=$peer1" 'PresharedKey=keep' 'AllowedIPs=10.200.200.0/24' \
    | helper wg-save home 2>&1)
[ $? -eq 0 ] && [ "$(value home PresharedKey)" = "$psk1" ]; expect $? "a peer's preshared key: \"keep\" keeps it" "$out"
printf '%s\n' '[interface]' 'PrivateKey=keep' '[peer]' "PublicKey=$peer1" 'PresharedKey=none' 'AllowedIPs=10.200.200.0/24' | helper wg-save home >/dev/null 2>&1
! grep -q PresharedKey "$WG/home.conf"; expect $? "(\"none\" removes it)" "$(conf home | tr '\n' '|')"
printf '%s\n' '[interface]' 'PrivateKey=generate' | helper wg-save home >/dev/null 2>&1
[ "$(value home PrivateKey)" != "$key" ] && [ ${#key} -eq 44 ]; expect $? "\"generate\" on a tunnel that has a key: a new one" ""

# --- what it refuses ----------------------------------------------------------------
before=$(conf home)
refuse() { # NAME WHAT INPUT...
    local name=$1 what=$2; shift 2
    local out; out=$(printf '%s\n' "$@" | helper wg-save "$name" 2>&1)
    [ $? -ne 0 ] && [ "$(conf home)" = "$before" ]; expect $? "refused: $what" "accepted, or the file changed: $out"
}
refuse home "a command to run as root (PostUp)" '[interface]' 'PrivateKey=keep' 'PostUp=touch /tmp/owned'
refuse home "a command hidden in an \"other\" line" '[interface]' 'PrivateKey=keep' 'other=PostUp = touch /tmp/owned'
[ "$(conf home)" = "$before" ] && ! grep -q PostUp "$WG/home.conf"; expect $? "(and none of it reached the file)" "$(conf home | tr '\n' '|')"
refuse home "a private key that isn't one" '[interface]' 'PrivateKey=hello'
refuse home "an address with a command in it" '[interface]' 'PrivateKey=keep' 'Address=10.0.0.1/32; rm -rf /'
refuse home "a second line smuggled into a value" '[interface]' 'PrivateKey=keep' "DNS=1.1.1.1\$(printf '\nPostUp = id')"
refuse home "a peer without a public key" '[interface]' 'PrivateKey=keep' '[peer]' 'AllowedIPs=10.0.0.0/24'
refuse home "an endpoint without a port" '[interface]' 'PrivateKey=keep' '[peer]' "PublicKey=$peer1" 'Endpoint=vpn.example.com'
refuse home "a port out of range" '[interface]' 'PrivateKey=keep' 'ListenPort=70000'
refuse home "no interface at all" '[peer]' "PublicKey=$peer1"
for name in '../evil' 'a/b' 'averyveryverylongname' '-x y' ''; do
    out=$(printf '[interface]\nPrivateKey=generate\n' | helper wg-save "$name" 2>&1)
    [ $? -ne 0 ] || { bad "refused: the name \"$name\"" "$out"; continue; }
    ok "refused: the name \"$name\""
done
[ "$(ls "$WG")" = home.conf ] && [ "$(find "$ROOT" -name '*evil*' | wc -l)" = 0 ]
expect $? "(nothing was written anywhere for those)" "$(ls "$WG") $(find "$ROOT" -name '*evil*')"
out=$(printf '[interface]\nPrivateKey=keep\n' | helper wg-save other 2>&1)
[ $? -ne 0 ] && [ ! -e "$WG/other.conf" ]; expect $? "refused: \"keep\" for a tunnel that has no key yet" "$out"

# --- a file with settings of its own (by hand, or another program's) ---------------------
hand=$(wg genkey)
printf '%s\n' '# my tunnel' '[Interface]' "PrivateKey = $hand" 'Address = 10.9.0.2/24' 'Table = off' 'PostUp = ip route add 10.9.0.0/16 dev %i' \
    'FwMark = 0x1234' '' '[Peer]' "PublicKey = $peer1" 'AllowedIPs = 10.9.0.0/16' 'Endpoint = [2001:db8::1]:51820' >"$WG/hand.conf"
chmod 600 "$WG/hand.conf"
shown=$(helper wg-show hand)
echo "$shown" | grep -qx 'other=PostUp = ip route add 10.9.0.0/16 dev %i' && echo "$shown" | grep -qx 'other=Table = off' \
    && echo "$shown" | grep -qx 'Endpoint=\[2001:db8::1\]:51820'
expect $? "a file written by hand: its other settings and commands are shown (an IPv6 endpoint too)" "$shown"
out=$(printf '%s\n' '[interface]' 'PrivateKey=keep' 'Address=10.9.0.3/24' '[peer]' "PublicKey=$peer1" 'AllowedIPs=10.9.0.0/16' 'Endpoint=[2001:db8::1]:51820' \
    | helper wg-save hand 2>&1)
[ $? -eq 0 ] && grep -qx 'PostUp = ip route add 10.9.0.0/16 dev %i' "$WG/hand.conf" && grep -qx 'Table = off' "$WG/hand.conf" \
    && grep -qx 'FwMark = 0x1234' "$WG/hand.conf" && [ "$(value hand Address)" = 10.9.0.3/24 ] && [ "$(value hand PrivateKey)" = "$hand" ]
expect $? "saving it keeps them (and its key), with the change made" "$out: $(conf hand | tr '\n' '|')"

# --- importing a .conf -----------------------------------------------------------------
imp=$(wg genkey)
out=$(printf '%s\r\n' '[Interface]' "PrivateKey = $imp   # mine" 'Address = 10.8.0.5/32' 'Address = fd08::5/128' 'DNS = 9.9.9.9, example.lan' \
    'PostUp = iptables -I OUTPUT -j DROP' 'PreDown = echo bye' '' '[Peer]' "PublicKey = $peer1" "PresharedKey = $psk1" \
    'AllowedIPs = 0.0.0.0/0, ::/0' 'Endpoint = 203.0.113.9:443' 'PersistentKeepalive = 25' | helper wg-import work 2>&1)
[ $? -eq 0 ] && [ "$(value work PrivateKey)" = "$imp" ] && [ "$(value work Address)" = "10.8.0.5/32, fd08::5/128" ] \
    && [ "$(value work DNS)" = "9.9.9.9, example.lan" ] && [ "$(value work PresharedKey)" = "$psk1" ] \
    && [ "$(value work AllowedIPs)" = "0.0.0.0/0, ::/0" ] && [ "$(stat -c %a "$WG/work.conf")" = 600 ]
expect $? "wg-import: a .conf from elsewhere (Windows line ends, a comment, two Address lines) becomes a tunnel" "$out: $(conf work | tr '\n' '|')"
! grep -qi 'PostUp\|PreDown\|iptables' "$WG/work.conf" && echo "$out" | grep -q "Left out" && echo "$out" | grep -q PostUp && echo "$out" | grep -q PreDown
expect $? "(its commands are left out, and it says so)" "$out"
out=$(printf '%s\n' '[Interface]' "PrivateKey = $imp" | helper wg-import work 2>&1)
[ $? -ne 0 ] && [ "$(value work Address)" = "10.8.0.5/32, fd08::5/128" ]; expect $? "refused: importing over a tunnel that's there" "$out"
out=$(printf '%s\n' '[Interface]' 'Address = 10.0.0.1/24' | helper wg-import nokey 2>&1)
[ $? -ne 0 ] && [ ! -e "$WG/nokey.conf" ]; expect $? "refused: a .conf with no private key" "$out"

# --- connecting, and starting with the computer ------------------------------------------
out=$(helper unit start wireguard:work 2>&1)
[ $? -eq 0 ] && [ "$(helper wg-list | grep '^work ')" = "work no yes" ]; expect $? "unit start wireguard:NAME connects that tunnel" "$out: $(helper wg-list | tr '\n' ' ')"
printf '%s\n' "x $psk1 203.0.113.9:443 0.0.0.0/0,::/0 0 0 0 25" >/dev/null
mkdir -p "$ROOT/dump"; printf 'priv\tpub\t51820\toff\n%s\t(none)\t203.0.113.9:443\t0.0.0.0/0\t1700000000\t1234\t5678\t25\n' "$peer1" >"$ROOT/dump/work"
shown=$(helper wg-show work)
echo "$shown" | grep -qx 'active=yes' && echo "$shown" | grep -qx 'handshake=1700000000' && echo "$shown" | grep -qx 'rx=1234' && echo "$shown" | grep -qx 'tx=5678'
expect $? "connected, wg-show has each peer's last handshake and traffic" "$shown"
: >"$ROOT/log"
printf '%s\n' '[interface]' 'PrivateKey=keep' 'Address=10.8.0.6/32' '[peer]' "PublicKey=$peer1" 'PresharedKey=keep' 'AllowedIPs=0.0.0.0/0' 'Endpoint=203.0.113.9:443' \
    | helper wg-save work >/dev/null 2>&1
grep -q "systemctl restart wg-quick@work.service" "$ROOT/log"; expect $? "saving a connected tunnel reconnects it with the new settings" "$(cat "$ROOT/log")"
good=$(conf work)
out=$(printf '%s\n' '[interface]' 'PrivateKey=keep' 'Address=10.8.0.7/32' 'MTU=9999' | helper wg-save work 2>&1)
[ $? -ne 0 ] && [ "$(conf work)" = "$good" ] && [ -e "$ROOT/active/wg-quick@work.service" ]
expect $? "settings it can't connect with: refused, the old ones put back and connected again" "$out"
[ "$(ls "$WG" | grep -c 'old\|\.conf\.')" = 0 ]; expect $? "(no leftover copies of the file)" "$(ls "$WG" | tr '\n' ' ')"
out=$(helper unit enable wireguard 2>&1)
[ $? -eq 0 ] && [ "$(helper wg-list | tr '\n' ',')" = "hand yes yes,home yes yes,work yes yes," ]
expect $? "unit enable wireguard: every tunnel connects, and starts with the computer" "$out: $(helper wg-list | tr '\n' ',')"
helper unit disable wireguard:home >/dev/null 2>&1
[ "$(helper wg-list | grep '^home ')" = "home no no" ] && [ "$(helper wg-list | grep '^work ')" = "work yes yes" ]
expect $? "unit disable wireguard:NAME: that one alone" "$(helper wg-list | tr '\n' ',')"
out=$(helper unit start wireguard:nothere 2>&1); [ $? -ne 0 ]; expect $? "refused: a tunnel that isn't there" "$out"
out=$(helper unit start 'wireguard:x;id' 2>&1); [ $? -ne 0 ]; expect $? "refused: a tunnel name with a command in it" "$out"

# --- removing ---------------------------------------------------------------------------
out=$(helper wg-delete work 2>&1)
[ $? -eq 0 ] && [ ! -e "$WG/work.conf" ] && [ ! -e "$ROOT/enabled/wg-quick@work.service" ] && [ ! -d "$ROOT/sys/class/net/work" ]
expect $? "wg-delete: disconnected, no longer started, its file gone" "$out"
out=$(helper wg-delete '../home' 2>&1); [ $? -ne 0 ] && [ -e "$WG/home.conf" ]; expect $? "refused: removing by a path" "$out"

echo
echo "== wayland95-services: MySQL (MariaDB)"
MYLOG=$ROOT/mariadb.log
CNF=$ROOT/etc/mysql/mariadb.conf.d/99-wayland95.cnf
out=$(printf 'listen=all\n' | helper my-config 2>&1); [ $? -ne 0 ]; expect $? "not installed: my-config refuses" "$out"
out=$(helper unit enable mysql 2>&1); [ $? -ne 0 ]; expect $? "not installed: unit enable mysql refuses" "$out"
mkdir -p "$ROOT/etc/mysql" "$ROOT/units" "$ROOT/my"
: >"$ROOT/units/mariadb.service"
out=$(helper unit enable mysql 2>&1)
[ $? -eq 0 ] && [ -e "$ROOT/enabled/mariadb.service" ] && [ -e "$ROOT/active/mariadb.service" ]
expect $? "unit enable mysql: mariadb.service started, and at boot" "$out"

out=$(printf 'listen=all\nport=3307\nmax_connections=300\n' | helper my-config 2>&1)
[ $? -eq 0 ] && grep -qx "bind-address = 0.0.0.0" "$CNF" && grep -qx "port = 3307" "$CNF" && grep -qx "max_connections = 300" "$CNF" \
    && grep -qx "\[mysqld\]" "$CNF" && grep -q "systemctl restart mariadb.service" "$ROOT/log"
expect $? "my-config: listen everywhere, port, connections (99-wayland95.cnf, after the package's), restarted" "$out $(cat "$CNF")"
printf 'listen=localhost\n' | helper my-config >/dev/null 2>&1
grep -qx "bind-address = 127.0.0.1" "$CNF"; expect $? "my-config listen=localhost: this computer only" "$(cat "$CNF")"
for bad in 'listen=everywhere' 'port=70000' 'port=33a' 'max_connections=0' 'bind-address=0.0.0.0' 'port=3306
skip-grant-tables=1'; do
    out=$(printf '%s\n' "$bad" | helper my-config 2>&1); [ $? -ne 0 ] && grep -qx "bind-address = 127.0.0.1" "$CNF"
    expect $? "my-config refuses: $(echo "$bad" | tr '\n' ' ')(and the file stays)" "$out"
done
printf 'listen=localhost\nport=3306\n' | helper my-config >/dev/null 2>&1
out=$(printf 'listen=all\nport=2\n' | helper my-config 2>&1)
[ $? -ne 0 ] && grep -qx "port = 3306" "$CNF" && [ -e "$ROOT/active/mariadb.service" ] && ! ls "$CNF".* >/dev/null 2>&1
expect $? "settings it won't start with: put back, and it's started again" "$out: $(cat "$CNF")"

: >"$MYLOG"
out=$(helper my-role my_user 2>&1)
[ $? -eq 0 ] && grep -qF "CREATE USER IF NOT EXISTS 'my_user'@'localhost' IDENTIFIED VIA unix_socket;" "$MYLOG" \
    && grep -qF 'GRANT ALL PRIVILEGES ON `my\_user`.* TO '"'my_user'@'localhost';" "$MYLOG" \
    && grep -qF 'GRANT ALL PRIVILEGES ON `my\_user\_%`.* TO '"'my_user'@'localhost';" "$MYLOG" \
    && grep -qF 'CREATE DATABASE IF NOT EXISTS `my_user`;' "$MYLOG" && ! grep -q "ON \*\.\*" "$MYLOG" \
    && grep -qF -- "-- mariadb --protocol=socket -u root" "$MYLOG"
expect $? "my-role: an account that logs on as the user (socket), with its own database and NAME_... (_ escaped)" "$out $(cat "$MYLOG")"
: >"$MYLOG"
helper my-role jo admin >/dev/null 2>&1
grep -qF "GRANT ALL PRIVILEGES ON *.* TO 'jo'@'localhost' WITH GRANT OPTION;" "$MYLOG"; expect $? "my-role NAME admin: everything" "$(cat "$MYLOG")"
for bad in "x';DROP DATABASE y;--" 'a b' '-u' "it's" 'a`b' "$(printf 'x%.0s' $(seq 40))"; do
    : >"$MYLOG"
    out=$(helper my-role "$bad" 2>&1); [ $? -ne 0 ] && [ ! -s "$MYLOG" ]
    expect $? "my-role refuses the name: $bad" "$out"
done

: >"$MYLOG"
out=$(printf 'secret\n' | helper my-password jo 2>&1); [ $? -ne 0 ] && ! grep -q "CREATE OR REPLACE" "$MYLOG"
expect $? "my-password without an account: refused" "$out"
: >"$ROOT/my/account"
pw="it's \"q\" \\ ;DROP USER x;-- é"
: >"$MYLOG"
out=$(printf '%s\n' "$pw" | helper my-password jo 2>&1); rc=$?
hex=$(printf '%s' "$pw" | od -An -v -tx1 | tr -d ' \n')
[ $rc -eq 0 ] && grep -qF "SET @pw = CONVERT(0x$hex USING utf8mb4);" "$MYLOG" && grep -qF "QUOTE(@pw)" "$MYLOG" \
    && ! grep -qF "DROP USER x" "$MYLOG" && ! grep -qF "secret" "$MYLOG" && ! grep -q "$hex" "$ROOT/log" \
    && grep -qF "TO 'jo'@'%';" "$MYLOG" && ! grep -q "ON \*\.\*" "$MYLOG"
expect $? "my-password: the network account, its password as hex quoted by the server (never in the SQL text, nor an argument)" "$out $(cat "$MYLOG")"
: >"$ROOT/my/admin"; : >"$MYLOG"
printf 'pw\n' | helper my-password jo >/dev/null 2>&1
grep -qF "GRANT ALL PRIVILEGES ON *.* TO 'jo'@'%' WITH GRANT OPTION;" "$MYLOG"; expect $? "(an administrator's network account is one too)" "$(cat "$MYLOG")"
: >"$MYLOG"
out=$(printf '' | helper my-password jo 2>&1)
[ $? -eq 0 ] && grep -qF "DROP USER IF EXISTS 'jo'@'%';" "$MYLOG"; expect $? "my-password with none: no network logons" "$out $(cat "$MYLOG")"
out=$(printf '%0201d\n' 0 | helper my-password jo 2>&1); [ $? -ne 0 ]; expect $? "my-password refuses one that's too long" "$out"

# --- the other services still work ------------------------------------------------------
out=$(helper unit start nosuch 2>&1); [ $? -ne 0 ]; expect $? "(an unknown service is still refused)" "$out"

echo
echo "== $pass passed, $fail failed"
[ "$fail" -eq 0 ]
