import type { Metadata } from "next";
import "./globals.css";
import { ToastProvider } from "@/components/ui/Toast";
import { ScrollProgress } from "@/components/ui/ScrollProgress";

export const metadata: Metadata = {
  title: "Bitboard Chess Engine",
  description: "Play, train, and analyze with the Bitboard C++23 chess engine.",
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en">
      <body>
        <ScrollProgress />
        <ToastProvider>
          {children}
        </ToastProvider>
      </body>
    </html>
  );
}
