# Tier 2 dev loop: expose local services through a Cloudflare tunnel so the real
# Discord iframe (served from <client_id>.discordsays.com) can reach them. The
# proxy cannot see localhost and CSP forbids ws://localhost, so the page, Nakama,
# and token Worker must be public.
#
# This is a thin convenience wrapper. It does NOT replace the Discord dev-portal
# URL Mappings, which must point:
#   /                 -> the page tunnel (or Cloudflare Pages)
#   /token            -> the token Worker tunnel (or deployed Worker)
#   /<nakama-prefix>  -> the Nakama tunnel
# and OAuth redirect https://127.0.0.1.
#
#   pwsh addons/networked_activity/scripts/dev_tunnel.ps1 -LocalUrl http://localhost:7350
param(
    [Parameter(Mandatory = $true)][string]$LocalUrl
)

if (-not (Get-Command cloudflared -ErrorAction SilentlyContinue)) {
    throw "cloudflared not found on PATH. Install it: https://developers.cloudflare.com/cloudflared/"
}

Write-Host "Opening a quick tunnel to $LocalUrl ..."
Write-Host "Copy the printed https://<random>.trycloudflare.com URL into the Discord URL Mapping."
& cloudflared tunnel --url $LocalUrl
