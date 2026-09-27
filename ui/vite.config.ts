import { defineConfig, loadEnv } from 'vite';
import react from '@vitejs/plugin-react-swc';

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), '');
  return {
    plugins: [react()],
    build: { sourcemap: false },
    server: env.ESP32_TARGET ? {
      proxy: { '/api': { target: env.ESP32_TARGET, changeOrigin: true } },
    } : undefined,
  };
});
