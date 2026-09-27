export type Status = {
  deviceName: string;
  firmwareVersion: string;
  chip: string;
  uptimeSeconds: number;
  freeHeapBytes: number;
  sampleCount: number;
  localIp: string;
  rssiDbm?: number;
};

export type Settings = {
  deviceName: string;
  sampleIntervalSeconds: number;
};

const TOKEN_KEY = 'esp32_hosted_ui_admin_key';
const demoMode = import.meta.env.DEV && import.meta.env.VITE_DEMO !== '0';
const bootTime = Date.now();
let demoSettings: Settings = { deviceName: 'Greenhouse monitor', sampleIntervalSeconds: 30 };

export const getKey = () => sessionStorage.getItem(TOKEN_KEY) ?? '';
export const setKey = (key: string) => sessionStorage.setItem(TOKEN_KEY, key);
export const clearKey = () => sessionStorage.removeItem(TOKEN_KEY);
export const isDemoMode = () => demoMode;

async function request<T>(path: string, options: RequestInit = {}): Promise<T> {
  if (demoMode) {
    await new Promise(resolve => setTimeout(resolve, 150));
    if (path === '/api/status') return {
      deviceName: demoSettings.deviceName,
      firmwareVersion: 'demo-1.0.0',
      chip: 'ESP32-S3',
      uptimeSeconds: Math.floor((Date.now() - bootTime) / 1000) + 742,
      freeHeapBytes: 196608,
      sampleCount: 84,
      localIp: '192.168.1.42',
      rssiDbm: -51,
    } as T;
    if (path === '/api/settings' && options.method === 'POST') {
      demoSettings = JSON.parse(options.body as string) as Settings;
      return demoSettings as T;
    }
    if (path === '/api/settings') return demoSettings as T;
    if (path === '/api/update/start') return { ok: true, maxChunkBytes: 12288 } as T;
    if (path === '/api/update/chunk') return { receivedBytes: 0 } as T;
    if (path === '/api/update/finish') return { ok: true, restarting: true } as T;
  }
  const response = await fetch(path, {
    ...options,
    headers: { 'X-Device-Key': getKey(), ...options.headers },
  });
  const body = await response.json().catch(() => ({})) as { error?: string };
  if (!response.ok) throw new Error(body.error || `${response.status} ${response.statusText}`);
  return body as T;
}

export const getStatus = () => request<Status>('/api/status');
export const getSettings = () => request<Settings>('/api/settings');
export const saveSettings = (settings: Settings) => request<Settings>('/api/settings', {
  method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(settings),
});

export async function uploadFirmware(file: File, onProgress: (percent: number) => void) {
  const { maxChunkBytes } = await request<{ maxChunkBytes: number }>('/api/update/start', {
    method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: String(file.size),
  });
  if (!maxChunkBytes || maxChunkBytes > 12288) throw new Error('Device returned an invalid chunk size');
  for (let offset = 0; offset < file.size; offset += maxChunkBytes) {
    const form = new FormData();
    form.append('chunk', file.slice(offset, offset + maxChunkBytes), 'chunk.bin');
    await request('/api/update/chunk', { method: 'POST', body: form });
    onProgress(Math.round((Math.min(offset + maxChunkBytes, file.size) / file.size) * 100));
  }
  await request('/api/update/finish', { method: 'POST' });
}
