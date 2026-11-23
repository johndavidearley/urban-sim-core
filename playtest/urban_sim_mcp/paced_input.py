"""Paced native gestures for observable exploratory playtests."""
import asyncio
import math
import anyio


class PacedInput:
    def __init__(self, session):
        self.session = session
        self.cursor = (640, 360)

    async def event(self, **event):
        return await self.session.request("input", {"events": [event]})

    async def pause(self, seconds=1.2):
        await asyncio.sleep(seconds)

    async def key(self, name):
        await self.event(type="key", key=name)
        await self.pause(.18)

    async def move(self, x, y, seconds=.65):
        start = self.cursor
        steps = max(1, math.ceil(seconds / .06))
        for step in range(1, steps + 1):
            t = step / steps
            t = t * t * (3 - 2 * t)
            self.cursor = (round(start[0] + (x-start[0])*t),
                           round(start[1] + (y-start[1])*t))
            await self.event(type="move", x=self.cursor[0], y=self.cursor[1])
            await self.pause(seconds / steps)

    async def drag(self, start, end, button="left", seconds=1.2,
                   on_hold=None, cancel=False):
        await self.move(*start)
        await self.event(type="down", button=button, x=start[0], y=start[1])
        try:
            await self.pause(.15)
            await self.move(*end, seconds=seconds)
            await self.pause(.15)
            if on_hold is not None:
                await on_hold()
        except BaseException:
            cancel = True
            raise
        finally:
            # Cancellation must not leave a held construction/orbit gesture.
            with anyio.CancelScope(shield=True):
                if cancel and button == "left":
                    await self.event(type="key", key="Escape")
                await self.event(type="up", button=button,
                                 x=self.cursor[0], y=self.cursor[1])
        await self.pause(.6)

    async def click(self, x, y):
        await self.drag((x,y), (x,y), seconds=.12)

    async def control(self, name):
        controls = (await self.session.request("inspect", {"section":"ui"}))["controls"]
        c = next(c for c in controls if c["name"] == name)
        await self.click(c["x"]+c["width"]//2, c["y"]+c["height"]//2)

    async def moderate_zoom(self, pixels=30):
        if not 24 <= pixels <= 36:
            raise ValueError("Exploration zoom must stay between 24 and 36 pixels")
        while self.session.last_state["tile_pixels"] != pixels:
            before = self.session.last_state["tile_pixels"]
            await self.key("Minus" if before > pixels else "Plus")
            if self.session.last_state["tile_pixels"] == before:
                raise RuntimeError("Zoom stopped responding")

    async def frame_tile(self, x, y):
        """Pan through player keys, retaining projection and neighbourhood context."""
        for _ in range(100):
            p = await self.session.request("coordinates", {"direction":"tile_to_screen", "x":x, "y":y})
            dimensions = self.session.last_state["dimensions"]
            dx, dy = p["x"]-dimensions["width"]*.5, p["y"]-dimensions["height"]*.48
            if abs(dx) < 65 and abs(dy) < 55:
                return (p["x"], p["y"])
            if abs(dx) >= 65:
                await self.key("Right" if dx > 0 else "Left")
            if abs(dy) >= 55:
                await self.key("Down" if dy > 0 else "Up")
        raise RuntimeError("Could not frame interaction site with native panning")

    async def orbit(self, point, dx=85, dy=-15):
        await self.drag(point, (point[0]+dx,point[1]+dy), button="right", seconds=1.6)
