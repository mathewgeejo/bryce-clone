"""Brycescape scene recipes and local MCP tools. Requires Python 3.10+."""

import argparse
import base64
import json
import math
import re
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path


def string(description=""):
    return {"type": "string", "minLength": 1, "maxLength": 4096, "description": description}


def number(low, high, integer=False):
    return {"type": "integer" if integer else "number", "minimum": low, "maximum": high}


def vector(low=-100000, high=100000):
    return {"type": "array", "items": number(low, high), "minItems": 3, "maxItems": 3}


def optional(schema, default):
    return dict(schema, default=default)


def object_schema(fields):
    return {
        "type": "object",
        "properties": fields,
        "required": [key for key, spec in fields.items() if "default" not in spec],
        "additionalProperties": False,
    }


NAME = string("Unique object name. Tree parts are named '<name> trunk' and '<name> foliage'.")
PRESET = string("Exact case-sensitive name from the Brycescape catalog.")
ASSET = string("Workspace-relative input file.")
TRANSFORM = {
    "position": optional(vector(), [0, 0, 0]),
    "rotation": optional(vector(), [0, 0, 0]),
    "scale": optional(vector(.002, 100000), [1, 1, 1]),
}
KEY_TRANSFORM = {key: {k: v for k, v in spec.items() if k != "default"} for key, spec in TRANSFORM.items()}
CAMERA = {
    "target": vector(),
    "yaw": optional(number(-360, 360), 20),
    "pitch": optional(number(-89.9, 89.9), 8),
    "distance": optional(number(.1, 100000), 48),
    "fov": optional(number(5, 150), 50),
}
SEED = optional(number(0, 2147483647, True), 1)
FRAME = number(0, 99999, True)
DIMENSION = number(1, 8192, True)
SIMULATION = {
    "frames": optional(number(2, 240, True), 60),
    "gravity": optional(number(0, 50), 9.81),
    "velocity": optional(vector(-50, 50), [0, 0, 0]),
    "floor": optional({"type": "boolean"}, True),
}
GRAVITY_PRESETS = ["Earth", "Moon", "Mars", "Jupiter", "Zero G", "Reverse", "Attract", "Repel"]
ADVANCED_SIMULATION = {
    "gravity_preset": optional({"type": "string", "enum": GRAVITY_PRESETS, "minLength": 1, "maxLength": 32}, "Earth"),
    "start_frame": optional(FRAME, 0),
    "detail": optional(number(0, 2, True), 1),
    "direction": optional(vector(-100, 100), [0, -1, 0]),
    "center": optional(vector(), [0, 0, 0]),
    "radius": optional(number(.1, 1000), 5),
}
RIGID_PRESETS = {"type": "string", "enum": ["Balanced", "Rubber", "Steel", "Ice", "Wood"], "minLength": 1, "maxLength": 32}
LIQUID_PRESETS = {"type": "string", "enum": ["Water", "Oil", "Syrup", "Lava"], "minLength": 1, "maxLength": 32}
OPS = {
    "base": {"path": ASSET},
    "example": {"index": number(0, 10000, True)},
    "title": {"title": string()},
    "sky": {"preset": PRESET},
    "environment": {"path": ASSET, "yaw": optional(number(-360, 360), 0),
                    "strength": optional(number(0, 64), 1)},
    "camera": CAMERA,
    "timeline": {"frames": number(1, 100000, True), "fps": optional(number(1, 240, True), 15)},
    "render": {"width": DIMENSION, "height": DIMENSION, "quality": optional(number(0, 2, True), 1),
               "samples": optional(number(1, 256, True), 16)},
    "camera_key": {"frame": FRAME, **CAMERA},
    "object": {"name": NAME, "kind": PRESET, **TRANSFORM},
    "terrain": {"name": NAME, **TRANSFORM, "kind": PRESET,
                "resolution": optional(number(17, 4097, True), 129), "seed": SEED,
                "roughness": optional(number(0, 1), .5)},
    "tree": {"name": NAME, **TRANSFORM, "seed": SEED},
    "mesh": {"name": NAME, **TRANSFORM, "path": ASSET},
    "picture": {"name": NAME, **TRANSFORM, "path": ASSET},
    "heightmap": {"name": NAME, "path": ASSET},
    "terrain_key": {"name": NAME, "path": ASSET, "frame": FRAME},
    "tileable": {"name": NAME, "border": optional(number(.01, .5), .15)},
    "terrain_tiles": {"name": NAME, "columns": number(1, 16, True), "rows": number(1, 16, True)},
    "transform": {"name": NAME, **KEY_TRANSFORM},
    "color": {"name": NAME, "rgb": vector(0, 1)},
    "material": {"name": NAME, "preset": PRESET},
    "layer": {"name": NAME, "texture": PRESET, "color_a": vector(0, 1), "color_b": vector(0, 1),
              "scale": optional(number(.01, 20), 1), "opacity": optional(number(0, 1), 1)},
    "light": {"name": NAME, "rgb": vector(0, 1), "power": number(0, 1000)},
    "key": {"name": NAME, "frame": FRAME, **KEY_TRANSFORM},
    "parent": {"name": NAME, "parent": NAME},
    "rigid_bake": {**SIMULATION, **ADVANCED_SIMULATION, "preset": optional(RIGID_PRESETS, "Balanced"),
                   "bounce": optional(number(0, 1), .35),
                   "friction": optional(number(0, 1), .45),
                   "damping": optional(number(0, 5), 0),
                   "names": {"type": "array", "items": NAME, "minItems": 1, "maxItems": 96}},
    "liquid_bake": {"name": NAME, **SIMULATION, **ADVANCED_SIMULATION, "preset": optional(LIQUID_PRESETS, "Water"),
                    "resolution": optional(number(2, 8, True), 4), "viscosity": optional(number(0, 1), .08),
                    "cohesion": optional(number(0, 1), 0), "surface": optional(number(.8, 1.3), 1),
                    "hide_source": optional({"type": "boolean"}, True)},
    "scatter": {"surface": NAME, "source": NAME, "count": number(1, 10000, True), "seed": SEED,
                "min": vector(), "max": vector()},
}
RECIPE = {
    "type": "array", "maxItems": 10000,
    "items": {"oneOf": [object_schema({"op": {"const": op}, **fields}) for op, fields in OPS.items()]},
}


