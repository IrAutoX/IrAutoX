import type { Config } from "tailwindcss";
export default { content: ["./app/**/*.{ts,tsx}"], theme: { extend: { colors: { ink: "#090a0f", panel: "#11131b", gold: "#d8b46a", muted: "#8d93a4" } } }, plugins: [] } satisfies Config;
