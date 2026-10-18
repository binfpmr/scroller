#!/usr/bin/env bash
# Serves the game to a test PC that boots over the network (UEFI PXE). Runs dnsmasq in the foreground; Ctrl+C stops.
# Every network boot fetches the current build/<preset>/scroller.efi, so rebuild and reboot the test PC.
#
#   scripts/netboot-server.sh proxy  <interface>   Test PC wired to the same LAN (router) as this PC (which may be on
#                                                  Wi-Fi). The router keeps handing out addresses; dnsmasq only
#                                                  answers network-boot requests (proxy DHCP) and serves the file.
#   scripts/netboot-server.sh direct <interface>   Cable straight from this PC (e.g. a USB Ethernet adapter) to the
#                                                  test PC. This PC takes 10.42.0.1/24 on <interface> (taken away
#                                                  from NetworkManager while running) and hands out addresses itself.
#   --dry-run (last argument)                      Only check the dnsmasq configuration and print the command.
#
# Environment: PRESET (default uefi). Needs sudo (DHCP and TFTP ports). A firewall must allow UDP 67, 69 and 4011
# in on <interface>. On the test PC: Secure Boot off, network (PXE IPv4) boot first or picked from the boot menu.
set -euo pipefail

usage() {
  sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
  exit 2
}

mode=${1:-}
iface=${2:-}
dry_run=0
[[ ${3:-} == --dry-run ]] && dry_run=1
[[ $mode == proxy || $mode == direct ]] && [[ -n $iface ]] || usage
[[ -e /sys/class/net/$iface ]] || { echo "error: no network interface '$iface' (see: ip -br link)" >&2; exit 1; }

root="$(cd "$(dirname "$0")/.." && pwd)/build/${PRESET:-uefi}"
[[ -f $root/scroller.efi ]] || { echo "error: $root/scroller.efi not built" >&2; exit 1; }

args=(
  --no-daemon --conf-file=/dev/null --port=0  # no DNS, no system configuration
  --interface="$iface" --bind-interfaces
  --enable-tftp --tftp-root="$root"
  --user="$(id -un)" --pid-file= --dhcp-leasefile="${TMPDIR:-/tmp}/scroller-netboot.leases"
  --log-dhcp
)

if [[ $mode == proxy ]]; then
  network=$(ip -4 route show dev "$iface" proto kernel scope link | awk 'NR == 1 { print $1 }')
  [[ -n $network ]] || { echo "error: $iface has no IPv4 network; is it connected?" >&2; exit 1; }
  args+=(
    --dhcp-range="${network%/*},proxy"
    # UEFI x64 firmware announces itself as either of these client types; the file name is used as given.
    --pxe-service=x86-64_EFI,"scroller",scroller.efi
    --pxe-service=BC_EFI,"scroller",scroller.efi
  )
  echo "proxy DHCP on $iface ($network), serving $root/scroller.efi"
else
  args+=(--dhcp-range=10.42.0.50,10.42.0.150,255.255.255.0,1h --dhcp-boot=scroller.efi)
  echo "DHCP server 10.42.0.1 on $iface, serving $root/scroller.efi"
fi

if [[ $dry_run -eq 1 ]]; then
  dnsmasq --test "${args[@]}"
  echo "sudo dnsmasq ${args[*]}"
  exit 0
fi

if [[ $mode == direct ]]; then
  restore() {
    sudo ip addr flush dev "$iface" || true
    sudo nmcli device set "$iface" managed yes 2>/dev/null || true
  }
  trap restore EXIT
  sudo nmcli device set "$iface" managed no 2>/dev/null || true
  sudo ip addr flush dev "$iface"
  sudo ip addr add 10.42.0.1/24 dev "$iface"
  sudo ip link set "$iface" up
fi

sudo dnsmasq "${args[@]}"
