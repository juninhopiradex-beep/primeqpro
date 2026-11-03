// PCC — Edge Function /validate (Supabase, Deno).  Deploy:  supabase functions deploy validate --no-verify-jwt
// Segredo necessário:  supabase secrets set PCC_SIGNING_KEY=<base64 do seed Ed25519, de tools/gen_pcc_keys.py>
// (SUPABASE_URL e SUPABASE_SERVICE_ROLE_KEY já existem automaticamente nas Edge Functions.)
// Contrato e mensagem assinada: ver Source/Shared/PccProtocol.h do repositório primeqpro.
import { createClient } from "https://esm.sh/@supabase/supabase-js@2.45.4";

const sb = createClient(Deno.env.get("SUPABASE_URL")!, Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!, { auth: { persistSession: false } });
const PRODUCTS = new Set(["PRIMEQPRO"]);          // acrescentar aqui os outros plugins quando migrarem para o PCC
const hex = (u: Uint8Array) => [...u].map((b) => b.toString(16).padStart(2, "0")).join("");
const b64 = (s: string) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0));

let keyPromise: Promise<CryptoKey> | null = null;
function signingKey() {
  keyPromise ??= (async () => {
    const seed = b64(Deno.env.get("PCC_SIGNING_KEY")!);
    const pkcs8 = new Uint8Array([0x30,0x2e,0x02,0x01,0x00,0x30,0x05,0x06,0x03,0x2b,0x65,0x70,0x04,0x22,0x04,0x20, ...seed]);
    return await crypto.subtle.importKey("pkcs8", pkcs8, { name: "Ed25519" }, false, ["sign"]);
  })();
  return keyPromise;
}

async function reply(o: { valid: boolean; role: string; expires: string; message: string; product: string; machineId: string; nonce: string }) {
  const ts = Math.floor(Date.now() / 1000);
  const msg = `v1|${o.valid ? 1 : 0}|${o.role}|${o.expires}|${o.product}|${o.machineId}|${o.nonce}|${ts}`;
  const sig = hex(new Uint8Array(await crypto.subtle.sign("Ed25519", await signingKey(), new TextEncoder().encode(msg))));
  return new Response(JSON.stringify({ ...o, ts, sig }), { headers: { "content-type": "application/json", "access-control-allow-origin": "*" } });
}

Deno.serve(async (req) => {
  if (req.method === "OPTIONS") return new Response(null, { headers: { "access-control-allow-origin": "*", "access-control-allow-headers": "content-type, authorization, apikey", "access-control-allow-methods": "POST" } });
  if (req.method !== "POST") return new Response("method not allowed", { status: 405 });
  let b: any; try { b = await req.json(); } catch { return new Response("bad json", { status: 400 }); }
  const product = String(b.product ?? "").toUpperCase(), machineId = String(b.machineId ?? ""), keyHash = String(b.keyHash ?? "").toLowerCase(), nonce = String(b.nonce ?? "");
  if (!PRODUCTS.has(product) || !machineId || !/^[0-9a-f]{64}$/.test(keyHash) || !/^[0-9a-f]{16,64}$/.test(nonce)) return new Response("bad request", { status: 400 });
  const base = { product, machineId, nonce, role: "", expires: "" };
  const ip = req.headers.get("x-forwarded-for")?.split(",")[0]?.trim() ?? null, pais = req.headers.get("cf-ipcountry") ?? null;
  const log = (licenca_id: string | null, evento: string) => sb.from("ativacoes").insert({ licenca_id, plugin: product.toLowerCase(), maquina: machineId, ip, pais, evento });

  const { data } = await sb.rpc("pcc_find_license", { p_keyhash: keyHash, p_plugin: product });
  const lic = Array.isArray(data) ? data[0] : data;
  if (!lic) { await log(null, "denied"); return reply({ ...base, valid: false, message: "Licença inválida." }); }
  const today = new Date().toISOString().slice(0, 10);
  const deny = async (message: string) => { await log(lic.id, "denied"); return reply({ ...base, valid: false, role: lic.tipo, expires: lic.validade ?? "", message }); };
  if (lic.estado !== "ativo") return deny("Licença desativada. Contacte o suporte.");
  if (lic.validade && lic.tipo !== "master" && lic.validade < today) return deny("Licença expirada em " + lic.validade + ".");
  if (lic.maquina && lic.maquina !== machineId) return deny("Esta licença está ativa noutro computador. Peça a transferência ao suporte.");

  await sb.from("licencas").update({ maquina: machineId, ultima_validacao: new Date().toISOString(), ativado_em: lic.ativado_em ?? new Date().toISOString() }).eq("id", lic.id);
  await log(lic.id, lic.maquina ? "validate" : "activate");
  return reply({ ...base, valid: true, role: lic.tipo, expires: lic.tipo === "master" ? "" : (lic.validade ?? ""), message: "ok" });
});
