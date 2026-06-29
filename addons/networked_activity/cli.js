#!/usr/bin/env node

/**
 * Unified, cross-platform orchestrator for building, serving, and tunneling
 * the Discord Activity web build.
 */

const { spawn } = require("child_process");
const fs = require("fs").promises;
const path = require("path");
const zlib = require("zlib");

// Helper to print help documentation
function printHelp() {
  console.log(`
Usage: node cli.js [options]

Options:
  --godot <path>       Path to the Godot executable (default: "godot")
  --main-scene <uid>   Swap run/main_scene to this specific UID/path in
                       project.godot during build
  --out-dir <path>     Output directory for the web build (default: "build/web")
  --port <number>      Port to serve the web build on (default: 8000)
  --skip-build         Skip the export and compression phases; serve existing
                       build
  --tunnel             Spawn a cloudflared tunnel to expose the local server
                       (Tier 2)
  --build-only         Run the build and compress phases, then exit without
                       serving
  --project <name>     Cloudflare Pages project name. Providing this
                       automatically deploys the build
  --help, -h           Show this help message
  `);
}

// Simple zero-dependency argument parser
const args = process.argv.slice(2);
const options = {
  godot: "godot",
  mainScene: "",
  outDir: "build/web",
  port: 8000,
  skipBuild: false,
  tunnel: false,
  buildOnly: false,
  project: "",
  help: false
};

for (let i = 0; i < args.length; i++) {
  const arg = args[i];
  if (arg === "--godot") {
    options.godot = args[++i];
  } else if (arg === "--main-scene") {
    options.mainScene = args[++i];
  } else if (arg === "--out-dir") {
    options.outDir = args[++i];
  } else if (arg === "--port") {
    options.port = parseInt(args[++i], 10);
  } else if (arg === "--skip-build") {
    options.skipBuild = true;
  } else if (arg === "--tunnel") {
    options.tunnel = true;
  } else if (arg === "--build-only") {
    options.buildOnly = true;
  } else if (arg === "--project") {
    options.project = args[++i];
  } else if (arg === "--help" || arg === "-h") {
    options.help = true;
  } else {
    console.error(`Unknown argument: ${arg}`);
    printHelp();
    process.exit(1);
  }
}

if (options.help) {
  printHelp();
  process.exit(0);
}

// Resolve paths relative to the repository root
// The script resides at: <repo_root>/addons/networked_activity/cli.js
const REPO_ROOT = path.resolve(__dirname, "../..");
const PROJECT_GODOT = path.join(REPO_ROOT, "project.godot");
const ABS_OUT_DIR = path.isAbsolute(options.outDir)
  ? options.outDir
  : path.join(REPO_ROOT, options.outDir);

// Spawn a child process and return a promise
function runProcess(command, args, spawnOpts = {}) {
  return new Promise((resolve, reject) => {
    const proc = spawn(command, args, {
      cwd: REPO_ROOT,
      stdio: "inherit",
      shell: true, // Required for executing batch/cmd files on Windows
      ...spawnOpts
    });

    proc.on("close", (code) => {
      resolve(code);
    });

    proc.on("error", (err) => {
      reject(err);
    });
  });
}

// Compress index.wasm using Brotli
async function compressWasm(wasmPath) {
  const brPath = wasmPath + ".br";
  console.log(`==> Compressing ${wasmPath} using Brotli...`);

  const wasmData = await fs.readFile(wasmPath);
  const compressed = zlib.brotliCompressSync(wasmData, {
    params: {
      [zlib.constants.BROTLI_PARAM_QUALITY]: 5
    }
  });

  await fs.writeFile(brPath, compressed);
  await fs.unlink(wasmPath);

  const origSize = (wasmData.length / 1024 / 1024).toFixed(2);
  const compSize = (compressed.length / 1024 / 1024).toFixed(2);
  console.log(
    `==> Brotli compression complete: ${origSize} MB -> ${compSize} MB`
  );
}

