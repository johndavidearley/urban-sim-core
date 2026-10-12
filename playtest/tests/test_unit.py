import asyncio
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import AsyncMock
from urban_sim_mcp.session import Session, PlaytestError
from urban_sim_mcp.scenarios import evaluate_assertion
from urban_sim_mcp.server import create_server


class Assertions(unittest.TestCase):
    def test_nested_comparisons_and_counts(self):
        self.assertTrue(
            evaluate_assertion({"state": {"tick": 3}}, "state.tick", "ge", 3)["passed"]
        )
        self.assertTrue(
            evaluate_assertion({"items": [1, 2]}, "items", "count", 2)["passed"]
        )
        self.assertTrue(
            evaluate_assertion({"items": [{"zone": 1}]}, "items.0.zone", "eq", 1)[
                "passed"
            ]
        )

    def test_invalid_and_failed_assertions(self):
        for field, op, expected in [
            ("missing", "eq", 1),
            ("value", "gt", "x"),
            ("value", "exec", 1),
            ("value", "eq", 2),
        ]:
            with self.assertRaises(PlaytestError):
                evaluate_assertion({"value": 1}, field, op, expected)

    def test_artifact_traversal_and_import(self):
        with tempfile.TemporaryDirectory() as root:
            session = Session("unused", root)
            session.run_dir = Path(root) / "run"
            session.run_dir.mkdir()
            with self.assertRaises(PlaytestError):
                session.artifact("../outside")
            source = Path(root) / "source"
            source.mkdir()
            (source / "city.json").write_text('{"tiles":[]}')
            (source / "save.json").write_text('{"citySnapshot":"city.json","tick":3}')
            session.import_save(str(source / "save.json"))
            self.assertEqual(
                json.loads(session.artifact("urban_sim_session.json").read_text())[
                    "citySnapshot"
                ],
                "urban_sim_session.json.city.json",
            )


class Protocol(unittest.IsolatedAsyncioTestCase):
    async def test_server_discovery_without_graphics(self):
        with tempfile.TemporaryDirectory() as root:
            server = create_server(Session("unused", root))
            tools = await server.list_tools()
            self.assertEqual(
                {t.name for t in tools},
                {
                    "launch",
                    "status",
                    "stop",
                    "input",
                    "window",
                    "city",
                    "time",
                    "tool",
                    "view",
                    "inspect",
                    "coordinates",
                    "screenshot",
                    "diagnostics",
                    "record",
                    "replay",
                    "assert_state",
                },
            )
            self.assertEqual(len(await server.list_prompts()), 2)
            self.assertEqual(len(await server.list_resources()), 5)
            inspection = await server.read_resource("urban-sim://inspection")
            reference = json.loads(list(inspection)[0].content)
            self.assertEqual(reference["inspection_version"], 3)
            self.assertIn("summary", reference["sections"])
            self.assertNotIn("state", reference["sections"])
            self.assertEqual(
                reference["summary"]["population_change"]["identity"],
                "net = arrivals - departures - deaths",
            )
            self.assertIn("illness_rate", reference["local_rates"]["fields"])
            inspect_tool = next(t for t in tools if t.name == "inspect")
            self.assertEqual(
                inspect_tool.inputSchema["properties"]["section"]["enum"],
                reference["sections"],
            )
            result = await server.call_tool("status", {})
            self.assertIsNotNone(result)

    async def test_timeout_poison_and_no_retry(self):
        with tempfile.TemporaryDirectory() as root:
            session = Session("unused", root, timeout=0.01)
            session.run_dir = Path(root)

            class Writer:
                count = 0

                def write(self, _data):
                    self.count += 1

                async def drain(self):
                    pass

                def close(self):
                    pass

            class Reader:
                async def readline(self):
                    await asyncio.sleep(5)

            class Process:
                returncode = None
                stdin = Writer()
                stdout = Reader()

            session.process = Process()
            session._terminate = AsyncMock()
            with self.assertRaises(asyncio.TimeoutError):
                await session.request("tool", {"action": "apply"})
            self.assertTrue(session.broken)
            with self.assertRaises(PlaytestError):
                await session.request("status")
            self.assertEqual(session.process.stdin.count, 1)
            self.assertTrue((Path(root) / "actions.jsonl").exists())

    async def test_invalid_response_invalidates_session(self):
        with tempfile.TemporaryDirectory() as root:
            session = Session("unused", root)
            session.run_dir = Path(root)

            class Writer:
                def write(self, _data):
                    pass

                async def drain(self):
                    pass

            class Reader:
                async def readline(self):
                    return b'{"version":1,"id":999,"result":{}}\n'

            class Process:
                returncode = None
                stdin = Writer()
                stdout = Reader()

            session.process = Process()
            session._terminate = AsyncMock()
            with self.assertRaises(PlaytestError):
                await session.request("status")
            self.assertTrue(session.broken)
            session._terminate.assert_awaited_once()

    async def test_cancel_poison(self):
        with tempfile.TemporaryDirectory() as root:
            session = Session("unused", root)
            session.run_dir = Path(root)

            class Writer:
                def write(self, _data):
                    pass

                async def drain(self):
                    pass

            class Reader:
                async def readline(self):
                    await asyncio.sleep(5)

            class Process:
                returncode = None
                stdin = Writer()
                stdout = Reader()

            session.process = Process()
            session._terminate = AsyncMock()
            task = asyncio.create_task(session.request("time", {"action": "step"}))
            await asyncio.sleep(0.01)
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await task
            self.assertTrue(session.broken)
            session._terminate.assert_awaited_once()


if __name__ == "__main__":
    unittest.main()
