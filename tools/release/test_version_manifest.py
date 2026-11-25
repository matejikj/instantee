"""Behavioral tests for the release version manifest contract."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import check_version_manifest as check


REPO_ROOT = Path(__file__).resolve().parents[2]


class VersionManifestTests(unittest.TestCase):
    def test_repo_manifest_passes(self) -> None:
        errors = check.validate_manifest(
            repo_root=REPO_ROOT,
            manifest_path=REPO_ROOT
            / "contracts"
            / "release"
            / "version-manifest-v1.json",
        )
        self.assertEqual(errors, [])

    def test_axes_are_separate_keys(self) -> None:
        manifest = json.loads(
            (
                REPO_ROOT
                / "contracts"
                / "release"
                / "version-manifest-v1.json"
            ).read_text(encoding="utf-8")
        )
        axes = manifest["axes"]
        self.assertIn("framework_semver", axes)
        self.assertIn("scene_ir_schema_version", axes)
        self.assertIn("style_package_version", axes)
        self.assertIsInstance(axes["style_package_version"]["packages"], list)

    def test_changelog_requires_breaking_changes(self) -> None:
        changelog = (REPO_ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        self.assertIn("## [1.0.0]", changelog)
        self.assertIn("### Breaking Changes", changelog)
        self.assertIn("schema_version 1.0.0", changelog)
        self.assertIn("migrate_authoring_json", changelog)

    def test_detects_framework_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            # Minimal fake tree.
            (root / "contracts" / "release").mkdir(parents=True)
            (root / "contracts" / "compatibility").mkdir(parents=True)
            (root / "contracts" / "fixtures" / "style-bible" / "positive").mkdir(
                parents=True
            )
            (root / "contracts" / "fixtures" / "authoring" / "migrations").mkdir(
                parents=True
            )
            (root / "authoring" / "packages" / "authoring").mkdir(parents=True)

            matrix = {
                "schema_version": "1.0.0",
                "families": {
                    "authoring": {
                        "current": "1.0.0",
                        "supported": [
                            {"version": "1.0.0", "status": "current"},
                            {
                                "version": "0.9.0",
                                "status": "migratable",
                                "migrator": "migrate_authoring_json",
                                "fixture": "fixtures/authoring/migrations/from-0.9.0",
                            },
                        ],
                        "rejected_examples": ["0.8.0", "1.1.0", "2.0.0"],
                    },
                    "scene-ir": {
                        "current": "1.0.0",
                        "supported": [{"version": "1.0.0", "status": "current"}],
                        "rejected_examples": ["0.9.0", "1.1.0", "2.0.0"],
                    },
                    "style-bible": {
                        "current": "1.0.0",
                        "supported": [{"version": "1.0.0", "status": "current"}],
                        "rejected_examples": ["0.9.0", "1.1.0", "2.0.0"],
                    },
                },
            }
            (
                root / "contracts" / "compatibility" / "schema-version-matrix-v1.json"
            ).write_text(json.dumps(matrix), encoding="utf-8")
            (
                root / "contracts" / "fixtures" / "authoring" / "migrations" / "from-0.9.0"
            ).mkdir()

            for pkg_id in ("editorial", "scientific-notebook", "paper-cinema"):
                path = (
                    root
                    / "contracts"
                    / "fixtures"
                    / "style-bible"
                    / "positive"
                    / f"{pkg_id}.style.json"
                )
                path.write_text(
                    json.dumps(
                        {
                            "schema_version": "1.0.0",
                            "package_id": pkg_id,
                            "version": "1.0.0",
                        }
                    ),
                    encoding="utf-8",
                )

            (root / "CMakeLists.txt").write_text(
                "project(Instantee VERSION 9.9.9 LANGUAGES CXX)\n",
                encoding="utf-8",
            )
            (
                root / "authoring" / "packages" / "authoring" / "package.json"
            ).write_text(
                json.dumps({"name": "@instantee/authoring", "version": "1.0.0"}),
                encoding="utf-8",
            )
            (root / "CHANGELOG.md").write_text(
                "## [1.0.0]\n\n### Breaking Changes\n\nx\n\n### Added\n\ny\n\n"
                "### Changed\n\nz\n",
                encoding="utf-8",
            )

            manifest = json.loads(
                (
                    REPO_ROOT
                    / "contracts"
                    / "release"
                    / "version-manifest-v1.json"
                ).read_text(encoding="utf-8")
            )
            (root / "contracts" / "release" / "version-manifest-v1.json").write_text(
                json.dumps(manifest),
                encoding="utf-8",
            )

            errors = check.validate_manifest(
                repo_root=root,
                manifest_path=root
                / "contracts"
                / "release"
                / "version-manifest-v1.json",
            )
            self.assertTrue(
                any("CMakeLists.txt" in error and "9.9.9" in error for error in errors),
                errors,
            )


if __name__ == "__main__":
    unittest.main()
