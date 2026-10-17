#!/usr/bin/env python3
"""Validate actual exported content, then compare native orbit rules with Godot's outputs."""
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest

sys.dont_write_bytecode = True
PROJECT = Path(__file__).resolve().parents[1]
SNAPSHOT = PROJECT / "Migration"


class MigrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.world = json.loads((PROJECT / "Content/Data/world.json").read_text())
        cls.story = json.loads((PROJECT / "Content/Data/story.json").read_text())
        cls.groups = {g["id"]: g for g in cls.world["groups"]}
        cls.meshes = {m["id"]: m for m in cls.world["meshes"]}

    def test_all_worlds_and_islands_survive(self):
        expected = {"aubade","dawnhollow","hush","slow_deep","chorus","kernel","seam","drift"}
        expected.update("chorus_island_"+str(i) for i in range(7))
        self.assertEqual(set(self.world["bodies"]), expected)
        self.assertFalse(self.world["failures"])

    def test_every_exported_reference_resolves(self):
        for group in self.groups.values():
            if group["parent"]:
                self.assertIn(group["parent"], self.groups)
        for category in ("placements", "colliders", "interactions", "lights"):
            for item in self.world[category]:
                self.assertIn(item["group"], self.groups)
                if "mesh" in item:
                    self.assertIn(item["mesh"], self.meshes)
        for mesh in self.meshes.values():
            path = SNAPSHOT / mesh["file"]
            self.assertTrue(path.is_file(), str(path))
            with path.open("rb") as stream:
                magic, version, length = struct.unpack("<4sII", stream.read(12))
            self.assertEqual(magic,b"glTF")
            self.assertEqual(version,2)
            self.assertEqual(length,path.stat().st_size)

    def test_transform_values_are_finite_and_non_degenerate(self):
        def inspect(value):
            if isinstance(value, dict):
                if set(("p","x","y","z")).issubset(value):
                    x,y,z = value["x"],value["y"],value["z"]
                    determinant = x[0]*(y[1]*z[2]-y[2]*z[1])-y[0]*(x[1]*z[2]-x[2]*z[1])+z[0]*(x[1]*y[2]-x[2]*y[1])
                    self.assertGreater(abs(determinant),1e-12)
                for child in value.values(): inspect(child)
            elif isinstance(value,list):
                for child in value: inspect(child)
            elif isinstance(value,(int,float)):
                self.assertTrue(math.isfinite(value))
        inspect(self.world)

    def test_story_links_and_interactions(self):
        for tree in self.story["dialogue"].values():
            for entry in tree["entry"]:
                self.assertIn(entry["goto"],tree["nodes"])
            for node in tree["nodes"].values():
                for choice in node.get("choices",[]):
                    self.assertIn(choice["goto"],tree["nodes"])
                for log in node.get("log",[]):
                    self.assertIn(log,self.story["log"])
        for text in self.story["texts"].values():
            for log in text.get("log",[]):
                self.assertIn(log,self.story["log"])
        for group in self.groups.values():
            if group["kind"] in ("npc","quantum"):
                self.assertIn(group["data"]["id"],self.story["dialogue"])
            if group["kind"] == "text":
                self.assertIn(group["data"]["id"],self.story["texts"])
        self.assertEqual(len(self.story["log"]),65)
        self.assertEqual(len(self.story["texts"]),29)
        self.assertEqual(len(self.story["dialogue"]),28)

    def test_scene_contains_terrain_collision_and_dense_scenery(self):
        self.assertGreater(len(self.world["colliders"]),400)
        self.assertGreater(sum(len(p.get("instances",[None])) for p in self.world["placements"]),24000)
        self.assertGreaterEqual(sum("terrain" in m["kinds"] for m in self.meshes.values()),7)
        self.assertGreater(sum("sun" in m["kinds"] for m in self.meshes.values()),0)
        self.assertGreater(sum("flame_stylized" in m["kinds"] for m in self.meshes.values()),0)
        self.assertTrue((SNAPSHOT/"axis_calibration.glb").exists())

    def test_native_math_and_orbits_match_godot(self):
        compiler = shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler,"A C++17 compiler is required")
        temp = PROJECT/"Saved/Tests"
        temp.mkdir(parents=True,exist_ok=True)
        binary = temp/"simulation_test"
        env = dict(os.environ,TMPDIR=str(temp),CLANG_MODULE_CACHE_PATH=str(temp/"Clang"))
        subprocess.run([compiler,"-std=c++17","-Wall","-Wextra","-Werror",
            "-I"+str(PROJECT/"Source/Ephemera/Public"),str(PROJECT/"Tests/simulation_test.cpp"),
            "-o",str(binary)],check=True,env=env)
        subprocess.run([str(binary)],check=True,env=env)
        references = json.loads((SNAPSHOT/"reference.json").read_text())
        body_data = {g["data"]["body_id"]:g["data"] for g in self.groups.values() if g["kind"]=="body"}
        lookup = {(b["body_id"],p["time"]):p["position"] for b in references for p in b["positions"]}
        commands, expected = [], []
        for body in references:
            data = body_data[body["body_id"]]
            if data["custom_transform"]:
                continue
            for point in body["positions"]:
                fields = ["orbit_radius","orbit_period","orbit_phase","orbit_ecc","orbit_tilt","orbit_peri_angle","orbit_peri_time"]
                commands.append("orbit "+" ".join(str(data[f]) for f in fields)+" "+str(point["time"]))
                parent = lookup.get((data["orbit_parent"],point["time"]),[0,0,0])
                expected.append([a-b for a,b in zip(point["position"],parent)])
        result = subprocess.run([str(binary),"probe"],input="\n".join(commands),text=True,capture_output=True,check=True,env=env)
        actual = [[float(x) for x in line.split()] for line in result.stdout.splitlines()]
        self.assertEqual(len(actual),len(expected))
        for got,want in zip(actual,expected):
            for a,b in zip(got,want): self.assertAlmostEqual(a,b,delta=0.003)


if __name__ == "__main__":
    unittest.main(verbosity=2)
