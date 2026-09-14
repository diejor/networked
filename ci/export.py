#!/usr/bin/env python3
"""Export and validate a demo for deployment."""

from __future__ import annotations

import argparse
import contextlib
import functools
import http.server
import json
import os
import re
import shutil
import socketserver
import subprocess
import tempfile
import threading
import time
from pathlib import Path

from common import CI_DIR, ROOT, fail, git_head, github_output, load_json, main, run, sha256_file, write_json

EXAMPLES_FILE = CI_DIR / "examples.json"

WASM_MAGIC = b"\x00asm"
PCK_MAGIC = b"GDPC"

WEB_REQUIRED = ("index.html", "index.js", "index.wasm", "index.pck")
WEB_OPTIONAL = ("index.audio.worklet.js", "index.audio.position.worklet.js", "index.icon.png", "index.png")

CROSS_ORIGIN_HEADERS = {
    "Cross-Origin-Opener-Policy": "same-origin",
    "Cross-Origin-Embedder-Policy": "require-corp",
}

LANDING_TEMPLATE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<style>
:root {{
  color-scheme: light dark;
  --bg: #fafafa;
  --fg: #111;
  --btn-bg: #fff;
  --btn-border: #ccc;
  --btn-hover: #eee;
  --muted: #666;
}}
@media (prefers-color-scheme: dark) {{
  :root {{
    --bg: #141414;
    --fg: #eee;
    --btn-bg: #222;
    --btn-border: #383838;
    --btn-hover: #2e2e2e;
    --muted: #888;
  }}
}}
* {{ box-sizing: border-box; }}
body {{
  margin: 0;
  padding: 3rem 1rem;
  background: var(--bg);
  color: var(--fg);
  font: 16px/1.5 system-ui, -apple-system, sans-serif;
  display: flex;
  justify-content: center;
}}
main {{
  width: 100%;
  max-width: 20rem;
}}
h1 {{
  font-size: 1.25rem;
  font-weight: 600;
  margin: 0 0 1.25rem;
  text-align: center;
}}
.buttons {{
  display: flex;
  flex-direction: column;
  gap: 0.6rem;
}}
.btn {{
  display: block;
  text-align: center;
  padding: 0.75rem 1rem;
  background: var(--btn-bg);
  border: 1px solid var(--btn-border);
  border-radius: 4px;
  color: inherit;
  text-decoration: none;
  font-size: 1rem;
}}
.btn:hover, .btn:focus-visible {{
  background: var(--btn-hover);
}}
footer {{
  margin-top: 2rem;
  text-align: center;
  font-size: 0.8rem;
  color: var(--muted);
}}
footer a {{ color: inherit; }}
code {{ font-family: ui-monospace, monospace; }}
</style>
</head>
<body>
  <main>
    <h1>{title}</h1>
    <div class="buttons">
{buttons}
    </div>
    <footer>
      Built from <code>{revision}</code>.
      <a href="https://github.com/diejor/networked">github.com/diejor/networked</a>
    </footer>
  </main>
