-- =====================================================================================
--  Projeto Final de IoT (UNIDAVI) - Checkpoint 3
--  Proposta de banco de dados para o Supabase (PostgreSQL)
--
--  Como usar: Supabase > SQL Editor > cole este arquivo inteiro > Run.
--  A equipe t27 / 1025897_36728 e as duas especies sao cadastradas na secao 8.
--
--  O que este script cria
--    1. Dados de referencia do Projeto Final (cenarios, especies e regras de cada cenario)
--    2. Equipes e as especies de cada equipe
--    3. Tabelas que o Node-RED alimenta: leituras, estados dos atuadores, status da placa e estado geral
--    4. Permissoes: a chave publica (anon) so consegue INSERIR. Nao le, nao altera, nao apaga.
--    5. Visoes de consulta (ultimos valores e estado atual)
--    6. Integracao com o ThingSpeak: uma funcao que envia os dados do banco ao canal, agendada a cada minuto
-- =====================================================================================


-- -------------------------------------------------------------------------------------
-- 1. DADOS DE REFERENCIA (fixos, vem da secao 4.2 do Projeto Final)
-- -------------------------------------------------------------------------------------
create table if not exists perfis_cenario (
  id    smallint primary key,
  nome  text not null unique
);

create table if not exists especies (
  slug             text primary key check (slug ~ '^[a-z0-9-]{1,40}$'),   -- igual ao nome no topico MQTT
  codigo           smallint not null unique,                               -- usado no campo 7 do ThingSpeak
  nome_popular     text not null,
  nome_cientifico  text not null,
  perfil_id        smallint not null references perfis_cenario(id)
);

create table if not exists regras (
  id               smallint generated always as identity primary key,
  perfil_id        smallint not null references perfis_cenario(id),
  sensor           text not null check (sensor in ('luminosidade','umidade-solo','umidade-ar','temperatura')),
  comparador       text not null check (comparador in ('menor','maior','fora_da_faixa')),
  limite           numeric not null,          -- o limite (ou o limite inferior, em fora_da_faixa)
  limite_superior  numeric,                   -- so em fora_da_faixa
  atuador          text not null check (atuador in ('aquecimento','irrigacao','sombreamento','nebulizacao','ventilacao')),
  check ((comparador = 'fora_da_faixa') = (limite_superior is not null))
);

insert into perfis_cenario (id, nome) values
  (1, 'Aquecimento e irrigacao'),
  (2, 'Sombreamento e nebulizacao'),
  (3, 'Sombreamento ajustavel e irrigacao'),
  (4, 'Irrigacao e ventilacao'),
  (5, 'Irrigacao e sombreamento leve')
on conflict do nothing;

insert into especies (slug, codigo, nome_popular, nome_cientifico, perfil_id) values
  ('araucaria',          1, 'Araucaria / Pinheiro-brasileiro', 'Araucaria angustifolia',        1),
  ('palmiteiro',         2, 'Palmiteiro / Jucara',             'Euterpe edulis',                2),
  ('cedro',              3, 'Cedro',                           'Cedrela fissilis',              3),
  ('pitangueira',        4, 'Pitangueira',                     'Eugenia uniflora',              3),
  ('cerejeira-do-mato',  5, 'Cerejeira-do-mato',               'Eugenia involucrata',           3),
  ('inga',               6, 'Inga',                            'Inga vera',                     4),
  ('embauba',            7, 'Embauba',                         'Cecropia glaziovii',            5),
  ('grandiuva',          8, 'Grandiuva',                       'Trema micrantha',               5),
  ('aroeira-vermelha',   9, 'Aroeira-vermelha',                'Schinus terebinthifolia',       5),
  ('araca',             10, 'Araca',                           'Psidium cattleianum',           5),
  ('camboata-vermelho', 11, 'Camboata-vermelho',               'Cupania vernalis',              3),
  ('camboata',          12, 'Camboata',                        'Matayba elaeagnoides',          3),
  ('chal-chal',         13, 'Chal-chal',                       'Allophylus edulis',             3),
  ('grapia',            14, 'Grapia',                          'Apuleia leiocarpa',             5),
  ('ipe',               15, 'Ipe (grupo Handroanthus)',        'Handroanthus spp.',             5),
  ('leiteiro',          16, 'Leiteiro / Jasmim-manga',         'Tabernaemontana catharinensis', 2),
  ('guabiroba',         17, 'Guabiroba',                       'Campomanesia xanthocarpa',      3),
  ('uvaia',             18, 'Uvaia',                           'Eugenia pyriformis',            3)
on conflict do nothing;

