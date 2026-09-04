import { useEffect, useRef, useState } from "react";
import { useTheme } from "../contexts/ThemeContext";

interface Particle {
  x: number;
  y: number;
  vx: number;
  vy: number;
  radius: number;
  opacity: number;
  shape: "circle" | "square" | "triangle";
  color: string;
  rotation: number;
  rotationSpeed: number;
}

const LIGHT_COLORS = ["rgba(37, 99, 235,", "rgba(59, 130, 246,", "rgba(99, 102, 241,"];
const DARK_COLORS = ["rgba(59, 130, 246,", "rgba(139, 92, 246,", "rgba(168, 85, 247,"];

export function ParticleBackground() {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const mouseRef = useRef({ x: 0, y: 0 });
  const particlesRef = useRef<Particle[]>([]);
  const [fps, setFps] = useState(0);
  const frameCountRef = useRef(0);
  const lastTimeRef = useRef(Date.now());
  const { theme } = useTheme();

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const resizeCanvas = () => {
      canvas.width = window.innerWidth;
      canvas.height = window.innerHeight;
    };

    resizeCanvas();
    window.addEventListener("resize", resizeCanvas);

    // Create particles with variety
    const colors = theme === "dark" ? DARK_COLORS : LIGHT_COLORS;
    particlesRef.current = Array.from({ length: 100 }, () => {
      const shapes: Array<"circle" | "square" | "triangle"> = [
        "circle",
        "circle",
        "circle",
        "square",
        "triangle",
      ];
      return {
        x: Math.random() * canvas.width,
        y: Math.random() * canvas.height,
        vx: (Math.random() - 0.5) * 0.8,
        vy: (Math.random() - 0.5) * 0.8,
        radius: Math.random() * 3 + 1,
        opacity: Math.random() * 0.6 + 0.15,
        shape: shapes[Math.floor(Math.random() * shapes.length)],
        color: colors[Math.floor(Math.random() * colors.length)],
        rotation: Math.random() * Math.PI * 2,
        rotationSpeed: (Math.random() - 0.5) * 0.05,
      };
    });

    const handleMouseMove = (e: MouseEvent) => {
      mouseRef.current = { x: e.clientX, y: e.clientY };
    };

    window.addEventListener("mousemove", handleMouseMove);

    let animationFrameId: number;

    const drawShape = (
      ctx: CanvasRenderingContext2D,
      x: number,
      y: number,
      radius: number,
      shape: "circle" | "square" | "triangle",
      rotation: number
    ) => {
      ctx.save();
      ctx.translate(x, y);
      ctx.rotate(rotation);

      switch (shape) {
        case "circle":
          ctx.beginPath();
          ctx.arc(0, 0, radius, 0, Math.PI * 2);
          ctx.fill();
          break;
        case "square":
          ctx.fillRect(-radius, -radius, radius * 2, radius * 2);
          break;
        case "triangle":
          ctx.beginPath();
          ctx.moveTo(0, -radius);
          ctx.lineTo(radius, radius);
          ctx.lineTo(-radius, radius);
          ctx.closePath();
          ctx.fill();
          break;
      }

      ctx.restore();
    };

    const animate = () => {
      const now = Date.now();
      const delta = now - lastTimeRef.current;

      if (delta >= 16) {
        frameCountRef.current++;
        if (delta >= 1000) {
          setFps(frameCountRef.current);
          frameCountRef.current = 0;
          lastTimeRef.current = now;
        } else {
          lastTimeRef.current += 16;
        }

        // Background gradient based on theme
        const isDark = theme === "dark";
        const gradient = ctx.createLinearGradient(0, 0, canvas.width, canvas.height);
        if (isDark) {
          gradient.addColorStop(0, "#0a0a0a");
          gradient.addColorStop(0.5, "#1a1a2e");
          gradient.addColorStop(1, "#16213e");
        } else {
          gradient.addColorStop(0, "#f5f5f5");
          gradient.addColorStop(0.5, "#ebebeb");
          gradient.addColorStop(1, "#e0e0e0");
        }
        ctx.fillStyle = gradient;
        ctx.fillRect(0, 0, canvas.width, canvas.height);

        const particles = particlesRef.current;
        const mouseX = mouseRef.current.x;
        const mouseY = mouseRef.current.y;

        particles.forEach((particle) => {
          const dx = mouseX - particle.x;
          const dy = mouseY - particle.y;
          const distance = Math.sqrt(dx * dx + dy * dy);
          const maxDistance = 200;

          if (distance < maxDistance) {
            const force = (1 - distance / maxDistance) * 0.06;
            particle.vx += (dx / distance) * force;
            particle.vy += (dy / distance) * force;
          }

          particle.vx *= 0.97;
          particle.vy *= 0.97;
          particle.rotation += particle.rotationSpeed;

          particle.x += particle.vx;
          particle.y += particle.vy;

          if (particle.x < -particle.radius) particle.x = canvas.width + particle.radius;
          if (particle.x > canvas.width + particle.radius) particle.x = -particle.radius;
          if (particle.y < -particle.radius) particle.y = canvas.height + particle.radius;
          if (particle.y > canvas.height + particle.radius) particle.y = -particle.radius;

          // Draw particle with shape
          ctx.fillStyle = particle.color + particle.opacity + ")";
          ctx.globalAlpha = particle.opacity;
          drawShape(ctx, particle.x, particle.y, particle.radius, particle.shape, particle.rotation);
          ctx.globalAlpha = 1;

          // Draw connections to nearby particles
          particles.forEach((other) => {
            const pdx = other.x - particle.x;
            const pdy = other.y - particle.y;
            const pdist = Math.sqrt(pdx * pdx + pdy * pdy);
            if (pdist < 120) {
              const lineColor = particle.color;
              ctx.strokeStyle = lineColor + (1 - pdist / 120) * 0.08 + ")";
              ctx.lineWidth = 0.5;
              ctx.beginPath();
              ctx.moveTo(particle.x, particle.y);
              ctx.lineTo(other.x, other.y);
              ctx.stroke();
            }
          });
        });

        // FPS counter
        ctx.globalAlpha = 1;
        ctx.fillStyle = isDark ? "rgba(107, 114, 128, 0.6)" : "rgba(107, 114, 128, 0.4)";
        ctx.font = "12px monospace";
        ctx.fillText(`FPS: ${fps}`, 12, 24);
      }

      animationFrameId = requestAnimationFrame(animate);
    };

    animate();

    return () => {
      cancelAnimationFrame(animationFrameId);
      window.removeEventListener("resize", resizeCanvas);
      window.removeEventListener("mousemove", handleMouseMove);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [theme]);

  return (
    <canvas
      ref={canvasRef}
      className="fixed inset-0 z-0 pointer-events-none"
      style={{ background: theme === "dark" ? "linear-gradient(135deg, #0a0a0a 0%, #1a1a2e 100%)" : "linear-gradient(135deg, #f5f5f5 0%, #e5e5e5 100%)" }}
    />
  );
}