// Swaps the main scene in project.godot, executes the action,
// then guarantees restoration
async function withSwappedScene(action) {
  if (!options.mainScene) {
    await action();
    return;
  }

  console.log(`==> Reading project.godot to swap main scene...`);
  const originalContent = await fs.readFile(PROJECT_GODOT, "utf-8");

  // Replace the run/main_scene configuration line
  const swappedContent = originalContent.replace(
    /^run\/main_scene=.*/m,
    `run/main_scene="${options.mainScene}"`
  );

  await fs.writeFile(PROJECT_GODOT, swappedContent, "utf-8");
  console.log(`==> Swapped main scene to: ${options.mainScene}`);

  try {
    await action();
  } finally {
    await fs.writeFile(PROJECT_GODOT, originalContent, "utf-8");
    console.log("==> Restored original project.godot configuration");
  }
}

async function build() {
  await fs.mkdir(ABS_OUT_DIR, { recursive: true });

  await withSwappedScene(async () => {
    console.log("==> Running Godot import...");
    await runProcess(options.godot, ["--headless", "--import"]).catch((err) => {
      if (err.code === "ENOENT") {
        throw new Error(
          `Could not find the Godot executable ("${options.godot}").\n` +
            "Please ensure Godot is installed and on your system PATH, " +
            "or specify its path explicitly using the --godot option.\n" +
            'Example: node cli.js --godot "C:\\path\\to\\godot.exe"'
        );
      }
      throw err;
    });

    console.log("==> Exporting project to Web preset...");
    const htmlPath = path.join(ABS_OUT_DIR, "index.html");

    // Godot 4 headless export may segfault on teardown (engine bug),
    // but still writes its artifacts. We check for the presence of
    // the files afterward rather than relying solely on the exit code.
    const code = await runProcess(options.godot, [
      "--headless",
      "--verbose",
      "--export-release",
      "Web",
      htmlPath
    ]).catch((err) => {
      if (err.code === "ENOENT") {
        throw new Error(
          `Could not find the Godot executable ("${options.godot}").\n` +
            "Please ensure Godot is installed and on your system PATH, " +
            "or specify its path explicitly using the --godot option.\n" +
            'Example: node cli.js --godot "C:\\path\\to\\godot.exe"'
        );
      }
      throw err;
    });

    const wasmPath = path.join(ABS_OUT_DIR, "index.wasm");
    const pckPath = path.join(ABS_OUT_DIR, "index.pck");

    const hasWasm = await fs
      .stat(wasmPath)
      .then((s) => s.size > 0)
      .catch(() => false);
    const hasPck = await fs
      .stat(pckPath)
      .then((s) => s.size > 0)
      .catch(() => false);

    if (hasWasm && hasPck) {
      if (code !== 0) {
        console.log(
          `==> Warning: Godot exited with code ${code} ` +
            "but export artifacts are present."
        );
      }
    } else {
      throw new Error(
        `Web export did not produce its expected artifacts ` +
          `(index.wasm and index.pck) in ${ABS_OUT_DIR}.\n\n` +
          "Please verify that:\n" +
          '1. You have a Web export preset named exactly "Web" ' +
          "configured in your Godot project (Project > Export).\n" +
          '2. The preset\'s Export Path is set to a file named ' +
          'exactly "index.html" (e.g. "build/web/index.html").'
      );
    }
  });

  // Brotli compress index.wasm to bypass the 25 MiB Cloudflare limit
  const wasmPath = path.join(ABS_OUT_DIR, "index.wasm");
  await compressWasm(wasmPath);

  // Write Cloudflare redirects and headers
  console.log("==> Writing Cloudflare _redirects and _headers...");
  await fs.writeFile(
    path.join(ABS_OUT_DIR, "_redirects"),
    "/index.wasm /index.wasm.br 200\n",
    "utf-8"
  );

  const headersContent = `/*
  Cross-Origin-Opener-Policy: same-origin
  Cross-Origin-Embedder-Policy: require-corp

/index.wasm
  Content-Encoding: br
  Content-Type: application/wasm
`;
  await fs.writeFile(
    path.join(ABS_OUT_DIR, "_headers"),
    headersContent,
    "utf-8"
  );
  console.log("==> Build successfully completed.");
}

