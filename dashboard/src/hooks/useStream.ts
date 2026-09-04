import { useEffect, useRef } from "react";

export function useStream(canvasRef: React.RefObject<HTMLCanvasElement>, connected: boolean) {
  const frameCountRef = useRef(0);

  useEffect(() => {
    if (!connected || !canvasRef.current) return;

    const canvas = canvasRef.current;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    // Phase 5: Replace with real WebSocket frame stream
    // For now, render test pattern
    const renderFrame = () => {
      const { width, height } = canvas;
      const imageData = ctx.createImageData(width, height);
      const data = imageData.data;

      frameCountRef.current++;
      const hue = (frameCountRef.current % 360) / 360;

      for (let i = 0; i < data.length; i += 4) {
        const brightness = (Math.sin(frameCountRef.current / 50 + i / 1000) + 1) / 2;
        const value = Math.floor(brightness * 255);

        data[i] = value;
        data[i + 1] = Math.floor(value * (1 - Math.abs(hue - 0.5) * 2));
        data[i + 2] = value;
        data[i + 3] = 255;
      }

      ctx.putImageData(imageData, 0, 0);
    };

    const interval = setInterval(renderFrame, 33);
    renderFrame();

    return () => clearInterval(interval);
  }, [connected, canvasRef]);

  return { frameCount: frameCountRef.current };
}