insert into regras (perfil_id, sensor, comparador, limite, limite_superior, atuador)
select * from (values
  (1::smallint, 'temperatura',  'menor',         10::numeric, null::numeric, 'aquecimento'),
  (1, 'umidade-solo', 'menor',         35, null, 'irrigacao'),
  (2, 'luminosidade', 'maior',         40, null, 'sombreamento'),
  (2, 'umidade-ar',   'menor',         70, null, 'nebulizacao'),
  (3, 'luminosidade', 'fora_da_faixa', 30, 70,   'sombreamento'),
  (3, 'umidade-solo', 'menor',         40, null, 'irrigacao'),
  (4, 'umidade-solo', 'menor',         60, null, 'irrigacao'),
  (4, 'temperatura',  'maior',         30, null, 'ventilacao'),
  (5, 'umidade-solo', 'menor',         30, null, 'irrigacao'),
  (5, 'luminosidade', 'maior',         90, null, 'sombreamento')
) as v(perfil_id, sensor, comparador, limite, limite_superior, atuador)
where not exists (select 1 from regras);


-- -------------------------------------------------------------------------------------
-- 2. EQUIPES
-- -------------------------------------------------------------------------------------
create table if not exists equipes (
  matriculas  text primary key check (matriculas ~ '^[a-z0-9_-]{1,40}$'),   -- ex.: 100234_100567
  turma       text not null default 't27',
  integrantes text,
  criada_em   timestamptz not null default now()
);

create table if not exists equipe_especies (
  equipe   text not null references equipes(matriculas) on delete cascade,
  especie  text not null references especies(slug),
  primary key (equipe, especie)
);


-- -------------------------------------------------------------------------------------
-- 3. DADOS QUE O NODE-RED GRAVA
-- -------------------------------------------------------------------------------------
create table if not exists leituras (
  id          bigint generated always as identity primary key,
  equipe      text not null,
  especie     text not null,
  sensor      text not null check (sensor in ('luminosidade','umidade-solo','umidade-ar','temperatura')),
  valor       numeric(8,2) not null,
  unidade     text not null default '',
  medido_em   timestamptz not null default now(),
  foreign key (equipe, especie) references equipe_especies (equipe, especie)
);
create index if not exists ix_leituras_busca on leituras (equipe, especie, sensor, medido_em desc);

create table if not exists estados_atuadores (
  id            bigint generated always as identity primary key,
  equipe        text not null,
  especie       text not null,
  atuador       text not null check (atuador in ('aquecimento','irrigacao','sombreamento','nebulizacao','ventilacao')),
  ligado        boolean not null,
  registrado_em timestamptz not null default now(),
  foreign key (equipe, especie) references equipe_especies (equipe, especie)
);
create index if not exists ix_estados_atuadores_busca on estados_atuadores (equipe, especie, atuador, registrado_em desc);

create table if not exists status_placa (
  id            bigint generated always as identity primary key,
  equipe        text not null,
  especie       text not null,
  status        text not null check (status in ('online','offline')),
  registrado_em timestamptz not null default now(),
  foreign key (equipe, especie) references equipe_especies (equipe, especie)
);
create index if not exists ix_status_placa_busca on status_placa (equipe, especie, registrado_em desc);

create table if not exists estados_gerais (
  id            bigint generated always as identity primary key,
  equipe        text not null,
  especie       text not null,
  estado        text not null check (estado in ('OFFLINE','SEM DADOS','ALERTA','ATUANDO','NORMAL','INATIVA')),
  motivo        text,
  registrado_em timestamptz not null default now(),
  foreign key (equipe, especie) references equipe_especies (equipe, especie)
);
create index if not exists ix_estados_gerais_busca on estados_gerais (equipe, especie, registrado_em desc);


-- -------------------------------------------------------------------------------------
-- 4. VISOES DE CONSULTA
-- -------------------------------------------------------------------------------------
create or replace view v_ultima_leitura as
select distinct on (equipe, especie, sensor)
       equipe, especie, sensor, valor, unidade, medido_em,
       extract(epoch from (now() - medido_em))::int as idade_s
from leituras
order by equipe, especie, sensor, medido_em desc, id desc;

create or replace view v_ultimo_estado_atuador as
select distinct on (equipe, especie, atuador)
       equipe, especie, atuador, ligado, registrado_em
from estados_atuadores
order by equipe, especie, atuador, registrado_em desc, id desc;

create or replace view v_ultimo_status as
select distinct on (equipe, especie)
       equipe, especie, status, registrado_em
from status_placa
order by equipe, especie, registrado_em desc, id desc;

