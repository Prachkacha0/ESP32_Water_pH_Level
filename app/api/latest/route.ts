export const dynamic = "force-dynamic";

export async function GET() {
  const url = process.env.SUPABASE_URL;
  const key = process.env.SUPABASE_SERVICE_ROLE_KEY;
  if (!url || !key) {
    return Response.json(
      { configured: false, reading: null, message: "Cloud database is not configured." },
      { status: 503, headers: { "Cache-Control": "no-store" } },
    );
  }

  try {
    const response = await fetch(
      `${url}/rest/v1/readings?select=device_id,ph,temperature,sensor1,sensor2,water_level,created_at&order=created_at.desc&limit=1`,
      { headers: { apikey: key, Authorization: `Bearer ${key}` }, cache: "no-store" },
    );
    if (!response.ok) throw new Error(`Supabase returned ${response.status}`);
    const rows = await response.json();
    return Response.json(
      { configured: true, reading: rows[0] ?? null },
      { headers: { "Cache-Control": "no-store" } },
    );
  } catch {
    return Response.json(
      { configured: true, reading: null, message: "Unable to read telemetry." },
      { status: 502, headers: { "Cache-Control": "no-store" } },
    );
  }
}
