"""Refresh ci-latest in place, using Factorio Access's draft/swap/publish sequence.

Run only in the serialized main push job, after both builds succeed. No release is
deleted. Failure during the swap deliberately leaves a draft for a later main push
to repair; never expose partial assets in a finally/cleanup handler. Reusing the
release avoids creating a new release per build, but email silence is not guaranteed.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import zipfile

TAG = "ci-latest"
ASSETS = (
    Path("dist/grimdark/grimdark.zip"),
    Path("dist/GrimdarkInstaller/GrimdarkInstaller.exe"),
    Path("dist/grimdark-pdb/grimdark.pdb"),
)


def run(*args, input=None):
    return subprocess.run(args, input=input, text=True, check=True, stdout=subprocess.PIPE).stdout.strip()


def api(repo, path, method="GET", body=None, paginate=False):
    args = ["gh", "api", f"repos/{repo}/{path}", "--method", method]
    if paginate:
        args += ["--paginate", "--slurp"]
    if body is not None:
        args += ["--input", "-"]
    result = run(*args, input=json.dumps(body) if body is not None else None)
    return json.loads(result) if result else None


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def manifest(assets, sha):
    result = {}
    for path in assets:
        require(path.is_file() and path.stat().st_size > 0, f"Missing or empty asset: {path}")
        result[path.name] = (path.stat().st_size, "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest())
    with zipfile.ZipFile(assets[0]) as archive:
        require(archive.testzip() is None, "Corrupt grimdark.zip")
        for name in ("gdlaunch.exe", "grimdark.dll", "prism.dll", "version.txt"):
            require(archive.getinfo(f"grimdark/{name}").file_size > 0, f"Missing payload: {name}")
        version = archive.read("grimdark/version.txt").decode().strip()
        require(re.fullmatch(r"ci-[0-9a-f]{7,40}", version) and sha.startswith(version[3:]),
                "Payload version does not match this commit")
    return result


def remote_tag():
    output = run("git", "ls-remote", "--refs", "origin", f"refs/tags/{TAG}")
    return output.split()[0] if output else ""


def verify(release, release_id, sha, expected, draft):
    require(release["id"] == release_id and release["tag_name"] == TAG, "Release identity changed")
    require(release["draft"] == draft and release["prerelease"], "Wrong release channel/state")
    require(release["target_commitish"] == sha, "Release target does not match build")
    assets = release["assets"]
    require(len(assets) == len(expected), "Wrong asset count")
    actual = {a["name"]: (a["size"], a.get("digest")) for a in assets if a["state"] == "uploaded"}
    require(actual == expected, "Uploaded asset names, sizes or SHA-256 digests do not match")
    require(remote_tag() == sha, "Rolling tag does not match build")


def publish(repo, sha, assets=ASSETS):
    require(re.fullmatch(r"[0-9a-f]{40}", sha), "Expected a full commit SHA")
    require(run("git", "rev-parse", "HEAD") == sha, "Checkout does not match build")
    expected = manifest(assets, sha)  # Validate every input before touching the release.
    run("git", "fetch", "--no-tags", "origin", "main")
    if run("git", "rev-parse", "FETCH_HEAD") != sha:
        print("Skipping stale run: main has moved since this build.")
        return

    # A successful, fully paginated list distinguishes absence from all lookup/auth failures.
    # Unlike a public tag lookup, it also finds drafts left by an interrupted publication.
    pages = api(repo, "releases?per_page=100", paginate=True)
    matches = [r for page in pages for r in page if r["tag_name"] == TAG]
    require(len(matches) <= 1, "Ambiguous rolling release")
    release = matches[0] if matches else None
    old_tag = remote_tag()
    if release:
        require(not release.get("immutable", False), "Rolling release must be mutable")
        previous = release["target_commitish"]
        require(re.fullmatch(r"[0-9a-f]{40}", previous), "Unknown rolling target; refusing to overwrite")
        # Unknown or divergent history fails closed; old reruns cannot roll back a newer build.
        run("git", "merge-base", "--is-ancestor", previous, sha)
        require(release["draft"] or old_tag == previous, "Published tag and target disagree")

    notes = (f"Automatic build of main at {sha[:7]}. Untested; the installer lists it last. "
             "Tagged releases are the ones to use.")
    metadata = dict(tag_name=TAG, target_commitish=sha, name="Latest CI build", body=notes,
                    draft=True, prerelease=True, make_latest="false")
    if release is None:
        release = api(repo, "releases", "POST", metadata)
        # Creation can create the tag; capture it before the compare-and-swap push.
        old_tag = remote_tag()
    else:
        # Hide before changing assets or build identity. Do not delete/recreate the release.
        release = api(repo, f"releases/{release['id']}", "PATCH", {"draft": True})
    release_id = release["id"]
    endpoint = f"releases/{release_id}"
    for asset in release["assets"]:
        api(repo, f"releases/assets/{asset['id']}", "DELETE")
    run("gh", "release", "upload", TAG, *map(str, assets), "--repo", repo)
    # Do not overwrite an unexpected concurrent tag edit, even by a human publisher.
    run("git", "push", f"--force-with-lease=refs/tags/{TAG}:{old_tag}",
        "origin", f"{sha}:refs/tags/{TAG}")
    api(repo, endpoint, "PATCH", metadata)
    verify(api(repo, endpoint), release_id, sha, expected, draft=True)
    api(repo, endpoint, "PATCH", {"draft": False, "prerelease": True, "make_latest": "false"})
    verify(api(repo, endpoint), release_id, sha, expected, draft=False)
    print(f"Published {TAG} at {sha}, retaining release ID {release_id}.")


if __name__ == "__main__":
    require(os.environ.get("GITHUB_EVENT_NAME") == "push" and
            os.environ.get("GITHUB_REF") == "refs/heads/main", "Only main push builds may publish rolling CI")
    publish(os.environ["GITHUB_REPOSITORY"], os.environ["GITHUB_SHA"])
