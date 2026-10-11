"""Owned game process, serialized bridge commands, and retained evidence."""

from __future__ import annotations
import asyncio
import anyio
import json
import os
import time
import uuid
from pathlib import Path
from typing import Any


class PlaytestError(RuntimeError):
    pass


class Session:
    def __init__(
        self, executable: str | Path, artifact_root: str | Path, timeout: float = 30
    ):
        self.executable = Path(executable).expanduser().resolve()
        self.artifact_root = Path(artifact_root).expanduser().resolve()
        self.timeout = timeout
        self.lock = asyncio.Lock()
        self.process: asyncio.subprocess.Process | None = None
        self.run_dir: Path | None = None
        self.broken = False
        self.request_id = 0
        self.config: dict[str, Any] = {}
        self.capabilities: dict[str, Any] = {}
        self.last_state: dict[str, Any] = {}
        self.log_file = None
        self.recording: dict[str, Any] | None = None
        self.record_started = 0.0

    async def launch(
        self,
        terrain_seed: int = 42,
        simulation_seed: int = 98765,
        mode: str = "deterministic",
    ) -> dict:
        async with self.lock:
            return await self._launch(terrain_seed, simulation_seed, mode)

    async def _launch(
        self, terrain_seed=42, simulation_seed=98765, mode="deterministic"
    ):
        if self.process and self.process.returncode is None:
            raise PlaytestError(
                "A game is already running; stop it before launching another"
            )
        await self._terminate()
        if mode not in ("deterministic", "realtime"):
            raise PlaytestError("Unknown time mode")
        if any(
            type(seed) is not int or not 0 <= seed <= 0xFFFFFFFF
            for seed in (terrain_seed, simulation_seed)
        ):
            raise PlaytestError("Seeds must be unsigned 32-bit integers")
        if not self.executable.is_file():
            raise PlaytestError(f"Playtest executable not found: {self.executable}")
        self.artifact_root.mkdir(parents=True, exist_ok=True)
        self.run_dir = self.artifact_root / (
            time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
        )
        self.run_dir.mkdir()
        self.config = dict(
            terrain_seed=terrain_seed, simulation_seed=simulation_seed, mode=mode
        )
        env = os.environ.copy()
        self.log_file = (self.run_dir / "game.log").open("wb")
        self.broken = False
        self.last_state = {}
        self.recording = None
        try:
            self.process = await asyncio.create_subprocess_exec(
                str(self.executable),
                "--terrain-seed",
                str(terrain_seed),
                "--simulation-seed",
                str(simulation_seed),
                "--time-mode",
                mode,
                cwd=self.run_dir,
                env=env,
                stdin=asyncio.subprocess.PIPE,
                stdout=asyncio.subprocess.PIPE,
                stderr=self.log_file,
                limit=4 * 1024 * 1024,
            )
            result = await self._request("status", {})
            self.capabilities = result.get("capabilities", {})
            if self.capabilities.get("bridge_version") != 1:
                raise PlaytestError("Unsupported game bridge version")
            (self.run_dir / "run.json").write_text(
                json.dumps(
                    {
                        "version": 1,
                        "configuration": self.config,
                        "capabilities": self.capabilities,
                        "executable": str(self.executable),
                    },
                    indent=2,
                ),
                encoding="utf-8",
            )
            return {
                **result,
                "run_directory": str(self.run_dir),
                "pid": self.process.pid,
            }
        except BaseException:
            self.broken = True
            with anyio.CancelScope(shield=True):
                await self._terminate()
            raise

    def trace(self, entry: dict):
        if self.run_dir:
            with (self.run_dir / "actions.jsonl").open("a", encoding="utf-8") as stream:
                stream.write(json.dumps(entry, allow_nan=False) + "\n")

    async def request(self, command: str, args: dict | None = None) -> dict:
        async with self.lock:
            return await self._request(command, args or {})

    async def _request(self, command: str, args: dict) -> dict:
        if not self.process or self.broken or self.process.returncode is not None:
            raise PlaytestError("No usable game session; launch a fresh session")
        self.request_id += 1
        request = dict(version=1, id=self.request_id, command=command, args=args)
        encoded = json.dumps(request, allow_nan=False).encode() + b"\n"
        if len(encoded) > 1048576:
            raise PlaytestError("Command exceeds 1 MiB")
        source = (
            "ui"
            if command == "input"
            else "window" if command == "window" else "semantic"
        )
        entry = dict(request=request, source=source, time=time.monotonic())
        if self.recording is not None:
            self.recording["steps"].append(
                {
                    "command": command,
                    "args": args,
                    "source": source,
                    "delay_ms": round((time.monotonic() - self.record_started) * 1000),
                }
            )
        try:
            assert self.process.stdin and self.process.stdout
            self.process.stdin.write(encoded)
            await asyncio.wait_for(self.process.stdin.drain(), self.timeout)
            line = await asyncio.wait_for(self.process.stdout.readline(), self.timeout)
            if not line:
                raise PlaytestError("Game exited without responding; see game.log")
            response = json.loads(line)
            if response.get("id") is None and "error" in response:
                raise PlaytestError(response["error"]["message"])
            if response.get("version") != 1 or response.get("id") != self.request_id:
                raise PlaytestError("Invalid or out-of-order game response")
            entry["response"] = response
        except BaseException as error:
            self.broken = True
            entry["failure"] = type(error).__name__ + ": " + str(error)
            self.trace(entry)
            # A mutation may already have run. Never retry or reuse these pipes.
            with anyio.CancelScope(shield=True):
                await self._terminate()
            raise
        self.trace(entry)
        if self.recording is not None:
            self.record_started = time.monotonic()
        if "error" in response:
            if self.recording is not None:
                self.recording["steps"][-1]["expected_error"] = response["error"][
                    "message"
                ]
            raise PlaytestError(response["error"]["message"])
        result = response["result"]
        self.last_state = result.get("state", {})
        return result

    async def stop(self, action="discard") -> dict:
        async with self.lock:
            if not self.process:
                return {
                    "stopped": True,
                    "run_directory": str(self.run_dir) if self.run_dir else None,
                }
            if action not in ("save", "discard"):
                raise PlaytestError("Stop action must be save or discard")
            result = {}
            if not self.broken and self.process.returncode is None:
                result = await self._request("stop", {"action": action})
            await self._terminate()
            return {**result, "stopped": True, "run_directory": str(self.run_dir)}

    async def _terminate(self):
        process = self.process
        if process and process.returncode is None:
            if process.stdin:
                process.stdin.close()
            try:
                await asyncio.wait_for(process.wait(), 2)
            except asyncio.TimeoutError:
                process.terminate()
                try:
                    await asyncio.wait_for(process.wait(), 2)
                except asyncio.TimeoutError:
                    process.kill()
                    await process.wait()
        if self.log_file:
            self.log_file.close()
            self.log_file = None

    def artifact(self, relative: str) -> Path:
        if not self.run_dir:
            raise PlaytestError("No run directory")
        root = self.run_dir.resolve()
        path = (root / relative).resolve()
        if not path.is_relative_to(root):
            raise PlaytestError("Artifact path escapes the run directory")
        return path

    def import_save(self, source: str):
        path = Path(source).expanduser().resolve()
        data = json.loads(path.read_text(encoding="utf-8"))
        snapshot = Path(data["citySnapshot"])
        if not snapshot.is_absolute():
            snapshot = path.parent / snapshot
        target = self.artifact("urban_sim_session.json.city.json")
        # Validate both documents before replacing either run-local file.
        snapshot_bytes = snapshot.read_bytes()
        json.loads(snapshot_bytes)
        data["citySnapshot"] = target.name
        target.write_bytes(snapshot_bytes)
        self.artifact("urban_sim_session.json").write_text(
            json.dumps(data), encoding="utf-8"
        )

    def logs(self, lines=100):
        if not 1 <= lines <= 500:
            raise PlaytestError("Log lines must be 1..500")
        if not self.run_dir:
            return []
        path = self.artifact("game.log")
        if not path.exists():
            return []
        with path.open("rb") as stream:
            stream.seek(max(0, path.stat().st_size - 256 * 1024))
            return stream.read().decode("utf-8", errors="replace").splitlines()[-lines:]

    async def screenshot(self, name="frame"):
        async with self.lock:
            result = await self._request("screenshot", {"name": name})
            from PIL import Image

            ppm = self.artifact(result["capture"])
            png = ppm.with_suffix(".png")
            with Image.open(ppm) as image:
                image.save(png, format="PNG")
            result["png"] = str(png)
            result["artifact"] = str(png.relative_to(self.run_dir))
            return result

    async def city(self, action, size=64, terrain=False, import_path=None):
        async with self.lock:
            if action == "new" and self.last_state.get("phase") != "start_screen":
                config = self.config.copy()
                await self._terminate()
                await self._launch(**config)
            if import_path:
                if action != "load":
                    raise PlaytestError("import_path is only supported for load")
                self.import_save(import_path)
            result = await self._request(
                "city", dict(action=action, size=size, terrain=terrain)
            )
            if import_path and self.recording is not None:
                self.recording["steps"][-1]["import_save"] = {
                    "session": json.loads(
                        self.artifact("urban_sim_session.json").read_text(
                            encoding="utf-8"
                        )
                    ),
                    "city": json.loads(
                        self.artifact("urban_sim_session.json.city.json").read_text(
                            encoding="utf-8"
                        )
                    ),
                }
            return result
