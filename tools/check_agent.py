"""End-to-end agent smoke test. Pass the path to a built Brycescape executable."""

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    directory = Path(__file__).resolve().parent
    binary = Path(sys.argv[1]).resolve(strict=True)
    recipe = json.loads((directory / "example_recipe.json").read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory() as temp, subprocess.Popen(
        [sys.executable, str(directory / "brycescape_agent.py"), "--binary", str(binary), "--workspace", temp, "mcp"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8",
    ) as process:
        counter = 0

        def call(method, params=None):
            nonlocal counter
            counter += 1
            process.stdin.write(json.dumps({"jsonrpc": "2.0", "id": counter,
                                           "method": method, "params": params or {}}) + "\n")
            process.stdin.flush()
            response = json.loads(process.stdout.readline())
            assert response["id"] == counter and "result" in response, response
            return response["result"]

        def tool(name, **args):
            return call("tools/call", {"name": "brycescape_" + name, "arguments": args})

        try:
            assert "tools" in call("initialize", {"protocolVersion": "2025-11-25"})["capabilities"]
            assert len(call("tools/list")["tools"]) == 4
            assert "Rolling Hills" in json.loads(tool("catalog")["content"][0]["text"])["terrains"]
            created = tool("compose", output="lake.c3d", operations=recipe)
            assert not created["isError"], created
            assert len(json.loads(created["content"][0]["text"])["objects"]) > 10
            assert not tool("inspect", scene="lake.c3d")["isError"]
            render = tool("render", scene="lake.c3d", output="lake.png", width=96, height=60)
            assert not render["isError"] and render["content"][1]["type"] == "image", render
            assert (Path(temp) / "lake.png").read_bytes().startswith(b"\x89PNG")
            edited = [{"op": "base", "path": "lake.c3d"}, {"op": "title", "title": "Night Four"}]
            result = tool("compose", output="lake edit.c3d", operations=edited)
            assert not result["isError"] and json.loads(result["content"][0]["text"])["title"] == "Night Four", result
            grid = [{"op": "terrain", "name": "Tile", "kind": "Rolling Hills", "scale": [10, 5, 15]},
                    {"op": "terrain_tiles", "name": "Tile", "columns": 2, "rows": 2}]
            tiled = tool("compose", output="tiles.c3d", operations=grid)
            assert not tiled["isError"], tiled
            corners = json.loads(tiled["content"][0]["text"])["objects"]
            assert len(corners) == 4 and corners[-1]["position"] == [20, 0, 30], corners
            assert tool("compose", output="../outside.c3d", operations=recipe)["isError"]
            assert tool("compose", output="lake.c3d", operations=recipe)["isError"]
            assert tool("compose", output="bad.c3d", operations=[{"op": "object", "name": "Bad", "kind": "unknown"}])["isError"]
            assert not (Path(temp) / "bad.c3d").exists()
        finally:
            process.stdin.close()
            process.wait(timeout=10)
    print("MCP scene creation, editing, render preview and input validation passed.")


if __name__ == "__main__":
    main()
