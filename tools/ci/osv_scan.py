# SPDX-License-Identifier: MIT
"""Dependency-vulnerability scan over the vcpkg SBOM.

vcpkg writes share/<port>/vcpkg.spdx.json for every installed port. Its source packages name the upstream
repository and ref (`git+https://github.com/owner/repo@<tag or commit>`). OSV has no vcpkg ecosystem, so each ref
is resolved to a commit (`git ls-remote`, annotated tags peeled) and OSV is queried by commit, which matches the
GIT ranges of C/C++ advisories (what osv-scanner does for git sources). Ports with no upstream repository
(vcpkg helper ports, binary tool downloads) are listed as skipped.

Every advisory found must be waived in osv_waivers.json (id, port and reason) or the scan fails. A waiver that no
longer matches anything is reported so the list stays honest. Network or resolution failures fail the scan.

Usage: osv_scan.py --installed-dir build/<preset>/vcpkg_installed/<triplet> [--installed-dir ...]
"""
import argparse
import json
import re
import subprocess
import sys
import time
import urllib.request
from dataclasses import dataclass
from pathlib import Path

OSV_API = "https://api.osv.dev/v1"
DEFAULT_WAIVERS = Path(__file__).resolve().parent / "osv_waivers.json"
GIT_SOURCE = re.compile(r"^git\+(https://[^@]+)@(.+)$")
COMMIT = re.compile(r"^[0-9a-f]{40}$")


@dataclass(frozen=True)
class Source:
    port: str
    version: str
    repo: str  # https URL
    ref: str  # tag, branch or commit as vcpkg recorded it


def read_sources(installed_dir: Path):
    """(sources, skipped port names) from one vcpkg_installed/<triplet> tree."""
    sources, skipped = [], []
    for spdx_path in sorted(installed_dir.glob("share/*/vcpkg.spdx.json")):
        doc = json.loads(spdx_path.read_text(encoding="utf-8"))
        packages = doc.get("packages", [])
        if not packages:
            continue
        port, version = packages[0].get("name", spdx_path.parent.name), packages[0].get("versionInfo", "")
        found = False
        for package in packages[1:]:
            match = GIT_SOURCE.match(package.get("downloadLocation") or "")
            if match:
                sources.append(Source(port, version, match.group(1), match.group(2)))
                found = True
        if not found:
            skipped.append(port)
    return sources, skipped


def resolve_commit(source: Source, ls_remote=None) -> str:
    """The commit `source.ref` names. Raises RuntimeError when the ref doesn't resolve."""
    if COMMIT.match(source.ref):
        return source.ref
    ls_remote = ls_remote or _git_ls_remote
    refs = ls_remote(source.repo, [f"refs/tags/{source.ref}", f"refs/tags/{source.ref}^{{}}", f"refs/heads/{source.ref}"])
    # A peeled annotated tag (`^{}`) names the commit; a lightweight tag or branch names it directly.
    for name in (f"refs/tags/{source.ref}^{{}}", f"refs/tags/{source.ref}", f"refs/heads/{source.ref}"):
        if name in refs:
            return refs[name]
    raise RuntimeError(f"{source.port}: {source.repo}@{source.ref} does not resolve to a commit")


def _git_ls_remote(repo: str, patterns):
    for attempt in range(3):
        try:
            output = subprocess.run(["git", "ls-remote", repo, *patterns], check=True, capture_output=True,
                                    text=True, timeout=120).stdout
            break
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
            if attempt == 2:
                raise
            time.sleep(2 * (attempt + 1))
    refs = {}
    for line in output.splitlines():
        sha, _, name = line.partition("\t")
        refs[name] = sha
    return refs


def _post(url: str, payload):
    data = json.dumps(payload).encode()
    for attempt in range(3):
        try:
            request = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json"})
            with urllib.request.urlopen(request, timeout=60) as response:
                return json.load(response)
        except OSError:
            if attempt == 2:
                raise
            time.sleep(2 * (attempt + 1))
    raise AssertionError("unreachable")


