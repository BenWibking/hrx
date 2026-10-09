#!/usr/bin/env python3
"""Provision an AMD MI300X GPU droplet on DigitalOcean.

Uses only the Python standard library. Reads the API token from the
DIGITALOCEAN_TOKEN environment variable.

API reference: https://docs.digitalocean.com/reference/api/reference/droplets/

Examples:
    export DIGITALOCEAN_TOKEN=dop_v1_...
    ./provision_mi300x.py --list-regions
    ./provision_mi300x.py --name mi300x-dev --region atl1
    ./provision_mi300x.py --name mi300x-8 --size gpu-mi300x8-1536gb --ssh-key my-laptop
    ./provision_mi300x.py --name test --dry-run
    ./provision_mi300x.py --list
    ./provision_mi300x.py --delete mi300x-dev
"""

import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

API_BASE = "https://api.digitalocean.com/v2"
DEFAULT_SIZE = "gpu-mi300x1-192gb"  # 1x MI300X; 8x is gpu-mi300x8-1536gb
DEFAULT_IMAGE = "gpu-amd-base"  # Ubuntu + ROCm, AI/ML-ready AMD GPU image


class DOError(RuntimeError):
    pass


class DOClient:
    def __init__(self, token, max_retries=5):
        self.token = token
        self.max_retries = max_retries

    def request(self, method, path, body=None, params=None):
        url = API_BASE + path
        if params:
            url += "?" + urllib.parse.urlencode(params)
        data = json.dumps(body).encode() if body is not None else None
        headers = {
            "Authorization": f"Bearer {self.token}",
            "Content-Type": "application/json",
        }
        for attempt in range(self.max_retries):
            req = urllib.request.Request(url, data=data, headers=headers, method=method)
            try:
                with urllib.request.urlopen(req, timeout=60) as resp:
                    raw = resp.read()
                    return json.loads(raw) if raw else {}
            except urllib.error.HTTPError as e:
                raw = e.read().decode(errors="replace")
                # Retry on rate limiting and transient server errors.
                if e.code in (429, 500, 502, 503, 504) and attempt + 1 < self.max_retries:
                    reset = e.headers.get("ratelimit-reset")
                    delay = 2 ** attempt
                    if e.code == 429 and reset and reset.isdigit():
                        delay = max(1, int(reset) - int(time.time()))
                    print(f"HTTP {e.code}; retrying in {delay}s...", file=sys.stderr)
                    time.sleep(min(delay, 60))
                    continue
                try:
                    msg = json.loads(raw).get("message", raw)
                except json.JSONDecodeError:
                    msg = raw
                raise DOError(f"{method} {path} failed: HTTP {e.code}: {msg}") from None
            except urllib.error.URLError as e:
                if attempt + 1 < self.max_retries:
                    time.sleep(2 ** attempt)
                    continue
                raise DOError(f"{method} {path} failed: {e.reason}") from None
        raise DOError(f"{method} {path} failed after {self.max_retries} attempts")

    def paginate(self, path, key, params=None):
        params = dict(params or {}, per_page=200, page=1)
        while True:
            resp = self.request("GET", path, params=params)
            yield from resp.get(key, [])
            if not resp.get("links", {}).get("pages", {}).get("next"):
                return
            params["page"] += 1


def get_size(client, slug):
    for size in client.paginate("/sizes", "sizes"):
        if size["slug"] == slug:
            return size
    raise DOError(
        f"Size {slug!r} not found. Available MI300X sizes: "
        + (", ".join(list_mi300x_sizes(client)) or "(none)")
    )


def list_mi300x_sizes(client):
    return [s["slug"] for s in client.paginate("/sizes", "sizes") if "mi300x" in s["slug"]]


def resolve_ssh_keys(client, requested):
    """Map names/IDs/fingerprints to key IDs. Empty request -> all account keys."""
    keys = list(client.paginate("/account/keys", "ssh_keys"))
    if not requested:
        return [k["id"] for k in keys]
    resolved = []
    for want in requested:
        match = next(
            (k for k in keys if want in (str(k["id"]), k["fingerprint"], k["name"])), None
        )
        if match is None:
            raise DOError(f"SSH key {want!r} not found in account")
        resolved.append(match["id"])
    return resolved


def public_ipv4(droplet):
    for net in droplet.get("networks", {}).get("v4", []):
        if net.get("type") == "public":
            return net["ip_address"]
    return None


def wait_for_active(client, droplet_id, timeout, interval=10):
    deadline = time.monotonic() + timeout
    while True:
        droplet = client.request("GET", f"/droplets/{droplet_id}")["droplet"]
        status = droplet["status"]
        ip = public_ipv4(droplet)
        print(f"  status={status} ip={ip or '-'}", file=sys.stderr)
        if status == "active" and ip:
            return droplet
        if time.monotonic() > deadline:
            raise DOError(f"Timed out waiting for droplet {droplet_id} (last status: {status})")
        time.sleep(interval)


def list_droplets(client):
    """All droplets in the account. GPU droplets are excluded from the default
    listing, so fetch them separately with type=gpus and merge."""
    droplets = {d["id"]: d for d in client.paginate("/droplets", "droplets")}
    for d in client.paginate("/droplets", "droplets", params={"type": "gpus"}):
        droplets[d["id"]] = d
    return sorted(droplets.values(), key=lambda d: d["created_at"])


def print_droplets(droplets):
    if not droplets:
        print("No droplets.")
        return
    print(f"{'ID':<12} {'NAME':<24} {'STATUS':<8} {'REGION':<7} {'SIZE':<24} {'IP':<16} {'$/HR':>6}  CREATED")
    for d in droplets:
        print(f"{d['id']:<12} {d['name']:<24} {d['status']:<8} {d['region']['slug']:<7} "
              f"{d['size_slug']:<24} {public_ipv4(d) or '-':<16} {d['size']['price_hourly']:>6.2f}  "
              f"{d['created_at']}")


