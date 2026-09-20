"""Syntax-check both installed Lua variants with a real Lua runtime, not string scans.

The legacy WoW 3.3.5a addon may contain syntax that fails before OnLoad/UI banner.
"""
from pathlib import Path
from lupa import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/auto_interrupt/addon/FrostmourneCastProbe.lua").read_text(encoding="utf-8")
needle = "local kickRequests = false"
event = 'frame:SetScript("OnEvent", function(self, event, ...)'
if source.count(needle) != 1 or source.count(event) != 1:
    raise SystemExit("LUA COMPILE FAIL: missing opt-in marker or vararg event handler")
lua = LuaRuntime(unpack_returned_tuples=True)
compile_chunk = lua.eval("function(source) local compile = loadstring or load; local f, error = compile(source, '@FrostmourneCastProbe.lua'); return f ~= nil, error end")
for label, content in (
    ("default_OFF", source),
    ("trial_ON", source.replace(needle, "local kickRequests = true")),
):
    success, error = compile_chunk(content)
    if not success:
        raise SystemExit(f"LUA COMPILE FAIL [{label}]: {error}")
    print(f"LUA COMPILE PASS [{label}]: event handler accepts combat-log varargs")
