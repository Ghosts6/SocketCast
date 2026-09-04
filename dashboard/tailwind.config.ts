import type { Config } from "tailwindcss";

export default {
  content: ["./index.html", "./src/**/*.{ts,tsx}"],
  darkMode: "class",
  theme: {
    extend: {
      colors: {
        neutral: {
          750: "#1f1f2e",
        },
      },
    },
  },
  plugins: [],
} satisfies Config;
