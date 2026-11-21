import type { Metadata } from "next";
import "./globals.css";
export const metadata: Metadata = { title: "IRAutoX", description: "IRAutoX Multiplayer Launcher" };
export default function RootLayout({ children }: Readonly<{ children: React.ReactNode }>) { return <html lang="en"><body>{children}</body></html>; }