async function serve() {
  const activeProcesses = [];

  // Handle clean termination of background processes
  const cleanup = () => {
    console.log("\n==> Terminating background services...");
    for (const proc of activeProcesses) {
      proc.kill("SIGINT");
    }
    process.exit(0);
  };

  // 1. Start cloudflared tunnel if requested (Tier 2)
  if (options.tunnel) {
    console.log(
      `==> Starting cloudflared tunnel to http://localhost:${options.port}...`
    );
    const tunnel = spawn(
      "cloudflared",
      ["tunnel", "--url", `http://localhost:${options.port}`],
      {
        cwd: REPO_ROOT,
        shell: true
      }
    );
    activeProcesses.push(tunnel);

    tunnel.on("error", (err) => {
      if (err.code === "ENOENT") {
        console.error(
          `\nError: Could not find "cloudflared" on your system PATH.\n` +
            "The --tunnel option requires Cloudflare Tunnel. " +
            "Please install it from:\n" +
            "  https://developers.cloudflare.com/cloudflare-one/" +
            "connections/connect-networks/downloads/\n"
        );
      } else {
        console.error(`[Tunnel Error] ${err.message}`);
      }
      cleanup();
    });

    tunnel.stdout.on("data", (data) => {
      const lines = data.toString().split("\n");
      for (const line of lines) {
        if (line.trim()) console.log(`[Tunnel] ${line.trim()}`);
      }
    });

    tunnel.stderr.on("data", (data) => {
      const lines = data.toString().split("\n");
      for (const line of lines) {
        if (line.trim()) {
          // Highlight the trycloudflare link to make it easy to copy
          if (line.includes(".trycloudflare.com")) {
            console.log(
              `\n============================================================`
            );
            console.log(`[Tunnel Link] ${line.trim()}`);
            console.log(
              `============================================================\n`
            );
          } else {
            console.log(`[Tunnel Debug] ${line.trim()}`);
          }
        }
      }
    });
  }

  // 2. Start wrangler pages dev server
  console.log(
    `==> Serving ${options.outDir} on http://localhost:${options.port} ` +
      "(Ctrl+C to stop)..."
  );
  const wrangler = spawn(
    "npx",
    [
      "wrangler",
      "pages",
      "dev",
      options.outDir,
      "--port",
      options.port.toString()
    ],
    {
      cwd: REPO_ROOT,
      shell: true
    }
  );
  activeProcesses.push(wrangler);

  wrangler.on("error", (err) => {
    if (err.code === "ENOENT") {
      console.error(
        `\nError: Could not find "npx" on your system PATH.\n` +
          "Please install Node.js and npm (which includes npx) " +
          "from https://nodejs.org/ to serve the activity.\n"
      );
    } else {
      console.error(`[Wrangler Error] ${err.message}`);
    }
    cleanup();
  });

  wrangler.stdout.on("data", (data) => {
    const lines = data.toString().split("\n");
    for (const line of lines) {
      if (line.trim()) console.log(`[Wrangler] ${line.trim()}`);
    }
  });

  wrangler.stderr.on("data", (data) => {
    const lines = data.toString().split("\n");
    for (const line of lines) {
      if (line.trim()) console.error(`[Wrangler Err] ${line.trim()}`);
    }
  });

  process.on("SIGINT", cleanup);
  process.on("SIGTERM", cleanup);
}

async function main() {
  try {
    if (!options.skipBuild) {
      await build();
    } else {
      // If skipping build, ensure the index.html actually exists in the out-dir
      const htmlPath = path.join(ABS_OUT_DIR, "index.html");
      const exists = await fs
        .stat(htmlPath)
        .then(() => true)
        .catch(() => false);
      if (!exists) {
        throw new Error(
          `No build found at ${ABS_OUT_DIR}. Run without --skip-build first.`
        );
      }
    }

    if (options.project) {
      console.log(
        `==> Deploying ${options.outDir} to Cloudflare Pages ` +
          `project "${options.project}"...`
      );
      const deployCode = await runProcess("npx", [
        "wrangler",
        "pages",
        "deploy",
        options.outDir,
        `--project-name=${options.project}`
      ]).catch((err) => {
        if (err.code === "ENOENT") {
          throw new Error(
            `Could not find "npx" on your system PATH.\n` +
              "Please install Node.js and npm from https://nodejs.org/ " +
              "to deploy the activity."
          );
        }
        throw err;
      });

      if (deployCode !== 0) {
        throw new Error(
          `Cloudflare Pages deployment failed with exit code ${deployCode}`
        );
      }
      console.log("==> Deployment successfully completed.");
    }

    if (!options.buildOnly && !options.project) {
      await serve();
    }
  } catch (error) {
    console.error("==> Orchestrator error:\n", error.message);
    process.exit(1);
  }
}

main();