def delete_droplet(client, target, assume_yes):
    """Delete one droplet, identified by ID or exact name."""
    droplets = list_droplets(client)
    matches = [d for d in droplets if target in (str(d["id"]), d["name"])]
    if not matches:
        raise DOError(f"No droplet with ID or name {target!r}")
    if len(matches) > 1:
        ids = ", ".join(str(d["id"]) for d in matches)
        raise DOError(f"Name {target!r} matches several droplets ({ids}); delete by ID instead")
    d = matches[0]
    print_droplets([d])
    if not assume_yes:
        if not sys.stdin.isatty():
            raise DOError("Refusing to delete without confirmation; pass --yes")
        answer = input(f"Permanently delete droplet {d['name']} ({d['id']})? Type its name to confirm: ")
        if answer.strip() != d["name"]:
            print("Aborted.", file=sys.stderr)
            return
    client.request("DELETE", f"/droplets/{d['id']}")
    print(f"Deleted droplet {d['name']} ({d['id']}).", file=sys.stderr)


def parse_args():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--name", help="Droplet name (also its hostname)")
    p.add_argument("--size", default=DEFAULT_SIZE, help=f"Size slug (default: {DEFAULT_SIZE})")
    p.add_argument("--region", help="Region slug; default: first region where the size is available")
    p.add_argument("--image", default=DEFAULT_IMAGE, help=f"Image slug or ID (default: {DEFAULT_IMAGE})")
    p.add_argument("--ssh-key", action="append", default=[],
                   help="SSH key name, ID, or fingerprint (repeatable; default: all account keys)")
    p.add_argument("--tag", action="append", default=[], help="Tag (repeatable)")
    p.add_argument("--user-data", type=argparse.FileType("r"), help="cloud-init / shell script file")
    p.add_argument("--vpc-uuid", help="VPC UUID (default: region's default VPC)")
    p.add_argument("--no-monitoring", action="store_true", help="Don't install the monitoring agent")
    p.add_argument("--ipv6", action="store_true", help="Enable IPv6")
    p.add_argument("--no-wait", action="store_true", help="Return immediately after the create request")
    p.add_argument("--timeout", type=int, default=900, help="Seconds to wait for active (default: 900)")
    p.add_argument("--dry-run", action="store_true", help="Print the request body without creating anything")
    p.add_argument("--list-regions", action="store_true", help="List MI300X sizes, regions, and prices, then exit")
    p.add_argument("--list", action="store_true", help="List all droplets in the account, then exit")
    p.add_argument("--delete", metavar="ID_OR_NAME", help="Delete one droplet by ID or exact name, then exit")
    p.add_argument("--yes", action="store_true", help="Skip the confirmation prompt for --delete")
    args = p.parse_args()
    if sum(map(bool, (args.list_regions, args.list, args.delete))) > 1:
        p.error("--list-regions, --list, and --delete are mutually exclusive")
    if not (args.list_regions or args.list or args.delete) and not args.name:
        p.error("--name is required")
    return args


def main():
    args = parse_args()
    token = os.environ.get("DIGITALOCEAN_TOKEN")
    if not token:
        sys.exit("error: set DIGITALOCEAN_TOKEN to a DigitalOcean API token")
    client = DOClient(token)

    if args.list_regions:
        for s in client.paginate("/sizes", "sizes"):
            if "mi300x" in s["slug"]:
                print(f"{s['slug']:<32} ${s['price_hourly']:.2f}/hr  "
                      f"available={s['available']}  regions={','.join(s['regions']) or '-'}")
        return
    if args.list:
        print_droplets(list_droplets(client))
        return
    if args.delete:
        delete_droplet(client, args.delete, args.yes)
        return

    size = get_size(client, args.size)
    if not size.get("available") or not size.get("regions"):
        raise DOError(f"Size {args.size!r} is not currently available in any region")
    region = args.region or size["regions"][0]
    if region not in size["regions"]:
        raise DOError(f"Size {args.size!r} is not offered in {region!r}; "
                      f"available regions: {', '.join(size['regions'])}")

    ssh_keys = resolve_ssh_keys(client, args.ssh_key)
    if not ssh_keys:
        print("warning: no SSH keys in account; root password will be emailed", file=sys.stderr)

    body = {
        "name": args.name,
        "region": region,
        "size": args.size,
        "image": args.image,
        "ssh_keys": ssh_keys,
        "monitoring": not args.no_monitoring,
        "ipv6": args.ipv6,
        "tags": args.tag,
    }
    if args.user_data:
        body["user_data"] = args.user_data.read()
    if args.vpc_uuid:
        body["vpc_uuid"] = args.vpc_uuid

    print(f"Size {args.size}: ${size['price_hourly']:.2f}/hr in {region}", file=sys.stderr)
    if args.dry_run:
        print(json.dumps(body, indent=2))
        return

    droplet = client.request("POST", "/droplets", body=body)["droplet"]
    print(f"Created droplet {droplet['id']} ({droplet['name']}); billing has started.", file=sys.stderr)
    if args.no_wait:
        print(droplet["id"])
        return

    print("Waiting for droplet to become active...", file=sys.stderr)
    droplet = wait_for_active(client, droplet["id"], args.timeout)
    ip = public_ipv4(droplet)
    print(json.dumps({"id": droplet["id"], "name": droplet["name"], "region": region,
                      "size": args.size, "ip": ip}, indent=2))
    print(f"\nConnect with: ssh root@{ip}", file=sys.stderr)


if __name__ == "__main__":
    try:
        main()
    except DOError as e:
        sys.exit(f"error: {e}")
