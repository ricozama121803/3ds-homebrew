import type { Metadata } from "next";
import { Geist, Geist_Mono } from "next/font/google";
import "./globals.css";

const geistSans = Geist({ variable: "--font-geist-sans", subsets: ["latin"] });
const geistMono = Geist_Mono({ variable: "--font-geist-mono", subsets: ["latin"] });

export const metadata: Metadata = {
  title: "BeatPad Song Studio",
  description: "Turn any song into a BeatPad chart and send it to your 3DS over Wi-Fi.",
  icons: {
    icon: "data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'%3E%3Cellipse cx='16' cy='17' rx='14' ry='9' fill='%235a2fd0'/%3E%3Cellipse cx='16' cy='15' rx='12' ry='7.5' fill='%237c4dff'/%3E%3Cellipse cx='13' cy='12.5' rx='5' ry='2.2' fill='%23c9b8ff'/%3E%3C/svg%3E",
  },
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en" className="dark">
      <body className={`${geistSans.variable} ${geistMono.variable} min-h-screen bg-neutral-950 font-sans text-neutral-200 antialiased`}>
        {children}
      </body>
    </html>
  );
}