create or replace view v_ultimo_estado_geral as
select distinct on (equipe, especie)
       equipe, especie, estado, motivo, registrado_em
from estados_gerais
order by equipe, especie, registrado_em desc, id desc;

-- Uma linha por equipe e especie: estado geral, status da placa e idade da ultima leitura
create or replace view v_estado_atual as
select ee.equipe, ee.especie,
       coalesce(g.estado, 'SEM DADOS')         as estado_geral,
       g.motivo,
       s.status                                as status_placa,
       l.idade_s                               as idade_ultima_leitura_s,
       a.atuadores_ligados
from equipe_especies ee
left join v_ultimo_estado_geral g on g.equipe = ee.equipe and g.especie = ee.especie
left join v_ultimo_status s       on s.equipe = ee.equipe and s.especie = ee.especie
left join (select equipe, especie, min(idade_s) as idade_s from v_ultima_leitura group by equipe, especie) l
       on l.equipe = ee.equipe and l.especie = ee.especie
left join (select equipe, especie, string_agg(atuador, ', ' order by atuador) as atuadores_ligados
           from v_ultimo_estado_atuador where ligado group by equipe, especie) a
       on a.equipe = ee.equipe and a.especie = ee.especie;


-- -------------------------------------------------------------------------------------
-- 5. PERMISSOES E SEGURANCA (RLS)
--    A chave publica (anon) fica no Node-RED, entao ela so pode INSERIR nas quatro tabelas
--    de dados. Todo o resto fica fechado. Quem consulta e voce, pelo painel do Supabase.
-- -------------------------------------------------------------------------------------
alter table perfis_cenario    enable row level security;
alter table especies          enable row level security;
alter table regras            enable row level security;
alter table equipes           enable row level security;
alter table equipe_especies   enable row level security;
alter table leituras          enable row level security;
alter table estados_atuadores enable row level security;
alter table status_placa      enable row level security;
alter table estados_gerais    enable row level security;

-- as visoes respeitam as permissoes de quem consulta (e nao as do dono)
alter view v_ultima_leitura         set (security_invoker = true);
alter view v_ultimo_estado_atuador  set (security_invoker = true);
alter view v_ultimo_status          set (security_invoker = true);
alter view v_ultimo_estado_geral    set (security_invoker = true);
alter view v_estado_atual           set (security_invoker = true);

-- o Supabase concede acesso amplo por padrao: retiramos tudo e devolvemos so o necessario
revoke all on all tables    in schema public from anon, authenticated;
revoke all on all sequences in schema public from anon, authenticated;

grant usage on schema public to anon;
grant insert on leituras, estados_atuadores, status_placa, estados_gerais to anon;

drop policy if exists anon_insere on leituras;
drop policy if exists anon_insere on estados_atuadores;
drop policy if exists anon_insere on status_placa;
drop policy if exists anon_insere on estados_gerais;
create policy anon_insere on leituras          for insert to anon with check (true);
create policy anon_insere on estados_atuadores for insert to anon with check (true);
create policy anon_insere on status_placa      for insert to anon with check (true);
create policy anon_insere on estados_gerais    for insert to anon with check (true);
-- Atencao: sem politica de leitura, a insercao so funciona com o cabecalho  Prefer: return=minimal


-- -------------------------------------------------------------------------------------
-- 6. INTEGRACAO COM O THINGSPEAK
--    O ThingSpeak nao le o banco sozinho. O proprio banco empurra os dados: a cada minuto
--    uma funcao monta o ultimo estado de cada equipe e envia ao canal da equipe.
--    Precisa das extensoes pg_net e pg_cron (Supabase > Database > Extensions).
--
--    Campos do canal (a mesma ordem para todas as equipes):
--      field1 luminosidade (%)      field5 estado geral (codigo)
--      field2 umidade-solo (%)      field6 quantidade de atuadores ligados
--      field3 umidade-ar (%)        field7 especie (codigo)
--      field4 temperatura (C)       field8 idade da ultima leitura (s)
--    Codigo do estado geral: 0 OFFLINE, 1 SEM DADOS, 2 ALERTA, 3 ATUANDO, 4 NORMAL, 5 INATIVA
-- -------------------------------------------------------------------------------------
create table if not exists integracoes (
  equipe                text primary key references equipes(matriculas) on delete cascade,
  thingspeak_canal      bigint,
  thingspeak_write_key  text
);
alter table integracoes enable row level security;     -- sem politica: nem a chave publica le a chave do ThingSpeak
revoke all on integracoes from anon, authenticated;

