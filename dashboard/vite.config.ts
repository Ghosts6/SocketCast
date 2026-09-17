/// <reference types="vitest/config" />
import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      "/api": "http://localhost:8000",
      "/ws": { target: "ws://localhost:8000", ws: true },
    },
  },
  test: {
    environment: "jsdom",
    // Needed for @testing-library/react's auto-cleanup, which registers via
    // a global afterEach and silently no-ops without it.
    globals: true,
    setupFiles: ["./src/test/setup.ts"],
  },
});
