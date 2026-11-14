// `npm run build` makes a static site in out/, which tools/songs.py serves at http://localhost:47330 together with
// its API. `npm run dev` (port 4310) hot-reloads and forwards /api to that server, so run ./serve-songs.sh as well.
const dev = process.env.NODE_ENV !== "production";

/** @type {import('next').NextConfig} */
const nextConfig = {
  reactStrictMode: true,
  ...(dev
    ? { rewrites: async () => [{ source: "/api/:path*", destination: "http://127.0.0.1:47330/api/:path*" }] }
    : { output: "export", images: { unoptimized: true } }),
};

export default nextConfig;