-- Especie "ativa" da equipe = a do ultimo dado recebido. Equipes com uma so especie nao mudam nada.
create or replace view v_thingspeak_payload as
with ativa as (
  select distinct on (equipe) equipe, especie
  from leituras
  order by equipe, medido_em desc, id desc
),
vals as (
  select u.equipe, u.especie,
         max(case when u.sensor = 'luminosidade' then u.valor end) as f1,
         max(case when u.sensor = 'umidade-solo' then u.valor end) as f2,
         max(case when u.sensor = 'umidade-ar'   then u.valor end) as f3,
         max(case when u.sensor = 'temperatura'  then u.valor end) as f4,
         min(u.idade_s) as f8
  from v_ultima_leitura u
  join ativa a on a.equipe = u.equipe and a.especie = u.especie
  group by u.equipe, u.especie
)
select v.equipe, v.especie, e.codigo as f7,
       v.f1, v.f2, v.f3, v.f4,
       case coalesce(g.estado, 'SEM DADOS')
            when 'OFFLINE' then 0 when 'SEM DADOS' then 1 when 'ALERTA' then 2
            when 'ATUANDO' then 3 when 'NORMAL' then 4 when 'INATIVA' then 5 end as f5,
       coalesce(l.qtd, 0) as f6,
       v.f8,
       coalesce(g.estado, 'SEM DADOS') || coalesce(' | ' || l.nomes, '') as texto_status
from vals v
join especies e on e.slug = v.especie
left join v_ultimo_estado_geral g on g.equipe = v.equipe and g.especie = v.especie
left join (select equipe, especie, count(*) as qtd, string_agg(atuador, ', ' order by atuador) as nomes
           from v_ultimo_estado_atuador where ligado group by equipe, especie) l
       on l.equipe = v.equipe and l.especie = v.especie;
revoke all on v_thingspeak_payload from anon, authenticated;

create or replace function fn_enviar_thingspeak() returns integer
language plpgsql security definer set search_path = public, extensions
as $$
declare r record; n integer := 0;
begin
  for r in
    select i.thingspeak_write_key as chave, p.*
    from integracoes i
    join v_thingspeak_payload p on p.equipe = i.equipe
    where i.thingspeak_write_key is not null
  loop
    perform net.http_post(
      url     := 'https://api.thingspeak.com/update.json',
      headers := '{"Content-Type": "application/json"}'::jsonb,
      body    := jsonb_strip_nulls(jsonb_build_object(
                   'api_key', r.chave,
                   'field1', r.f1, 'field2', r.f2, 'field3', r.f3, 'field4', r.f4,
                   'field5', r.f5, 'field6', r.f6, 'field7', r.f7, 'field8', r.f8,
                   'status', r.texto_status))
    );
    n := n + 1;
  end loop;
  return n;
end;
$$;
revoke all on function fn_enviar_thingspeak() from public, anon, authenticated;

-- Agendamento: uma vez por minuto (a conta gratuita do ThingSpeak aceita 1 envio a cada 15 s por canal).
-- Rode esta linha depois de ativar as extensoes pg_net e pg_cron:
--   select cron.schedule('thingspeak_a_cada_minuto', '* * * * *', 'select public.fn_enviar_thingspeak()');
-- Para parar:  select cron.unschedule('thingspeak_a_cada_minuto');


-- -------------------------------------------------------------------------------------
-- 7. CONSULTAS UTEIS (rode no SQL Editor do Supabase)
-- -------------------------------------------------------------------------------------
--   select * from v_estado_atual;
--   select * from v_ultima_leitura where equipe = '100234_100567';
--   select sensor, valor, medido_em from leituras
--     where equipe = '100234_100567' and especie = 'cedro' and medido_em > now() - interval '30 minutes'
--     order by medido_em;
--   select * from v_thingspeak_payload;


-- -------------------------------------------------------------------------------------
-- 8. CADASTRO DA SUA EQUIPE  (troque pelos seus dados e rode)

insert into equipes (matriculas, turma, integrantes)
values ('1025897_36728', 't27', 'Sofya Bruzadelli Borges Seidler de Andrade e Matheus Ferrari dos Santos')
on conflict (matriculas) do update
set turma = excluded.turma, integrantes = excluded.integrantes;

insert into equipe_especies (equipe, especie)
values ('1025897_36728', 'araucaria'), ('1025897_36728', 'grandiuva')
on conflict do nothing;

insert into integracoes (equipe) values ('1025897_36728')
on conflict do nothing;

-- Na sua conta, configure o ID do canal e a chave de escrita na linha de integracoes.
-- Ative pg_net e pg_cron e execute o agendamento da secao 6 separadamente.
