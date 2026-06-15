import type { Metadata } from "next";
import { Inter } from "next/font/google";
import "./globals.css";
import { ToastProvider } from "@/components/ui/Toast";
import { ScrollProgress } from "@/components/ui/ScrollProgress";

const inter = Inter({ subsets: ["latin"] });

export const metadata: Metadata = {
  title: "C++ Chess Engine",
  description: "Showcase of a custom bitboard-based C++ UCI chess engine.",
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en">
      <body className={inter.className}>
        <ScrollProgress />
        <ToastProvider>
          {children}
        </ToastProvider>
      </body>
    </html>
  );
}