</body>
</html>
"""


@contextlib.contextmanager
def project_overrides(overrides: dict[str, str]):
    """Apply `key=value` lines to project.godot and put it back afterwards."""
    path = ROOT / "project.godot"
    original = path.read_text(encoding="utf-8")
    text = original
    for key, value in overrides.items():
        pattern = re.compile(r"^%s=.*$" % re.escape(key), re.MULTILINE)
        if not pattern.search(text):
            raise fail("project.godot declares no %s to override" % key)
        text = pattern.sub("%s=%s" % (key, value), text)
    path.write_text(text, encoding="utf-8")
    try:
        yield
    finally:
        path.write_text(original, encoding="utf-8")


def extension_libraries(html: str) -> list[str]:
    """The GDExtension libraries the exported page will try to load.

    An entry named twice loads the library twice, and the second load reports
    every class as already registered.
    """
    match = re.search(r'"gdextensionLibs"\s*:\s*\[(.*?)\]', html, re.DOTALL)
    if match is None:
        return []
    return re.findall(r'"([^"]+)"', match.group(1))


def validate_web(directory: Path) -> dict:
    """Every promised file, present, non-trivial, and of its declared format."""
    if not directory.is_dir():
        raise fail("no export directory at %s" % directory)
    missing = [name for name in WEB_REQUIRED if not (directory / name).is_file()]
    if missing:
        raise fail("the web export is missing %s" % ", ".join(missing))

    html = (directory / "index.html").read_text(encoding="utf-8", errors="replace")
    if "index.js" not in html:
        raise fail("index.html does not reference index.js, so the page loads nothing")

    libraries = extension_libraries(html)
    if not libraries:
        raise fail(
            "the page declares no GDExtension library, so the addon cannot load. "
            "Turn variant/extensions_support on in the export preset."
        )
    duplicates = sorted({name for name in libraries if libraries.count(name) > 1})
    if duplicates:
        raise fail(
            "the page loads %s more than once, which registers every class twice. "
            "A second .gdextension manifest is reachable under res://; give its "
            "directory a .gdignore." % ", ".join(duplicates)
        )

    side = directory / "index.side.wasm"
    required = list(WEB_REQUIRED) + list(libraries)
    if side.is_file():
        required.append(side.name)

    payload = {}
    for name in required + list(WEB_OPTIONAL):
        path = directory / name
        if not path.is_file():
            if name in WEB_OPTIONAL:
                continue
            raise fail("the web export is missing %s" % name)
        size = path.stat().st_size
        if size == 0:
            raise fail("%s is empty" % name)
        if name.endswith(".wasm") and path.read_bytes()[:4] != WASM_MAGIC:
            raise fail("%s does not start with the WebAssembly magic" % name)
        payload[name] = {"bytes": size, "sha256": sha256_file(path)}

    pack = directory / "index.pck"
    if pack.read_bytes()[:4] != PCK_MAGIC:
        raise fail("index.pck does not start with the Godot pack magic")
    print("EXPORT ok %d file(s), %d extension librar(ies), under %s" % (len(payload), len(libraries), directory))
    return payload


SLUG_PATTERN = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")


def load_examples(path: Path | None = None) -> dict:
    """The published set. Adding or removing a demo edits this file only."""
    catalog = load_json(path or EXAMPLES_FILE)
    entries = catalog.get("examples")
    if not entries:
        raise fail("%s publishes no examples" % (path or EXAMPLES_FILE))
    seen = set()
    for entry in entries:
        for key in ("slug", "title", "scene"):
            if not entry.get(key):
                raise fail("an example entry is missing %s" % key)
        slug = entry["slug"]
        if not SLUG_PATTERN.match(slug):
            raise fail("%s is not a url-safe slug, so it cannot be a subpath" % slug)
        if slug in seen:
            raise fail("%s is published twice, and the second would overwrite the first" % slug)
        seen.add(slug)
        if not entry["scene"].startswith("uid://"):
            raise fail("%s names its scene as %r, and only a uid:// survives a file move" % (slug, entry["scene"]))
    return catalog


def site_revision(declared: str = "") -> str:
    """What the footer credits the build to.

    A deployment knows the revision it resolved and checked out, so it says
    so. Asking git is the fallback for a local run, and a tree with no git
    at all still renders a page.
    """
    if declared:
        return declared[:12]
    try:
        return git_head()[:12]
    except Exception:
        return "unknown"


def landing_page(catalog: dict, revision: str = "") -> str:
    """The site index, linking every example the catalog publishes."""
    site = catalog.get("site", {})
    title = site.get("title", "Examples")
    buttons = []
    for entry in catalog["examples"]:
        buttons.append(
            '      <a class="btn" href="{slug}/">{title}</a>'.format(
                slug=entry["slug"],
                title=html_escape(entry["title"]),
            )
        )
    return LANDING_TEMPLATE.format(
        title=html_escape(title),
        buttons="\n".join(buttons),
        revision=html_escape(site_revision(revision)),
    )


TURN_HEADERS_KEY = "webrtc/turn_credentials_headers"
TURN_URL_KEY = "webrtc/turn_credentials_url"


def inject_turn_credentials(token: str, path: Path | None = None) -> str:
    """Write the auth header a deployed build presents for TURN credentials.

    The setting has no reader today: 5e8b58bc retired the GDScript WebRTC
    stack and `_ensure_ice_servers` did not cross with it, so a build falls
    back to the ice servers compiled into the transport. Injecting anyway
    keeps restoring that fetch from needing a CI change.
    """
    path = path or ROOT / "project.godot"
    text = path.read_text(encoding="utf-8")
    line = '%s=PackedStringArray("X-Turn-Auth-Token: %s")' % (TURN_HEADERS_KEY, token)
    if re.search(r"^%s=" % re.escape(TURN_HEADERS_KEY), text, flags=re.MULTILINE):
        text = re.sub(r"^%s=.*$" % re.escape(TURN_HEADERS_KEY), lambda _: line, text, flags=re.MULTILINE)
    else:
        anchor = re.search(r"^%s=.*$" % re.escape(TURN_URL_KEY), text, flags=re.MULTILINE)
        if anchor is None:
            raise fail("project.godot declares no %s to anchor the header to" % TURN_URL_KEY)
        text = text[: anchor.end()] + "\n" + line + text[anchor.end() :]
    path.write_text(text, encoding="utf-8")
    print("TURN wrote %s" % TURN_HEADERS_KEY)
    return text


def html_escape(text: str) -> str:
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


def export_web(
    godot: str,
    *,
    preset: str,
    scene: str | None,
    out: Path,
    templates: Path | None,
    timeout: float,
) -> dict:
    overrides: dict[str, str] = {"enabled": "PackedStringArray()"}
    if scene:
        overrides["run/main_scene"] = '"%s"' % scene
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    env = {}
    if templates is not None:
        home = templates.parent / "home"
        (home / ".local" / "share" / "godot").mkdir(parents=True, exist_ok=True)
        link = home / ".local" / "share" / "godot" / "export_templates"
        if link.is_symlink() or link.exists():
            if link.is_symlink():
                link.unlink()
            else:
                shutil.rmtree(link)
        link.symlink_to(templates)
        env["HOME"] = str(home)

    with project_overrides(overrides):
        run(
            [godot, "--headless", "--path", str(ROOT), "--import"],
            timeout=timeout,
            check=False,
            env=env,
        )
        completed = run(
            [
                godot,
                "--headless",
                "--verbose",
                "--path",
                str(ROOT),
                "--export-release",
                preset,
                str(out / "index.html"),
            ],
            timeout=timeout,
            check=False,
            env=env,
        )
    if completed.returncode != 0:
        print("::warning::godot exited %d during export; the payload check decides" % completed.returncode)
    return validate_web(out)


class _Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        for key, value in CROSS_ORIGIN_HEADERS.items():
            self.send_header(key, value)
        super().end_headers()

    def log_message(self, *args):
        del args


class _Server(socketserver.TCPServer):
    allow_reuse_address = True


@contextlib.contextmanager
def serving(directory: Path):
    handler = functools.partial(_Handler, directory=str(directory))
    with _Server(("127.0.0.1", 0), handler) as httpd:
        thread = threading.Thread(target=httpd.serve_forever, daemon=True)
        thread.start()
        try:
            yield "http://127.0.0.1:%d/index.html" % httpd.server_address[1]
        finally:
            httpd.shutdown()


def find_browser() -> str | None:
    for name in ("chromium", "google-chrome-stable", "google-chrome", "chromium-browser"):
        found = shutil.which(name)
        if found:
            return found
    return None


def smoke_web(directory: Path, *, seconds: float) -> dict:
    """Boot the exported page and read what the engine says about itself."""
    browser = find_browser()
    if browser is None:
        raise fail("no chromium or chrome on PATH, so the page cannot be booted here")
    validate_web(directory)
    with tempfile.TemporaryDirectory() as profile:
        with serving(directory) as url:
            completed = subprocess.run(
                [
                    browser,
                    "--headless=new",
                    "--no-sandbox",
                    "--use-angle=swiftshader",
                    "--enable-unsafe-swiftshader",
                    "--enable-logging=stderr",
                    "--v=0",
                    "--user-data-dir=%s" % profile,
                    "--virtual-time-budget=%d" % int(seconds * 1000),
                    "--dump-dom",
                    url,
                ],
                capture_output=True,
                text=True,
                timeout=seconds + 180,
            )
    log = (completed.stdout or "") + (completed.stderr or "")
    console = [line for line in log.splitlines() if "INFO:CONSOLE" in line and "chrome-extension://" not in line]
    banner = next((line for line in console if "Godot Engine v" in line), "")
    configuration = next((line for line in console if "Build configuration:" in line), "")
    engine_errors = [line for line in console if "ERROR: " in line]
    notice = re.search(
        r'id="status-notice"[^>]*style="display: block;"[^>]*>(.*?)</div>',
        log,
        re.DOTALL,
    )

    print("BROWSER %s" % Path(browser).name)
    if banner:
        print("BOOT %s" % banner.split('"', 1)[-1].rsplit('", source', 1)[0])
    if configuration:
        print("BUILD %s" % configuration.split("Build configuration: ", 1)[-1].rsplit('", source', 1)[0])
    if notice:
        raise fail("the page showed the failure notice %r" % notice.group(1).strip()[:200])
    if engine_errors:
        for line in engine_errors[:10]:
            print(line)
        raise fail("the engine reported %d error(s) in the browser" % len(engine_errors))
    if not banner:
        print("\n".join(console[-30:]) or log[-4000:])
        raise fail("the page never printed the engine banner, so it did not start")
    if "GDExtension support" not in configuration:
        raise fail("the template that ran does not carry GDExtension support")
    return {"browser": browser, "banner": banner, "configuration": configuration}


def stage_cloudflare(directory: Path, *, limit: int) -> dict:
    """Brotli every file over the per-file limit, and route to the copy.

    Cloudflare Pages refuses a file over 25 MiB. Which files cross that line
    depends on the export: an extension-enabled build splits the engine into
    a small loader and a large side module, so the name to compress is not
    always `index.wasm`.

    `_headers` and `_redirects` are read from the deploy root only, so every
    rule is written root-relative and a site holding one export per subpath
    is staged by one call over the whole tree.
    """
    if not shutil.which("brotli"):
        raise fail("brotli is not on PATH")
    oversized = sorted(
        path for path in directory.rglob("*") if path.is_file() and path.stat().st_size > limit and path.suffix != ".br"
    )
    if not oversized:
        print("CLOUDFLARE nothing exceeds %d bytes" % limit)
    redirects = []
    headers = [
        "/*",
        "  Cross-Origin-Opener-Policy: same-origin",
        "  Cross-Origin-Embedder-Policy: require-corp",
        "",
    ]
    for path in oversized:
        run(["brotli", "-f", "-q", "11", str(path)], timeout=1800)
        compressed = path.with_name(path.name + ".br")
        if not compressed.is_file():
            raise fail("brotli produced no %s" % compressed.name)
        path.unlink()
        route = "/" + path.relative_to(directory).as_posix()
        redirects.append("%s %s.br 200" % (route, route))
        headers.extend(
            [
                route,
                "  Content-Encoding: br",
                "  Content-Type: %s" % ("application/wasm" if path.suffix == ".wasm" else "application/octet-stream"),
                "",
            ]
        )
        print("CLOUDFLARE %s -> %s.br" % (route, route))
    (directory / "_redirects").write_text("\n".join(redirects) + "\n", encoding="utf-8")
    (directory / "_headers").write_text("\n".join(headers) + "\n", encoding="utf-8")
    return {"compressed": [path.relative_to(directory).as_posix() for path in oversized]}


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--godot", default=os.environ.get("GODOT", "godot"))
    parser.add_argument("--timeout", type=float, default=1800.0)
    sub = parser.add_subparsers(dest="action", required=True)

    web = sub.add_parser("web")
    web.add_argument("--preset", default="Web")
    web.add_argument("--scene", default="")
    web.add_argument("--out", type=Path, default=ROOT / "build" / "web")
    web.add_argument("--templates", type=Path)
    web.add_argument("--record", type=Path)

    check = sub.add_parser("validate")
    check.add_argument("--dir", type=Path, default=ROOT / "build" / "web")

    boot = sub.add_parser("smoke")
    boot.add_argument("--dir", type=Path, default=ROOT / "build" / "web")
    boot.add_argument("--seconds", type=float, default=180.0)

    pages = sub.add_parser("cloudflare")
    pages.add_argument("--dir", type=Path, default=ROOT / "build" / "web")
    pages.add_argument("--limit", type=int, default=25 * 1024 * 1024)

    listing = sub.add_parser("examples")
    listing.add_argument("--catalog", type=Path, default=EXAMPLES_FILE)
    listing.add_argument("--format", choices=("json", "github"), default="json")

    site = sub.add_parser("site")
    site.add_argument("--catalog", type=Path, default=EXAMPLES_FILE)
    site.add_argument("--dir", type=Path, default=ROOT / "build" / "site")
    site.add_argument("--revision", default="")

    turn = sub.add_parser("turn")
    turn.add_argument("--token", required=True)
    turn.add_argument("--project", type=Path, default=ROOT / "project.godot")
    return parser


def run_cli(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.action == "web":
        payload = export_web(
            args.godot,
            preset=args.preset,
            scene=args.scene or None,
            out=args.out,
            templates=args.templates,
            timeout=args.timeout,
        )
        if args.record:
            write_json(
                args.record,
                {
                    "source": {"sha": git_head()},
                    "preset": args.preset,
                    "scene": args.scene,
                    "payload": payload,
                    "exported": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                },
            )
        return 0
    if args.action == "validate":
        validate_web(args.dir)
        return 0
    if args.action == "cloudflare":
        print(json.dumps(stage_cloudflare(args.dir, limit=args.limit), indent=2))
        return 0
    if args.action == "turn":
        inject_turn_credentials(args.token, args.project)
        return 0
    if args.action == "examples":
        catalog = load_examples(args.catalog)
        if args.format == "json":
            print(json.dumps(catalog["examples"], indent=2))
            return 0
        encoded = json.dumps(catalog["examples"], separators=(",", ":"))
        print("examples=" + encoded)
        github_output(examples=encoded, count=str(len(catalog["examples"])))
        return 0
    if args.action == "site":
        catalog = load_examples(args.catalog)
        missing = [entry["slug"] for entry in catalog["examples"] if not (args.dir / entry["slug"]).is_dir()]
        if missing:
            raise fail("the catalog publishes %s and %s holds no such directory" % (", ".join(missing), args.dir))
        for entry in catalog["examples"]:
            validate_web(args.dir / entry["slug"])
        args.dir.mkdir(parents=True, exist_ok=True)
        (args.dir / "index.html").write_text(landing_page(catalog, args.revision), encoding="utf-8")
        print("SITE %d example(s) under %s" % (len(catalog["examples"]), args.dir))
        return 0
    print(json.dumps(smoke_web(args.dir, seconds=args.seconds), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(lambda: run_cli()))