def validate(value, schema, label):
    kind = schema.get("type")
    if kind == "object":
        if not isinstance(value, dict):
            raise ValueError(f"{label} must be an object")
        fields = schema["properties"]
        if value.keys() - fields.keys():
            raise ValueError(f"{label} has unknown fields {sorted(value.keys() - fields.keys())}")
        for key in schema["required"]:
            if key not in value:
                raise ValueError(f"{label} is missing {key}")
        return {key: validate(value[key], spec, f"{label}.{key}") if key in value else spec["default"]
                for key, spec in fields.items()}
    if "const" in schema:
        if value != schema["const"]:
            raise ValueError(f"{label} must be {schema['const']}")
    elif kind == "string":
        if (not isinstance(value, str) or not schema["minLength"] <= len(value.encode("utf-8")) <= schema["maxLength"]
                or any(ord(c) < 32 for c in value)):
            raise ValueError(f"{label} must be a nonempty string without control characters")
        if "enum" in schema and value not in schema["enum"]:
            raise ValueError(f"{label} must be one of {', '.join(schema['enum'])}")
    elif kind == "array":
        if not isinstance(value, list) or not schema.get("minItems", 0) <= len(value) <= schema["maxItems"]:
            raise ValueError(f"{label} has the wrong number of items")
        return [validate(item, schema["items"], label) for item in value]
    elif kind in ("integer", "number"):
        types = (int,) if kind == "integer" else (int, float)
        if type(value) not in types or not schema["minimum"] <= value <= schema["maximum"] or not math.isfinite(value):
            raise ValueError(f"{label} must be a {kind} from {schema['minimum']} to {schema['maximum']}")
    elif kind == "boolean" and type(value) is not bool:
        raise ValueError(f"{label} must be a boolean")
    return value


def encode(value):
    if isinstance(value, bool):
        return "1" if value else "0"
    if isinstance(value, list):
        return " ".join(map(encode, value))
    if isinstance(value, str):
        return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'
    return str(value)


def encode_simulation(op, values, command):
    def override(key):
        return values[key] if key in command else -1

    preset = values["gravity_preset"]
    direction = values["direction"] if "direction" in command else [0, 1 if preset == "Reverse" else -1, 0]
    settings = [preset, values["frames"], values["start_frame"], values["detail"], override("gravity"),
                values["velocity"], override("floor"), direction, values["center"], values["radius"], values["preset"]]
    if op == "rigid_bake":
        return settings + [override("bounce"), override("friction"), override("damping"), values["names"]]
    return [values["name"]] + settings + [values["resolution"], override("viscosity"), override("cohesion"),
                                          override("surface"), values["hide_source"]]