def query_osv(commits, post=None):
    """{commit: [advisory id, ...]} for each commit, following querybatch pagination."""
    post = post or _post
    results = {commit: [] for commit in commits}
    pending = [(commit, None) for commit in commits]
    while pending:
        queries = []
        for commit, token in pending:
            query = {"commit": commit}
            if token:
                query["page_token"] = token
            queries.append(query)
        response = post(f"{OSV_API}/querybatch", {"queries": queries})
        next_pending = []
        for (commit, _), result in zip(pending, response.get("results", [])):
            results[commit].extend(vuln["id"] for vuln in result.get("vulns", []) or [])
            if result.get("next_page_token"):
                next_pending.append((commit, result["next_page_token"]))
        pending = next_pending
    return results


def load_waivers(path: Path):
    """{advisory id: waiver}; each waiver names its port and a reason."""
    waivers = {}
    for waiver in json.loads(path.read_text(encoding="utf-8")).get("waivers", []):
        if not waiver.get("id") or not waiver.get("port") or not waiver.get("reason"):
            raise ValueError(f"{path}: every waiver needs id, port and reason: {waiver}")
        waivers[waiver["id"]] = waiver
    return waivers


def evaluate(findings, waivers):
    """(unwaived [(port, id)], waived [(port, id, reason)], stale waiver ids) from [(port, id)] findings."""
    unwaived, waived, used = [], [], set()
    for port, vuln_id in findings:
        waiver = waivers.get(vuln_id)
        if waiver and waiver["port"] == port:
            waived.append((port, vuln_id, waiver["reason"]))
            used.add(vuln_id)
        else:
            unwaived.append((port, vuln_id))
    stale = sorted(set(waivers) - used)
    return unwaived, waived, stale


def scan(installed_dirs, waivers, ls_remote=None, post=None, out=sys.stdout):
    """Returns the process exit code: 0 when every advisory is waived."""
    sources, skipped = {}, set()
    for installed_dir in installed_dirs:
        found, skip = read_sources(installed_dir)
        for source in found:
            sources[(source.port, source.repo, source.ref)] = source
        skipped.update(skip)
    skipped -= {source.port for source in sources.values()}
    if not sources:
        print("error: no vcpkg SBOM with an upstream source found", file=out)
        return 1

    commits = {key: resolve_commit(source, ls_remote) for key, source in sorted(sources.items())}
    vulns = query_osv(sorted(set(commits.values())), post)
    findings = sorted({(key[0], vuln_id) for key, commit in commits.items() for vuln_id in vulns[commit]})
    unwaived, waived, stale = evaluate(findings, waivers)

    print(f"Scanned {len(sources)} upstream sources:", file=out)
    for key, source in sorted(sources.items()):
        print(f"  {source.port} {source.version}: {source.repo}@{source.ref} ({commits[key][:12]})", file=out)
    if skipped:
        print(f"Skipped (no upstream repository in the SBOM): {', '.join(sorted(skipped))}", file=out)
    for port, vuln_id, reason in waived:
        print(f"waived  {port}: {vuln_id}: {reason}", file=out)
    for vuln_id in stale:
        print(f"note: waiver {vuln_id} matches no current advisory; remove it", file=out)
    for port, vuln_id in unwaived:
        print(f"error: {port}: {vuln_id} (https://osv.dev/vulnerability/{vuln_id})", file=out)
    if unwaived:
        print(f"{len(unwaived)} unwaived advisories: update the port or add a reviewed waiver to osv_waivers.json",
              file=out)
        return 1
    print("No unwaived advisories.", file=out)
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--installed-dir", type=Path, action="append", required=True)
    parser.add_argument("--waivers", type=Path, default=DEFAULT_WAIVERS)
    args = parser.parse_args(argv)
    return scan(args.installed_dir, load_waivers(args.waivers))


if __name__ == "__main__":
    sys.exit(main())
