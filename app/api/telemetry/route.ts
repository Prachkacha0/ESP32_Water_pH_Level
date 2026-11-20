import { timingSafeEqual } from "node:crypto";

export const runtime = "nodejs";

function secureEqual(received: string, expected: string) {
  const left = Buffer.from(received);
  const right = Buffer.from(expected);
  return left.length === right.length && timingSafeEqual(left, right);
}

export async function POST(request: Request) {
  const deviceKey = process.env.DEVICE_API_KEY;
  const suppliedKey = request.headers.get("x-device-key") ?? "";
  if (!deviceKey || !secureEqual(suppliedKey, deviceKey)) {
    return Response.json({ error: "Unauthorized" }, { status: 401 });
  }

  const url = process.env.SUPABASE_URL;
  const key = process.env.SUPABASE_SECRET_KEY;
  if (!url || !key) {
    return Response.json({ error: "Database is not configured" }, { status: 503 });
  }
  const baseUrl = url.replace(/\/rest\/v1\/?$/, "").replace(/\/+$/, "");

  let body: Record<string, unknown>;
  try { body = await request.json(); }
  catch { return Response.json({ error: "Invalid JSON" }, { status: 400 }); }

  const ph = Number(body.ph);
  const temperature = Number(body.temperature);
  const sensor1 = body.sensor1;
  const sensor2 = body.sensor2;
  const waterLevel = String(body.waterLevel ?? "");
  const validLevels = ["LOW", "MIDDLE", "HIGH", "SENSOR ERROR"];
  if (!Number.isFinite(ph) || ph < 0 || ph > 14 ||
      !Number.isFinite(temperature) || temperature < -55 || temperature > 125 ||
      typeof sensor1 !== "boolean" || typeof sensor2 !== "boolean" ||
      !validLevels.includes(waterLevel)) {
    return Response.json({ error: "Telemetry validation failed" }, { status: 422 });
  }

  const payload = {
    device_id: String(body.deviceId ?? "water-monitor-01").slice(0, 64),
    ph, temperature, sensor1, sensor2, water_level: waterLevel,
  };
  const response = await fetch(`${baseUrl}/rest/v1/readings`, {
    method: "POST",
    headers: {
      apikey: key,
      "Content-Type": "application/json",
      Prefer: "return=minimal",
    },
    body: JSON.stringify(payload),
    cache: "no-store",
  });
  if (!response.ok) return Response.json({ error: "Database write failed" }, { status: 502 });
  return Response.json({ ok: true }, { status: 201 });
}