class Studio:
    def __init__(self, binary, workspace):
        self.binary = binary.resolve(strict=True)
        self.workspace = workspace.resolve(strict=True)

    def path(self, value, suffixes=(), output=False):
        path = (self.workspace / value).resolve()
        if not path.is_relative_to(self.workspace):
            raise ValueError(f"Path is outside the workspace: {value}")
        if suffixes and path.suffix.lower() not in suffixes:
            raise ValueError(f"Expected {', '.join(suffixes)} for {value}")
        if not output and not path.is_file():
            raise ValueError(f"File not found: {value}")
        if output and not path.parent.is_dir():
            raise ValueError(f"Output directory does not exist: {path.parent}")
        return path

    def run(self, *args):
        result = subprocess.run(
            [str(self.binary), *map(str, args)], cwd=self.workspace, capture_output=True,
            text=True, encoding="utf-8", errors="replace", timeout=300,
        )
        if result.returncode:
            raise ValueError(result.stderr.strip() or f"Brycescape {args[0]} exited with code {result.returncode}")

    def describe(self, scene=None):
        with tempfile.TemporaryDirectory() as temp:
            result = Path(temp) / "metadata.json"
            if scene is None:
                self.run("--catalog", result)
            else:
                self.run("--inspect", self.path(scene, (".c3d",)), result)
            return json.loads(result.read_text(encoding="utf-8"))

    def compose(self, output, operations, overwrite=False):
        destination = self.path(output, (".c3d",), output=True)
        if destination.exists() and not overwrite:
            raise ValueError(f"Scene already exists: {output}. Set overwrite to replace it.")
        if not isinstance(operations, list) or len(operations) > RECIPE["maxItems"]:
            raise ValueError("operations must be an array of up to 10000 commands")
        lines = ["BRYCESCAPE_RECIPE 1"]
        for index, command in enumerate(operations):
            if not isinstance(command, dict) or type(command.get("op")) is not str or command["op"] not in OPS:
                raise ValueError(f"Unknown operation at index {index}")
            op = command["op"]
            values = validate(command, object_schema({"op": {"const": op}, **OPS[op]}), f"operations[{index}]")
            if "path" in values:
                suffixes = {"base": (".c3d",), "mesh": (".obj",), "environment": (".hdr", ".rgbe", ".pic")}
                values["path"] = str(self.path(values["path"], suffixes.get(op, ())))
            del values["op"]
            if op in ("rigid_bake", "liquid_bake"):
                lines.append(op + "_v2 " + " ".join(map(encode, encode_simulation(op, values, command))))
            else:
                lines.append(op + " " + " ".join(map(encode, values.values())))
        with tempfile.TemporaryDirectory() as temp:
            recipe = Path(temp) / "scene.recipe"
            recipe.write_text("\n".join(lines) + "\n", encoding="utf-8")
            self.run("--compose", recipe, destination)
        return {"path": str(destination), **self.describe(output)}

    def render(self, scene, output, width=640, height=400, preview=False, overwrite=False):
        source = self.path(scene, (".c3d",))
        target = self.path(output, (".png",), output=True)
        outputs = [target, target.with_name(target.stem + "_depth.png"), target.with_name(target.stem + "_id.png")]
        if not overwrite and any(path.exists() or path.is_symlink() for path in outputs):
            raise ValueError(f"Render output already exists: {output}. Set overwrite to replace it.")
        with tempfile.TemporaryDirectory(dir=target.parent) as temp:
            rendered = Path(temp) / target.name
            self.run("--render", rendered, width, height, source)
            files = []
            for path in outputs:
                artifact = Path(temp) / path.name
                if artifact.exists():
                    artifact.replace(path)
                    files.append(str(path))
        image = target.read_bytes() if preview and target.stat().st_size <= 4 * 1024 * 1024 else None
        return {"path": str(target), "files": files, "width": width, "height": height}, image


OVERWRITE = optional({"type": "boolean"}, False)
TOOLS = {
    "brycescape_catalog": ("List exact preset names, object types, textures, terrain generators, and example indices.",
                           object_schema({})),
    "brycescape_inspect": ("Read object names, transforms, and frame count from an editable scene.",
                           object_schema({"scene": ASSET})),
    "brycescape_compose": ("Create an editable .c3d scene from ordered commands. Use a base scene as the first command to edit it.",
                           object_schema({"output": string(), "operations": RECIPE, "overwrite": OVERWRITE})),
    "brycescape_render": ("Render a scene to PNG and optionally return the image for visual iteration.",
                          object_schema({"scene": ASSET, "output": string(), "width": optional(DIMENSION, 640),
                                         "height": optional(DIMENSION, 400), "preview": optional({"type": "boolean"}, True),
                                         "overwrite": OVERWRITE})),
}


