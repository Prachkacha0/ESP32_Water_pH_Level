# ESP32 Water pH Level Monitor

This repository contains:

- `ESP32_Water_Monitor.ino`: ESP32 firmware
- A Next.js dashboard for Vercel
- Protected telemetry API at `POST /api/telemetry`
- Supabase table setup in `supabase.sql`

## Deploy the dashboard

1. Create a Supabase project.
2. Run `supabase.sql` in the Supabase SQL Editor.
3. In Vercel, add these Environment Variables:
   - `SUPABASE_URL`
   - `SUPABASE_SERVICE_ROLE_KEY` (server-only; never expose it in browser code)
   - `DEVICE_API_KEY` (use a long random value)
4. Redeploy the Vercel project.

Until the variables are configured, the dashboard loads normally and displays
`รอตั้งค่า Cloud` instead of returning a 404 page.

## Telemetry request

Send JSON to `POST https://esp32-water-ph.vercel.app/api/telemetry` with the
header `X-Device-Key: <DEVICE_API_KEY>`.

```json
{
  "deviceId": "water-monitor-01",
  "ph": 7.2,
  "temperature": 26.5,
  "sensor1": true,
  "sensor2": false,
  "waterLevel": "MIDDLE"
}
```
