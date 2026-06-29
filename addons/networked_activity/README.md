# Networked Activity Addon

A Godot 4 addon for building, serving, and deploying Discord Activities.

## Developer CLI (`cli.js`)

The `cli.js` tool at the root of the addon automates the build, compression, 
local serving, and deployment pipelines.

```bash
# Local Testing (Builds and serves locally on port 8000)
node addons/networked_activity/cli.js

# Local testing with a Cloudflare Tunnel (Exposes local port to the internet)
node addons/networked_activity/cli.js --tunnel

# Specific Scene Swap (Builds and serves a specific test scene)
node addons/networked_activity/cli.js --main-scene "res://path/to/scene.tscn"

# Direct Cloudflare Deploy (Builds, Brotli-compresses, and publishes)
node addons/networked_activity/cli.js --project "your-pages-project"
```

---

## Manual Execution (Without the script)

If you prefer not to use `cli.js`, you can run the underlying tools manually.

### Build and Brotli Compress
Export the project using your configured "Web" preset and compress the WASM 
binary to bypass Cloudflare's 25 MiB file limit:

```bash
# Export the project
godot --headless --export-release "Web" build/web/index.html

# Compress WASM using Brotli and remove the raw file
brotli -f -q 5 build/web/index.wasm
rm build/web/index.wasm

# Write the redirects mapping rule
echo "/index.wasm /index.wasm.br 200" > build/web/_redirects

# Write the security and compression headers
echo -e "/*\n  Cross-Origin-Opener-Policy: same-origin\n  Cross-Origin-Embedder-Policy: require-corp\n\n/index.wasm\n  Content-Encoding: br\n  Content-Type: application/wasm" > build/web/_headers
```

### Serve Locally
Serve the build directory using Wrangler to correctly resolve the Brotli 
redirections and COOP/COEP headers:
```bash
npx wrangler pages dev build/web --port 8000
```

### Expose Local Server (Tunnel)
Expose your local port through a public tunnel to test inside the Discord client:
```bash
cloudflared tunnel --url http://localhost:8000
```

### Deploy to Cloudflare Pages
Publish your build directory directly to your Cloudflare Pages project:
```bash
npx wrangler pages deploy build/web --project-name="your-pages-project"
```