def serve(studio):
    operations = dict(zip(TOOLS, (studio.describe, studio.describe, studio.compose, studio.render)))
    for line in sys.stdin.buffer:
        request_id = None
        try:
            request = json.loads(line)
            if not isinstance(request, dict) or request.get("jsonrpc") != "2.0":
                raise ValueError("Invalid JSON-RPC request")
            request_id = request.get("id")
            if "id" not in request:
                continue
            method = request.get("method")
            params = request.get("params", {})
            if method == "initialize":
                version = params.get("protocolVersion")
                result = {"protocolVersion": version if version in {"2024-11-05", "2025-03-26", "2025-06-18", "2025-11-25"} else "2025-11-25",
                          "capabilities": {"tools": {}}, "serverInfo": {"name": "brycescape", "version": "1.0.0"}}
            elif method == "ping":
                result = {}
            elif method == "tools/list":
                result = {"tools": [{"name": tool, "description": desc, "inputSchema": schema}
                                    for tool, (desc, schema) in TOOLS.items()]}
            elif method == "tools/call" and isinstance(params, dict) and params.get("name") in TOOLS:
                tool = params["name"]
                try:
                    arguments = validate(params.get("arguments", {}), TOOLS[tool][1], "arguments")
                    value = operations[tool](**arguments)
                    image = None
                    if tool == "brycescape_render":
                        value, image = value
                    content = [{"type": "text", "text": json.dumps(value)}]
                    if image is not None:
                        content.append({"type": "image", "mimeType": "image/png",
                                        "data": base64.b64encode(image).decode("ascii")})
                    result = {"content": content, "isError": False}
                except (ValueError, OSError, subprocess.SubprocessError) as error:
                    result = {"content": [{"type": "text", "text": str(error)}], "isError": True}
            else:
                response = {"jsonrpc": "2.0", "id": request_id,
                            "error": {"code": -32601, "message": "Unknown method or tool"}}
                print(json.dumps(response), flush=True)
                continue
            response = {"jsonrpc": "2.0", "id": request_id, "result": result}
        except (ValueError, TypeError, AttributeError) as error:
            response = {"jsonrpc": "2.0", "id": request_id,
                        "error": {"code": -32700 if isinstance(error, json.JSONDecodeError) else -32600,
                                  "message": str(error)}}
        print(json.dumps(response), flush=True)


def probe_bryce(path):
    """Inspect the header and first compressed stream from a known BR7 container."""
    with path.open("rb") as source:
        sample = source.read(1024 * 1024)
    magic = re.match(rb"Bryce_[0-9]+\.[0-9]+_File\x00", sample)
    if not magic:
        raise ValueError("Bryce scene signature not recognized")
    result = {"signature": magic[0][:-1].decode("ascii"), "file_bytes": path.stat().st_size}
    if result["signature"] == "Bryce_6.0_File" and len(sample) > 42:
        try:
            stream = zlib.decompressobj()
            decoded = stream.decompress(sample[42:], 4 * 1024 * 1024)
            if stream.eof:
                result["first_zlib_stream"] = {"offset": 42, "decoded_bytes": len(decoded)}
        except zlib.error:
            pass
    return result


def main():
    root = Path(__file__).resolve().parents[1]
    binaries = ([root / "build/Brycescape.exe", root / "out/windows/Brycescape.exe"]
                if sys.platform == "win32" else [root / "out/linux/Brycescape"])
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=next((p for p in binaries if p.is_file()), binaries[0]))
    parser.add_argument("--workspace", type=Path, default=Path.cwd())
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("mcp")
    commands.add_parser("catalog")
    commands.add_parser("schema")
    commands.add_parser("inspect").add_argument("scene")
    commands.add_parser("probe-bryce").add_argument("scene", type=Path)
    compose = commands.add_parser("compose")
    compose.add_argument("recipe", type=Path)
    compose.add_argument("output")
    compose.add_argument("--overwrite", action="store_true")
    render = commands.add_parser("render")
    render.add_argument("scene")
    render.add_argument("output")
    render.add_argument("--width", type=int, default=640)
    render.add_argument("--height", type=int, default=400)
    render.add_argument("--overwrite", action="store_true")
    args = parser.parse_args()
    try:
        if args.command == "schema":
            print(json.dumps(RECIPE, indent=2))
            return
        if args.command == "probe-bryce":
            print(json.dumps(probe_bryce(args.scene), indent=2))
            return
        studio = Studio(args.binary, args.workspace)
        if args.command == "mcp":
            serve(studio)
            return
        if args.command == "compose":
            recipe = json.loads(args.recipe.read_text(encoding="utf-8"))
            result = studio.compose(args.output, recipe, args.overwrite)
        elif args.command == "render":
            width = validate(args.width, DIMENSION, "width")
            height = validate(args.height, DIMENSION, "height")
            result, _ = studio.render(args.scene, args.output, width, height, overwrite=args.overwrite)
        else:
            result = studio.describe(args.scene if args.command == "inspect" else None)
        print(json.dumps(result, indent=2))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
