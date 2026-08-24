import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// TODO(Phase 6): proxy /api and /ws to the control-plane service in dev.
export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      "/api": "http://localhost:8000",
      "/ws": { target: "ws://localhost:8000", ws: true },
    },
  },
});
