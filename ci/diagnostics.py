from __future__ import annotations

import json
import re
from collections import defaultdict, deque
from pathlib import Path

from common import fail

HEADER = re.compile(r"^(ERROR|WARNING): (.*)$")
LOCATION = re.compile(r"\s+at: (.*?) \((.*):(\d+)\)\s*$")


def read(path: Path) -> list[dict]:
    try:
        events = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise fail("cannot read diagnostic expectations at %s (%s)" % (
            path, error
        ))
    if not isinstance(events, list):
        raise fail("diagnostic expectations at %s are not an array" % path)
    for event in events:
        if (
            not isinstance(event, dict)
            or event.get("severity") not in ("ERROR", "WARNING", "STDERR")
            or not isinstance(event.get("message"), str)
            or not isinstance(event.get("file"), str)
            or not isinstance(event.get("function"), str)
            or type(event.get("line")) is not int
            or type(event.get("passed")) is not bool
            or type(event.get("expected")) is not bool
        ):
            raise fail("invalid diagnostic expectation at %s" % path)
    return events


def visible(raw: str, events: list[dict]) -> tuple[str, int]:
    pending = defaultdict(deque)
    for event in events:
        key = (
            event["severity"], event["message"], event["function"],
            event["file"], event["line"],
        )
        pending[key].append(event["expected"] and event["passed"])

    lines = raw.splitlines(keepends=True)
    output = []
    hidden = 0
    at = 0
    while at < len(lines):
        plain = lines[at].rstrip("\r\n")
        stderr_key = ("STDERR", plain, "", "", 0)
        if pending[stderr_key]:
            if pending[stderr_key].popleft():
                hidden += 1
                at += 1
                continue
        header = HEADER.fullmatch(plain)
        if header is None:
            output.append(lines[at])
            at += 1
            continue
        end = at + 1
        location = None
        while end < len(lines):
            following = lines[end]
            if following.strip() and not following[:1].isspace():
                break
            if location is None:
                location = LOCATION.fullmatch(following.rstrip("\r\n"))
            end += 1
        hide = False
        if location is not None:
            key = (
                header[1], header[2], location[1], location[2],
                int(location[3]),
            )
            if pending[key]:
                hide = pending[key].popleft()
        if hide:
            hidden += 1
        else:
            output.extend(lines[at:end])
        at = end
    return "".join(output), hidden
