// Dolby Vision no cabecalho do Matroska: o registro dvcC/dvvC mora num
// BlockAdditionMapping dentro da TrackEntry de video, e e dele que sai a
// decisao de declarar o perfil ao pipeline (video.c). Errar aqui nao falha:
// declara o perfil errado e a C9 fica SEM IMAGEM. Este teste monta cabecalhos
// em memoria, um por caso, e confere o que o parser le e o que a regra decide.
#include "../src/mkv.h"
#include <stdio.h>
#include <string.h>

#define main mkv_main_nao_usado
#include "../src/mkv.c"
#undef main

static int falhas;
static void ok(int cond, const char *o) {
  printf("  %-52s %s\n", o, cond ? "ok" : "FALHOU");
  if (!cond) falhas++;
}

// --- montagem de EBML (mesmo esquema de tests/mkv_caps.c) --------------------
static unsigned char buf[4096];
static long n;

static void put(const unsigned char *b, long k) { memcpy(buf + n, b, k); n += k; }
static void id1(unsigned char v) { put(&v, 1); }
static void id2(unsigned long v) { unsigned char b[2]={(unsigned char)(v>>8),(unsigned char)v}; put(b,2); }
static void id4(unsigned long v) {
  unsigned char b[4]={(unsigned char)(v>>24),(unsigned char)(v>>16),
                      (unsigned char)(v>>8),(unsigned char)v};
  put(b,4);
}
static long tam4(void) { long p = n; unsigned char b[4]={0x10,0,0,0}; put(b,4); return p; }
static void fecha(long p) {
  long v = n - p - 4;
  buf[p]   = (unsigned char)(0x10 | ((v >> 24) & 0x0F));
  buf[p+1] = (unsigned char)(v >> 16);
  buf[p+2] = (unsigned char)(v >> 8);
  buf[p+3] = (unsigned char)v;
}
static void uint1(unsigned v) { unsigned char b[2] = { 0x81, (unsigned char)v }; put(b, 2); }
static void uint4(unsigned long v) {
  unsigned char b[5] = { 0x84, (unsigned char)(v>>24), (unsigned char)(v>>16),
                         (unsigned char)(v>>8), (unsigned char)v };
  put(b, 5);
}
static void str(const char *s) {
  size_t k = strlen(s);
  unsigned char t = (unsigned char)(0x80 | k);
  put(&t, 1); put((const unsigned char *)s, (long)k);
}

// DOVIDecoderConfigurationRecord de 24 bytes, como o mkvmerge e o ffmpeg
// gravam: versao 1.0, depois perfil/nivel/camadas/compatibilidade.
static void registroDv(unsigned long fourcc, int perfil, int nivel,
                       int rpu, int el, int bl, int compat) {
  unsigned char r[24];
  long m;
  memset(r, 0, sizeof r);
  r[0] = 1; r[1] = 0;
  r[2] = (unsigned char)((perfil << 1) | ((nivel >> 5) & 1));
  r[3] = (unsigned char)(((nivel & 0x1F) << 3) | (rpu << 2) | (el << 1) | bl);
  r[4] = (unsigned char)(compat << 4);
  id2(0x41E4UL); m = tam4();
    id2(0x41E7UL); uint4(fourcc);
    id2(0x41EDUL); { unsigned char t = 0x80 | 24; put(&t, 1); put(r, 24); }
  fecha(m);
}

// Faixa de video HEVC, com ou sem registro, e uma de audio depois — a de audio
// esta la para provar que a regra olha a faixa de VIDEO.
static int comRegistro;
static unsigned long rFourcc;
static int rPerfil, rRpu, rEl, rBl, rCompat;

static void montar(void) {
  long seg, trs, e;
  n = 0;
  id4(0x1A45DFA3UL); { long h = tam4(); id2(0x4282UL); str("matroska"); fecha(h); }
  id4(0x18538067UL); seg = tam4();
    id4(0x1654AE6BUL); trs = tam4();
      id1(0xAE); e = tam4();
        id1(0xD7); uint1(1);
        id1(0x83); uint1(1);
        id1(0x86); str("V_MPEGH/ISO/HEVC");
        if (comRegistro) registroDv(rFourcc, rPerfil, 6, rRpu, rEl, rBl, rCompat);
      fecha(e);
      id1(0xAE); e = tam4();
        id1(0xD7); uint1(2);
        id1(0x83); uint1(2);
        id1(0x86); str("A_EAC3");
      fecha(e);
    fecha(trs);
  fecha(seg);
}

static int decidir(int *perfil, MkvFaixa *fx, int *nf) {
  montar();
  *nf = acharTracks(buf, n, fx, MKV_MAX_FAIXAS, NULL, 0, NULL);
  return mkv_dv_decidir(fx, *nf, perfil);
}

