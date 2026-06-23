# Discord Activity with Cloudflare

This is a simple Discord Activity application that uses Cloudflare Workers and Cloudflare Pages.

# Architecture

## Client

- [Vite](https://vitejs.dev/) with React and TypeScript
- Cloudflare Pages

## Server

- [Hono](https://hono.dev/) with TypeScript
- Cloudflare Workers

# Setup

## Discord Application

Go to [Discord Developer Portal](https://discord.com/developers/applications) and create a new application. There will be a client ID and client secret that you will need to fill in the environment variables.

## Environment Variables

For client side, fill in the environment variables in `client/.env.example` and rename it to `client/.env`.

```
VITE_CLIENT_ID=
```

Server is using `wrangler.toml` for environment variables. Create a `wrangler.toml` file in the `server` directory with the following content:

```toml
name = "discord-activity-server"

[vars]
CLIENT_ID = ""
CLIENT_SECRET = ""
```

## Wrangler

Install [Wrangler](https://developers.cloudflare.com/workers/cli-wrangler/install-update) and login to your Cloudflare account.

## Running the Application

Go to the `client` directory and run the following commands:

```bash
npm install
npm run build
npm run deploy
```

This will install the dependencies, build the app locally and deploy the frontend application files to Cloudflare Pages.

Go to the `server` directory and run the following commands:

```bash
npm install
npm run deploy
```

This will install the dependencies and deploy minified server code to Cloudflare Workers. You should see the Worker URL after the deployment.

## Cloudflare

Open [Cloudflare](https://dash.cloudflare.com) and go to Workers & Pages section.
Check URL for the frontend application and Worker URL for the backend application.


Frontend application URL will be in the format of `https://<subdomain>.pages.dev` and backend application URL will be in the format of `https://<subdomain>.workers.dev`.
You need to add these URLs to the Discord application URL Mappings.

| Prefix  | Target |
| ------------- | ------------- |
| / (Root Mapping)  | ...pages.dev |
| /api  | ...workers.dev  |

Remember to add the OAuth2 redirect URL to the Discord application.

After setting up the environment variables and running the application, you can visit the frontend application URL and start using your Discord Activity.
