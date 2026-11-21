"""Publisher failure-path tests. All Git/GitHub operations are fakes, never live calls."""
import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import publish_ci as ci

SHA = "a" * 40
OLD = "b" * 40


class PublisherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.assets = tuple(root / p.name for p in ci.ASSETS)
        with zipfile.ZipFile(self.assets[0], "w") as archive:
            for name in ("gdlaunch.exe", "grimdark.dll", "prism.dll"):
                archive.writestr(f"grimdark/{name}", b"test payload")
            archive.writestr("grimdark/version.txt", f"ci-{SHA[:7]}\n")
        self.assets[1].write_bytes(b"test installer")
        self.assets[2].write_bytes(b"test symbols")
        self.expected = ci.manifest(self.assets, SHA)
        self.release = dict(id=123, tag_name=ci.TAG, target_commitish=OLD, draft=False,
                            prerelease=True, immutable=False,
                            assets=[dict(id=10, name="old.zip")])
        self.tag = OLD
        self.head = SHA
        self.events = []
        self.fail_lookup = self.fail_upload = self.fail_push = False
        self.bad_digest = self.diverged = False
        self.addCleanup(patch.stopall)
        patch.object(ci, "run", side_effect=self.fake_run).start()
        patch.object(ci, "api", side_effect=self.api).start()

    def fake_run(self, *args, **kwargs):
        if args[:3] == ("git", "rev-parse", "HEAD"):
            return SHA
        if args[:3] == ("git", "rev-parse", "FETCH_HEAD"):
            return self.head
        if args[:2] == ("git", "fetch"):
            return ""
        if args[:2] == ("git", "ls-remote"):
            return f"{self.tag}\trefs/tags/{ci.TAG}" if self.tag else ""
        if args[:2] == ("git", "merge-base"):
            if self.diverged:
                raise subprocess.CalledProcessError(1, args)
            return ""
        if args[:3] == ("gh", "release", "upload"):
            self.events.append("upload")
            self.assertTrue(self.release["draft"])
            if self.fail_upload:
                raise subprocess.CalledProcessError(1, args)
            self.release["assets"] = [dict(id=i, name=name, size=size, digest=digest, state="uploaded")
                                      for i, (name, (size, digest)) in enumerate(self.expected.items())]
            if self.bad_digest:
                self.release["assets"][0]["digest"] = "sha256:wrong"
            return ""
        if args[:2] == ("git", "push"):
            self.events.append("tag")
            self.assertIn(f"--force-with-lease=refs/tags/{ci.TAG}:{self.tag}", args)
            if self.fail_push:
                raise subprocess.CalledProcessError(1, args)
            self.tag = SHA
            return ""
        self.fail(f"Unexpected command: {args}")

    def api(self, repo, path, method="GET", body=None, paginate=False):
        if path == "releases?per_page=100":
            self.assertTrue(paginate)
            if self.fail_lookup:
                raise subprocess.CalledProcessError(1, ["gh", "api"])
            # A later page exercises discovery of an older persistent release.
            return [[], [copy.deepcopy(self.release)] if self.release else []]
        if method == "POST":
            self.assertEqual(path, "releases")
            self.events.append("create")
            self.release = dict(id=456, assets=[], **body)
        elif method == "PATCH":
            self.assertEqual(path, f"releases/{self.release['id']}")
            self.events.append("publish" if body.get("draft") is False else "draft")
            self.release.update(body)
        elif method == "DELETE":
            self.assertTrue(self.release["draft"])
            self.events.append("delete-asset")
            self.release["assets"] = []
            return None
        else:
            self.assertEqual(path, f"releases/{self.release['id']}")
        return copy.deepcopy(self.release)

    def publish(self):
        ci.publish("owner/repo", SHA, self.assets)

    def test_existing_release_retains_identity_and_complete_assets(self):
        self.publish()
        self.assertEqual(self.release["id"], 123)
        self.assertEqual(self.release["target_commitish"], SHA)
        self.assertEqual(self.release["make_latest"], "false")
        self.assertEqual(self.events, ["draft", "delete-asset", "upload", "tag", "draft", "publish"])

    def test_missing_release_created_as_draft_before_upload(self):
        self.release = None
        self.tag = ""
        self.publish()
        self.assertEqual(self.events, ["create", "upload", "tag", "draft", "publish"])

    def test_lookup_failure_never_creates_or_mutates(self):
        self.fail_lookup = True
        with self.assertRaises(subprocess.CalledProcessError):
            self.publish()
        self.assertEqual(self.events, [])

    def test_stale_main_run_does_not_mutate(self):
        self.head = OLD
        self.publish()
        self.assertEqual(self.events, [])

    def test_divergent_history_does_not_mutate(self):
        self.diverged = True
        with self.assertRaises(subprocess.CalledProcessError):
            self.publish()
        self.assertEqual(self.events, [])

    def test_unknown_target_and_immutable_release_fail_before_mutation(self):
        for changes in ({"target_commitish": "main"}, {"immutable": True}):
            with self.subTest(changes=changes):
                self.release.update(target_commitish=OLD, immutable=False)
                self.release.update(changes)
                with self.assertRaises(RuntimeError):
                    self.publish()
                self.assertEqual(self.events, [])

    def test_missing_asset_fails_before_mutation(self):
        self.assets[1].unlink()
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.events, [])

    def test_payload_version_must_match_commit(self):
        with self.assertRaises(RuntimeError):
            ci.manifest(self.assets, OLD)
        self.assertEqual(self.events, [])

    def test_upload_or_tag_failure_leaves_draft(self):
        for failure in ("fail_upload", "fail_push"):
            with self.subTest(failure=failure):
                setattr(self, failure, True)
                with self.assertRaises(subprocess.CalledProcessError):
                    self.publish()
                self.assertTrue(self.release["draft"])
                self.assertNotIn("publish", self.events)
                setattr(self, failure, False)

    def test_wrong_uploaded_digest_never_republishes(self):
        self.bad_digest = True
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertTrue(self.release["draft"])
        self.assertNotIn("publish", self.events)

    def test_interrupted_draft_can_be_repaired(self):
        self.release["draft"] = True
        self.tag = SHA  # Prior run moved the tag but did not finish metadata/publication.
        self.publish()
        self.assertFalse(self.release["draft"])
        self.assertEqual(self.release["id"], 123)

    def test_public_tag_metadata_mismatch_fails_closed(self):
        self.tag = SHA
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.events, [])


class ApiTests(unittest.TestCase):
    def test_api_propagates_failure_and_encodes_structured_body(self):
        with patch.object(ci, "run", return_value='{"id":123}') as command:
            self.assertEqual(ci.api("owner/repo", "releases/123", "PATCH", {"draft": True}), {"id": 123})
            self.assertEqual(json.loads(command.call_args.kwargs["input"]), {"draft": True})
        with patch.object(ci, "run", side_effect=subprocess.CalledProcessError(1, "gh")):
            with self.assertRaises(subprocess.CalledProcessError):
                ci.api("owner/repo", "releases?per_page=100", paginate=True)


if __name__ == "__main__":
    unittest.main()