static void caso(unsigned long fourcc, int perfil, int rpu, int el, int bl, int compat) {
  comRegistro = 1; rFourcc = fourcc; rPerfil = perfil;
  rRpu = rpu; rEl = el; rBl = bl; rCompat = compat;
}

int main(void) {
  MkvFaixa fx[MKV_MAX_FAIXAS];
  int nf, perfil, d;

  printf("dolby vision no cabecalho do matroska\n");

  // 1. Perfil 8.1 (WEB-DL tipico): dvvC, RPU + base HDR10, sem realce.
  caso(FOURCC_DVVC, 8, 1, 0, 1, 1);
  d = decidir(&perfil, fx, &nf);
  ok(nf == 2, "8.1: duas faixas lidas");
  ok(fx[0].dvPerfil == 8 && fx[0].dvCompat == 1, "8.1: perfil 8, compatibilidade 1");
  ok(fx[0].dvRpu == 1 && fx[0].dvEl == 0 && fx[0].dvBl == 1, "8.1: camadas rpu/bl, sem el");
  ok(fx[1].dvPerfil == 0, "8.1: faixa de audio sem DV");
  ok(d == MKV_DV_DECLARAR && perfil == 8, "8.1: declara perfil 8");

  // 2. Perfil 5 (streaming, base IPT): dvcC, compatibilidade 0.
  caso(FOURCC_DVCC, 5, 1, 0, 1, 0);
  d = decidir(&perfil, fx, &nf);
  ok(d == MKV_DV_DECLARAR && perfil == 5, "5: declara perfil 5");

  // 3. Perfil 7 FEL/MEL (UHD Blu-ray): realce no mesmo fluxo. Quebrou na C9
  //    declarado como 7/dual E como 8/single — nao declara.
  caso(FOURCC_DVCC, 7, 1, 1, 1, 6);
  d = decidir(&perfil, fx, &nf);
  ok(fx[0].dvEl == 1, "7: camada de realce lida");
  ok(d == MKV_DV_RECUSAR && perfil == 7, "7: recusa, toca HDR10");

  // 4. 8.4 (base HLG) e 8.2 (base SDR): nao medidos na C9 — nao declara.
  caso(FOURCC_DVVC, 8, 1, 0, 1, 4);
  d = decidir(&perfil, fx, &nf);
  ok(d == MKV_DV_RECUSAR, "8.4: recusa");
  caso(FOURCC_DVVC, 8, 1, 0, 1, 2);
  d = decidir(&perfil, fx, &nf);
  ok(d == MKV_DV_RECUSAR, "8.2: recusa");

  // 5. Registro sem RPU: nao ha metadado de DV para o decoder.
  caso(FOURCC_DVVC, 8, 0, 0, 1, 1);
  d = decidir(&perfil, fx, &nf);
  ok(d == MKV_DV_RECUSAR, "sem rpu: recusa");

  // 6. Mapeamento que nao e DV (fourcc qualquer): a faixa fica sem perfil.
  caso(0x69747533UL /* "itu3" */, 8, 1, 0, 1, 1);
  d = decidir(&perfil, fx, &nf);
  ok(fx[0].dvPerfil == 0 && d == MKV_DV_SEM, "outro fourcc: sem DV");

  // 7. Sem registro nenhum: o rotulo "DV" do addon nao se confirma.
  comRegistro = 0;
  d = decidir(&perfil, fx, &nf);
  ok(nf == 2 && d == MKV_DV_SEM && perfil == 0, "sem registro: sem DV");

  // 8. Cabecalho cortado no meio da faixa de video: nada lido, nada decidido
  //    — quem chama trata nf == 0 como "nao sei", nao como "sem DV".
  caso(FOURCC_DVVC, 8, 1, 0, 1, 1);
  montar();
  nf = acharTracks(buf, n - 60, fx, MKV_MAX_FAIXAS, NULL, 0, NULL);
  ok(nf == 0, "cabecalho cortado: zero faixas");

  // 9. Registro curto (menos de 5 bytes): ignorado, sem ler fora do buffer.
  { MkvFaixa f; unsigned char m[32]; long k = 0;
    memset(&f, 0, sizeof f);
    m[k++] = 0x41; m[k++] = 0xE7; m[k++] = 0x84;
    m[k++] = 0x64; m[k++] = 0x76; m[k++] = 0x76; m[k++] = 0x43;
    m[k++] = 0x41; m[k++] = 0xED; m[k++] = 0x83; m[k++] = 1; m[k++] = 0; m[k++] = 0x11;
    lerMapaDv(m, k, &f);
    ok(f.dvPerfil == 0, "registro curto: ignorado"); }

  printf(falhas ? "%d falha(s)\n" : "tudo certo\n", falhas);
  return falhas ? 1 : 0;
}
