-- =============================================================================
--  PCC — suporte à validação online dos plugins (função /validate)
--  Colar em Supabase -> SQL Editor -> Run. Seguro repetir.
--  A chave em claro nunca sai do computador do cliente: o plugin envia sha256(chave|PLUGIN) e o servidor
--  compara com o mesmo hash calculado sobre a coluna `chave`.
-- =============================================================================
create extension if not exists pgcrypto with schema extensions;

-- índice por hash (pesquisa rápida sem expor chaves)
create index if not exists idx_licencas_keyhash on public.licencas
  ( (encode(extensions.digest(chave || '|' || upper(plugin), 'sha256'), 'hex')) );

-- procura a licença pelo hash; só a Edge Function (service_role) a pode chamar
create or replace function public.pcc_find_license(p_keyhash text, p_plugin text)
returns setof public.licencas
language sql
security definer
set search_path = public, extensions
as $$
  select * from public.licencas
  where encode(digest(chave || '|' || upper(plugin), 'sha256'), 'hex') = lower(p_keyhash)
    and upper(plugin) = upper(p_plugin)
  limit 1;
$$;
revoke all on function public.pcc_find_license(text, text) from public, anon, authenticated;
grant execute on function public.pcc_find_license(text, text) to service_role;

-- criar uma licença do PRIMEQ PRO (gera a chave; devolve-a UMA vez). Ex.:
--   select * from public.pcc_new_primeqpro_license('João Silva','joao@x.com','Angola','venda', null);
create or replace function public.pcc_new_primeqpro_license(p_cliente text, p_email text, p_pais text, p_tipo text, p_validade date)
returns table(chave text, validade date)
language plpgsql
security definer
set search_path = public, extensions
as $$
declare k text;
begin
  k := 'PQP-' || upper(substr(encode(gen_random_bytes(2),'hex'),1,4)) || '-' || upper(substr(encode(gen_random_bytes(2),'hex'),1,4)) || '-' || upper(substr(encode(gen_random_bytes(2),'hex'),1,4));
  insert into public.licencas(cliente,email,pais,plugin,chave,tipo,validade,estado)
  values (p_cliente,p_email,p_pais,'primeqpro',k,coalesce(p_tipo,'venda'),p_validade,'ativo');
  return query select k, p_validade;
end $$;
revoke all on function public.pcc_new_primeqpro_license(text,text,text,text,date) from public, anon;
grant execute on function public.pcc_new_primeqpro_license(text,text,text,text,date) to authenticated, service_role;
